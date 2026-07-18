#include "GenomicTrackReader.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QtEndian>
#include <curl/curl.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

namespace {
constexpr quint32 kBigWigMagic = 0x888FFC26U;
constexpr quint32 kBigBedMagic = 0x8789F2EBU;
constexpr quint32 kBPlusMagic = 0x78CA8C91U;
constexpr quint32 kRTreeMagic = 0x2468ACE0U;
constexpr qsizetype kMaxResidentTrackFeatures = 1000000;

size_t appendBytes(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* output = static_cast<QByteArray*>(userp);
    output->append(static_cast<const char*>(contents), static_cast<qsizetype>(total));
    return total;
}

QByteArray readBytes(const QString& pathOrUrl) {
    if (pathOrUrl.startsWith(QStringLiteral("http://")) || pathOrUrl.startsWith(QStringLiteral("https://"))) {
        QByteArray bytes;
        CURL* curl = curl_easy_init();
        if (!curl) return {};
        curl_easy_setopt(curl, CURLOPT_URL, pathOrUrl.toUtf8().constData());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBytes);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        const CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return res == CURLE_OK ? bytes : QByteArray();
    }
    QFile file(pathOrUrl);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QByteArray inflateChunk(const QByteArray& input, int expectedSize) {
    if (expectedSize <= 0) return input;
    QByteArray output;
    output.resize(expectedSize);
    uLongf destLen = static_cast<uLongf>(output.size());
    int code = uncompress(reinterpret_cast<Bytef*>(output.data()), &destLen,
                          reinterpret_cast<const Bytef*>(input.constData()), static_cast<uLongf>(input.size()));
    if (code != Z_OK) return {};
    output.resize(static_cast<qsizetype>(destLen));
    return output;
}

QByteArray gunzipBytes(const QByteArray& input) {
    if (input.size() < 2 || static_cast<unsigned char>(input[0]) != 0x1f ||
        static_cast<unsigned char>(input[1]) != 0x8b) {
        return input;
    }

    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) return {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());

    QByteArray output;
    char buffer[32768];
    int code = Z_OK;
    while (code == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        code = inflate(&stream, Z_NO_FLUSH);
        if (code == Z_OK || code == Z_STREAM_END) {
            const qsizetype produced = static_cast<qsizetype>(sizeof(buffer) - stream.avail_out);
            if (produced > 0) output.append(buffer, produced);
        }
    }
    inflateEnd(&stream);
    return code == Z_STREAM_END ? output : QByteArray();
}

class BinaryReader {
public:
    BinaryReader(QByteArray bytes, bool littleEndian) : m_bytes(std::move(bytes)), m_little(littleEndian) {}
    void seek(qint64 offset) { m_pos = std::clamp<qint64>(offset, 0, static_cast<qint64>(m_bytes.size())); }
    qint64 pos() const { return m_pos; }
    qint64 size() const { return m_bytes.size(); }
    bool remaining(qint64 n) const { return m_pos + n <= m_bytes.size(); }
    QByteArray slice(qint64 offset, qint64 length) const {
        if (offset < 0 || length <= 0 || offset >= m_bytes.size()) return {};
        return m_bytes.mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(std::min<qint64>(length, m_bytes.size() - offset)));
    }
    quint8 u8() { return remaining(1) ? static_cast<quint8>(m_bytes[m_pos++]) : 0; }
    quint16 u16() {
        if (!remaining(2)) return 0;
        quint16 v = 0;
        std::memcpy(&v, m_bytes.constData() + m_pos, 2);
        m_pos += 2;
        return m_little ? qFromLittleEndian(v) : qFromBigEndian(v);
    }
    quint32 u32() {
        if (!remaining(4)) return 0;
        quint32 v = 0;
        std::memcpy(&v, m_bytes.constData() + m_pos, 4);
        m_pos += 4;
        return m_little ? qFromLittleEndian(v) : qFromBigEndian(v);
    }
    quint64 u64() {
        if (!remaining(8)) return 0;
        quint64 v = 0;
        std::memcpy(&v, m_bytes.constData() + m_pos, 8);
        m_pos += 8;
        return m_little ? qFromLittleEndian(v) : qFromBigEndian(v);
    }
    float f32() {
        quint32 raw = u32();
        float v = 0.0f;
        std::memcpy(&v, &raw, 4);
        return v;
    }
    QString fixedString(int size) {
        if (!remaining(size)) return {};
        QByteArray data = m_bytes.mid(static_cast<qsizetype>(m_pos), size);
        m_pos += size;
        const int zero = data.indexOf('\0');
        if (zero >= 0) data.truncate(zero);
        return QString::fromUtf8(data);
    }
    QString zString() {
        const int zero = m_bytes.indexOf('\0', m_pos);
        if (zero < 0) {
            QByteArray data = m_bytes.mid(static_cast<qsizetype>(m_pos));
            m_pos = m_bytes.size();
            return QString::fromUtf8(data);
        }
        QByteArray data = m_bytes.mid(static_cast<qsizetype>(m_pos), static_cast<qsizetype>(zero - m_pos));
        m_pos = zero + 1;
        return QString::fromUtf8(data);
    }

private:
    QByteArray m_bytes;
    bool m_little = true;
    qint64 m_pos = 0;
};

struct BigHeader {
    bool bigWig = false;
    bool littleEndian = true;
    quint64 chromTreeOffset = 0;
    quint64 fullDataOffset = 0;
    quint64 fullIndexOffset = 0;
    quint32 uncompressBufSize = 0;
};

struct LeafChunk {
    quint32 startChromIx = 0;
    quint32 startBase = 0;
    quint64 offset = 0;
    quint64 size = 0;
};

QColor parseColor(const QString& token, const QColor& fallback) {
    if (token.startsWith('#')) {
        QColor c(token);
        return c.isValid() ? c : fallback;
    }
    const QStringList rgb = token.split(',');
    if (rgb.size() == 3) {
        bool rOk = false, gOk = false, bOk = false;
        const int r = rgb[0].toInt(&rOk);
        const int g = rgb[1].toInt(&gOk);
        const int b = rgb[2].toInt(&bOk);
        if (rOk && gOk && bOk) return QColor(r, g, b);
    }
    return fallback;
}

bool appendTrackFeature(QVector<GenomicTrackFeature>& out, const GenomicTrackFeature& feature) {
    if (out.size() >= kMaxResidentTrackFeatures) {
        return false;
    }
    out.push_back(feature);
    return true;
}

void appendBedLike(const QStringList& parts, QVector<GenomicTrackFeature>& out, const QString& defaultName) {
    if (parts.size() < 3) return;
    bool startOk = false, endOk = false;
    GenomicTrackFeature feature;
    feature.chr = parts[0];
    feature.start = parts[1].toLongLong(&startOk);
    feature.end = parts[2].toLongLong(&endOk);
    if (parts.size() > 3 && parts[3] != ".") {
        QString name = parts[3];
        name.remove('"');
        feature.name = name;
    } else {
        feature.name = defaultName;
    }
    if (parts.size() > 4) {
        bool valueOk = false;
        const double score = parts[4].toDouble(&valueOk);
        feature.value = valueOk ? score : 1.0;
    }
    if (parts.size() > 8) feature.color = parseColor(parts[8], feature.color);
    if (startOk && endOk && feature.end > feature.start) appendTrackFeature(out, feature);
}

GenomicTrackReadResult parseTextTrack(const QString& path, const QByteArray& bytes) {
    GenomicTrackReadResult result;
    const QString defaultName = QFileInfo(path).baseName();
    const QString lowerPath = path.toLower();
    if (lowerPath.endsWith(QStringLiteral(".bed")) || lowerPath.endsWith(QStringLiteral(".bed.gz"))) {
        result.format = QStringLiteral("bed");
    } else if (lowerPath.endsWith(QStringLiteral(".bedgraph")) || lowerPath.endsWith(QStringLiteral(".bedgraph.gz")) ||
               lowerPath.endsWith(QStringLiteral(".bdg")) || lowerPath.endsWith(QStringLiteral(".bdg.gz"))) {
        result.format = QStringLiteral("bedGraph");
    } else if (lowerPath.endsWith(QStringLiteral(".wig")) || lowerPath.endsWith(QStringLiteral(".wig.gz"))) {
        result.format = QStringLiteral("wig");
    }
    const QString text = QString::fromUtf8(bytes);
    QTextStream in(const_cast<QString*>(&text), QIODevice::ReadOnly);
    QString wigChr;
    qint64 wigPosition = 0;
    qint64 wigStep = 1;
    qint64 wigSpan = 1;
    enum class WigMode { None, Fixed, Variable };
    WigMode wigMode = WigMode::None;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(QStringLiteral("browser"))) continue;
        if (line.startsWith(QStringLiteral("track"))) {
            if (line.contains(QStringLiteral("bedGraph"), Qt::CaseInsensitive)) result.format = QStringLiteral("bedGraph");
            else if (line.contains(QStringLiteral("wig"), Qt::CaseInsensitive)) result.format = QStringLiteral("wig");
            else if (line.contains(QStringLiteral("bed"), Qt::CaseInsensitive)) result.format = QStringLiteral("bed");
            continue;
        }
        if (line.startsWith(QStringLiteral("fixedStep"))) {
            result.format = QStringLiteral("wig");
            wigMode = WigMode::Fixed;
            for (const QString& token : line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
                const QStringList kv = token.split('=');
                if (kv.size() != 2) continue;
                if (kv[0] == QStringLiteral("chrom")) wigChr = kv[1];
                else if (kv[0] == QStringLiteral("start")) wigPosition = kv[1].toLongLong() - 1;
                else if (kv[0] == QStringLiteral("step")) wigStep = kv[1].toLongLong();
                else if (kv[0] == QStringLiteral("span")) wigSpan = kv[1].toLongLong();
            }
            continue;
        }
        if (line.startsWith(QStringLiteral("variableStep"))) {
            result.format = QStringLiteral("wig");
            wigMode = WigMode::Variable;
            for (const QString& token : line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
                const QStringList kv = token.split('=');
                if (kv.size() != 2) continue;
                if (kv[0] == QStringLiteral("chrom")) wigChr = kv[1];
                else if (kv[0] == QStringLiteral("span")) wigSpan = kv[1].toLongLong();
            }
            continue;
        }
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (wigMode == WigMode::Fixed && !wigChr.isEmpty() && !parts.isEmpty()) {
            GenomicTrackFeature f;
            f.chr = wigChr;
            f.start = wigPosition;
            f.end = wigPosition + wigSpan;
            f.value = parts[0].toDouble();
            f.name = defaultName;
            appendTrackFeature(result.features, f);
            wigPosition += wigStep;
        } else if (wigMode == WigMode::Variable && !wigChr.isEmpty() && parts.size() >= 2) {
            GenomicTrackFeature f;
            f.chr = wigChr;
            f.start = parts[0].toLongLong() - 1;
            f.end = f.start + wigSpan;
            f.value = parts[1].toDouble();
            f.name = defaultName;
            appendTrackFeature(result.features, f);
        } else if (parts.size() >= 4) {
            bool vOk = false;
            parts[3].toDouble(&vOk);
            if (vOk && result.format != QStringLiteral("bed")) {
                result.format = result.format.isEmpty() ? QStringLiteral("bedGraph") : result.format;
                GenomicTrackFeature f;
                f.chr = parts[0];
                f.start = parts[1].toLongLong();
                f.end = parts[2].toLongLong();
                f.value = parts[3].toDouble();
                f.name = defaultName;
                if (f.end > f.start) appendTrackFeature(result.features, f);
            } else {
                result.format = result.format.isEmpty() ? QStringLiteral("bed") : result.format;
                appendBedLike(parts, result.features, defaultName);
            }
        } else {
            appendBedLike(parts, result.features, defaultName);
        }
        if (result.features.size() >= kMaxResidentTrackFeatures) {
            result.warning = QStringLiteral("Track was capped at %1 intervals to keep memory bounded.").arg(kMaxResidentTrackFeatures);
            break;
        }
    }
    if (result.format.isEmpty()) result.format = QStringLiteral("text");
    return result;
}

bool parseBigHeader(BinaryReader& reader, BigHeader& header) {
    reader.seek(0);
    const quint32 magicLE = reader.u32();
    if (magicLE == kBigWigMagic || magicLE == kBigBedMagic) {
        header.littleEndian = true;
        header.bigWig = magicLE == kBigWigMagic;
    } else {
        reader = BinaryReader(reader.slice(0, reader.size()), false);
        reader.seek(0);
        const quint32 magicBE = reader.u32();
        if (magicBE != kBigWigMagic && magicBE != kBigBedMagic) return false;
        header.littleEndian = false;
        header.bigWig = magicBE == kBigWigMagic;
    }
    reader.u16();
    reader.u16();
    header.chromTreeOffset = reader.u64();
    header.fullDataOffset = reader.u64();
    header.fullIndexOffset = reader.u64();
    reader.u16();
    reader.u16();
    reader.u64();
    reader.u64();
    header.uncompressBufSize = reader.u32();
    return true;
}

void walkBPlus(BinaryReader& reader, qint64 offset, int keySize, int valSize, std::map<int, QString>& idToName) {
    reader.seek(offset);
    const quint8 type = reader.u8();
    reader.u8();
    const int count = reader.u16();
    for (int i = 0; i < count; ++i) {
        const QString key = reader.fixedString(keySize);
        if (type == 1) {
            const qint64 valuePos = reader.pos();
            if (valSize >= 8) {
                const int chromId = static_cast<int>(reader.u32());
                reader.u32();
                idToName[chromId] = key;
                if (valSize > 8) reader.seek(valuePos + valSize);
            } else {
                reader.seek(valuePos + valSize);
            }
        } else {
            const quint64 child = reader.u64();
            walkBPlus(reader, static_cast<qint64>(child), keySize, valSize, idToName);
        }
    }
}

std::map<int, QString> readChromTree(BinaryReader& reader, quint64 offset) {
    std::map<int, QString> idToName;
    reader.seek(static_cast<qint64>(offset));
    if (reader.u32() != kBPlusMagic) return idToName;
    reader.u32();
    const int keySize = static_cast<int>(reader.u32());
    const int valSize = static_cast<int>(reader.u32());
    reader.u64();
    reader.u64();
    walkBPlus(reader, static_cast<qint64>(offset + 32), keySize, valSize, idToName);
    return idToName;
}

void walkRTree(BinaryReader& reader, qint64 offset, QVector<LeafChunk>& chunks) {
    reader.seek(offset);
    const quint8 type = reader.u8();
    reader.u8();
    const int count = reader.u16();
    for (int i = 0; i < count; ++i) {
        const quint32 startChromIx = reader.u32();
        const quint32 startBase = reader.u32();
        reader.u32();
        reader.u32();
        const quint64 dataOffset = reader.u64();
        if (type == 1) {
            const quint64 dataSize = reader.u64();
            chunks.push_back({startChromIx, startBase, dataOffset, dataSize});
        } else {
            walkRTree(reader, static_cast<qint64>(dataOffset), chunks);
        }
    }
}

QVector<LeafChunk> readRTreeLeaves(BinaryReader& reader, quint64 offset) {
    QVector<LeafChunk> chunks;
    reader.seek(static_cast<qint64>(offset));
    if (reader.u32() != kRTreeMagic) return chunks;
    reader.seek(static_cast<qint64>(offset + 48));
    walkRTree(reader, reader.pos(), chunks);
    return chunks;
}

GenomicTrackReadResult parseBigBinary(const QString& path, const QByteArray& bytes) {
    GenomicTrackReadResult result;
    BinaryReader reader(bytes, true);
    BigHeader header;
    if (!parseBigHeader(reader, header)) {
        result.warning = QStringLiteral("Not a bigWig/bigBed file.");
        return result;
    }
    reader = BinaryReader(bytes, header.littleEndian);
    result.format = header.bigWig ? QStringLiteral("bigWig") : QStringLiteral("bigBed");
    const QString defaultName = QFileInfo(path).baseName();
    const std::map<int, QString> idToName = readChromTree(reader, header.chromTreeOffset);
    QVector<LeafChunk> chunks = readRTreeLeaves(reader, header.fullIndexOffset);
    if (idToName.empty() || chunks.empty()) {
        result.warning = QStringLiteral("Could not read bigWig/bigBed chromosome or data index.");
        return result;
    }
    // R-tree leaves are not necessarily visited in genome order, so without this
    // sort a capped file (see below) would only ever show data for whichever
    // chromosome happened to come first in the index rather than the whole genome.
    std::sort(chunks.begin(), chunks.end(), [](const LeafChunk& a, const LeafChunk& b) {
        if (a.startChromIx != b.startChromIx) return a.startChromIx < b.startChromIx;
        return a.startBase < b.startBase;
    });
    // We don't know the item density up front, so use total compressed bytes as a
    // rough proxy for total feature count and, if that exceeds what we can hold,
    // sample chunks evenly across the whole (now genome-ordered) file instead of
    // reading a prefix. That way a capped load still covers every chromosome,
    // just more thinly, rather than the first one encountered and nothing else.
    quint64 totalBytes = 0;
    for (const LeafChunk& chunk : chunks) totalBytes += chunk.size;
    constexpr quint64 kAssumedBytesPerFeature = 24;
    const quint64 affordableBytes = static_cast<quint64>(kMaxResidentTrackFeatures) * kAssumedBytesPerFeature;
    qsizetype chunkStride = 1;
    if (totalBytes > affordableBytes) {
        chunkStride = static_cast<qsizetype>(std::max<quint64>(1, (totalBytes + affordableBytes - 1) / affordableBytes));
    }
    bool sampled = chunkStride > 1;
    for (qsizetype chunkIndex = 0; chunkIndex < chunks.size(); chunkIndex += chunkStride) {
        const LeafChunk& chunk = chunks[chunkIndex];
        QByteArray block = reader.slice(static_cast<qint64>(chunk.offset), static_cast<qint64>(chunk.size));
        if (header.uncompressBufSize > 0) {
            block = inflateChunk(block, static_cast<int>(header.uncompressBufSize));
            if (block.isEmpty()) continue;
        }
        BinaryReader blockReader(block, header.littleEndian);
        if (header.bigWig) {
            if (!blockReader.remaining(24)) continue;
            const int chromId = static_cast<int>(blockReader.u32());
            const qint64 blockStart = blockReader.u32();
            blockReader.u32();
            const qint64 itemStep = blockReader.u32();
            const qint64 itemSpan = blockReader.u32();
            const int type = blockReader.u8();
            blockReader.u8();
            int itemCount = blockReader.u16();
            int idx = 0;
            while (itemCount-- > 0 && blockReader.remaining(4)) {
                GenomicTrackFeature f;
                f.chr = idToName.count(chromId) ? idToName.at(chromId) : QString();
                f.name = defaultName;
                if (type == 1 && blockReader.remaining(12)) {
                    f.start = blockReader.u32();
                    f.end = blockReader.u32();
                    f.value = blockReader.f32();
                } else if (type == 2 && blockReader.remaining(8)) {
                    f.start = blockReader.u32();
                    f.end = f.start + itemSpan;
                    f.value = blockReader.f32();
                } else if (type == 3 && blockReader.remaining(4)) {
                    f.start = blockStart + idx * itemStep;
                    f.end = f.start + itemSpan;
                    f.value = blockReader.f32();
                    ++idx;
                } else {
                    break;
                }
                if (!f.chr.isEmpty() && f.end > f.start && !appendTrackFeature(result.features, f)) {
                    result.warning = QStringLiteral("Track was capped at %1 intervals (sampled across the genome) to keep memory bounded.").arg(kMaxResidentTrackFeatures);
                    return result;
                }
            }
        } else {
            while (blockReader.remaining(13)) {
                const int chromId = static_cast<int>(blockReader.u32());
                GenomicTrackFeature f;
                f.chr = idToName.count(chromId) ? idToName.at(chromId) : QString();
                f.start = blockReader.u32();
                f.end = blockReader.u32();
                const QString rest = blockReader.zString();
                const QStringList fields = rest.split('\t');
                f.name = fields.value(0, defaultName);
                bool scoreOk = false;
                const double score = fields.value(1).toDouble(&scoreOk);
                f.value = scoreOk ? score : 1.0;
                if (fields.size() > 5) f.color = parseColor(fields[5], f.color);
                if (!f.chr.isEmpty() && f.end > f.start && !appendTrackFeature(result.features, f)) {
                    result.warning = QStringLiteral("Track was capped at %1 intervals (sampled across the genome) to keep memory bounded.").arg(kMaxResidentTrackFeatures);
                    return result;
                }
            }
        }
    }
    if (sampled && result.warning.isEmpty()) {
        result.warning = QStringLiteral("Track was sampled across the genome (1 of every %1 index blocks) to keep memory bounded.").arg(chunkStride);
    }
    return result;
}
}

GenomicTrackReadResult readGenomicTrack(const QString& pathOrUrl) {
    QByteArray bytes = readBytes(pathOrUrl);
    GenomicTrackReadResult result;
    if (bytes.isEmpty()) {
        result.warning = QStringLiteral("Could not read %1").arg(pathOrUrl);
        return result;
    }
    const QByteArray decompressed = gunzipBytes(bytes);
    if (decompressed.isEmpty() && bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0x1f &&
        static_cast<unsigned char>(bytes[1]) == 0x8b) {
        result.warning = QStringLiteral("Could not decompress gzip track: %1").arg(pathOrUrl);
        return result;
    }
    bytes = decompressed;
    if (bytes.size() >= 4) {
        quint32 raw = 0;
        std::memcpy(&raw, bytes.constData(), sizeof(raw));
        const quint32 first = qFromLittleEndian(raw);
        const quint32 firstBE = qFromBigEndian(raw);
        if (first == kBigWigMagic || first == kBigBedMagic || firstBE == kBigWigMagic || firstBE == kBigBedMagic) {
            return parseBigBinary(pathOrUrl, bytes);
        }
    }
    return parseTextTrack(pathOrUrl, bytes);
}
