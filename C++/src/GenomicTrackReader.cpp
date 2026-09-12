#include "GenomicTrackReader.h"

#include <QFileInfo>
#include <QStringList>
#include <QUrl>

#include <igv/igv.hpp>

#include <cstddef>
#include <exception>
#include <string>
#include <unordered_map>

namespace {
// Every interval of a track stays resident so the view can rebin it at any
// locus without going back to disk. The budget bounds that: a genome-wide
// bedGraph at 500bp fits, anything finer is cut short (text formats) or
// thinned (big* formats) by the reader, which is why it is surfaced per track
// rather than only in a transient status line.
constexpr std::size_t kMaxResidentRecords = 6000000;

// Contig names repeat across millions of records. Interning them lets every
// record share one buffer instead of allocating its own QString, which
// roughly halves the resident size of a large track and removes the dominant
// cost of loading one.
class ContigNames {
public:
    QString intern(const std::string& contig) {
        const auto found = cache_.find(contig);
        if (found != cache_.end()) return found->second;
        return cache_.emplace(contig, QString::fromUtf8(contig.data(),
                                                        static_cast<qsizetype>(contig.size())))
            .first->second;
    }

private:
    std::unordered_map<std::string, QString> cache_;
};

std::string utf8(const QString& value) {
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

QString sourceBaseName(const QString& pathOrUrl) {
    const QUrl url = QUrl::fromUserInput(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
    const QString baseName = QFileInfo(path).baseName();
    return baseName.isEmpty() ? QStringLiteral("track") : baseName;
}

QString cartonFormat(igv::Format format) {
    switch (format) {
        case igv::Format::bed: return QStringLiteral("bed");
        case igv::Format::bedgraph: return QStringLiteral("bedGraph");
        case igv::Format::wig: return QStringLiteral("wig");
        case igv::Format::bigwig: return QStringLiteral("bigWig");
        case igv::Format::bigbed: return QStringLiteral("bigBed");
        case igv::Format::bedpe: return QStringLiteral("bedpe");
        default: return QString::fromUtf8(igv::format_name(format).data(),
                                          static_cast<qsizetype>(igv::format_name(format).size()));
    }
}

QColor parseColor(const std::string& value, const QColor& fallback) {
    const QString token = QString::fromUtf8(value);
    QColor color(token);
    if (color.isValid()) return color;
    const QStringList rgb = token.split(QLatin1Char(','));
    if (rgb.size() != 3) return fallback;
    bool redOk = false, greenOk = false, blueOk = false;
    const int red = rgb[0].toInt(&redOk);
    const int green = rgb[1].toInt(&greenOk);
    const int blue = rgb[2].toInt(&blueOk);
    return redOk && greenOk && blueOk ? QColor(red, green, blue) : fallback;
}

bool isIndexedBinaryFormat(igv::Format format) {
    return format == igv::Format::bigwig || format == igv::Format::bigbed;
}

template <typename Record>
QString batchWarning(const QString& kind, const igv::RecordBatch<Record>& batch, igv::Format format) {
    QStringList warnings;
    if (batch.truncated) {
        // The two reader families lose records differently, and the
        // difference matters when reading a plot: text formats stop dead at
        // the budget, so the track simply ends partway through the genome.
        warnings.push_back(isIndexedBinaryFormat(format)
            ? QStringLiteral("%1 exceeded the %2 record budget and was thinned across the whole file")
                  .arg(kind)
                  .arg(kMaxResidentRecords)
            : QStringLiteral("%1 exceeded the %2 record budget; everything past that point in the file was not read")
                  .arg(kind)
                  .arg(kMaxResidentRecords));
    }
    if (batch.skipped_records > 0) {
        warnings.push_back(QStringLiteral("skipped %1 malformed record%2")
                               .arg(batch.skipped_records)
                               .arg(batch.skipped_records == 1 ? QString() : QStringLiteral("s")));
    }
    return warnings.isEmpty() ? QString() : warnings.join(QStringLiteral("; ")) + QLatin1Char('.');
}
}

GenomicTrackReadResult readGenomicTrack(const QString& pathOrUrl) {
    GenomicTrackReadResult result;
    try {
        const igv::Resource resource{.uri = utf8(pathOrUrl)};
        const igv::Format format = igv::detect_format(resource);
        result.format = cartonFormat(format);
        const QString defaultName = sourceBaseName(pathOrUrl);
        igv::AnyReader reader = igv::open_reader(resource);

        if (auto* featureReader = std::get_if<std::unique_ptr<igv::FeatureReader>>(&reader)) {
            auto batch = (*featureReader)->read_all(kMaxResidentRecords);
            result.features.reserve(static_cast<qsizetype>(batch.records.size()));
            ContigNames contigs;
            for (const igv::Feature& record : batch.records) {
                GenomicTrackFeature feature;
                feature.chr = contigs.intern(record.interval.contig);
                feature.start = record.interval.start;
                feature.end = record.interval.end;
                feature.name = record.name.empty() ? defaultName : QString::fromUtf8(record.name);
                feature.value = record.score.value_or(1.0);
                if (record.color) feature.color = parseColor(*record.color, feature.color);
                result.features.push_back(std::move(feature));
            }
            result.warning = batchWarning(QStringLiteral("Track"), batch, format);
            return result;
        }

        if (auto* signalReader = std::get_if<std::unique_ptr<igv::SignalReader>>(&reader)) {
            auto batch = (*signalReader)->read_all(kMaxResidentRecords);
            result.features.reserve(static_cast<qsizetype>(batch.records.size()));
            ContigNames contigs;
            const QColor signalColor("#4b7bec");
            for (const igv::SignalValue& record : batch.records) {
                result.features.push_back({
                    contigs.intern(record.interval.contig), record.interval.start, record.interval.end,
                    defaultName, record.value, signalColor});
            }
            result.warning = batchWarning(QStringLiteral("Track"), batch, format);
            return result;
        }

        result.warning = QStringLiteral("%1 is not a feature or signal track.").arg(pathOrUrl);
    } catch (const std::exception& error) {
        result.warning = QString::fromUtf8(error.what());
    }
    return result;
}

GenomicInteractionReadResult readGenomicInteractions(const QString& pathOrUrl) {
    GenomicInteractionReadResult result;
    try {
        const igv::Resource resource{.uri = utf8(pathOrUrl)};
        const igv::Format format = igv::detect_format(resource);
        result.format = cartonFormat(format);
        const QString defaultName = sourceBaseName(pathOrUrl);
        igv::AnyReader reader = igv::open_reader(resource);
        auto* interactionReader = std::get_if<std::unique_ptr<igv::InteractionReader>>(&reader);
        if (interactionReader == nullptr) {
            result.warning = QStringLiteral("%1 is not an interaction file.").arg(pathOrUrl);
            return result;
        }

        auto batch = (*interactionReader)->read_all(kMaxResidentRecords);
        result.interactions.reserve(static_cast<qsizetype>(batch.records.size()));
        for (const igv::Interaction& record : batch.records) {
            GenomicInteraction interaction;
            interaction.chr1 = QString::fromUtf8(record.first.contig);
            interaction.start1 = record.first.start;
            interaction.end1 = record.first.end;
            interaction.chr2 = QString::fromUtf8(record.second.contig);
            interaction.start2 = record.second.start;
            interaction.end2 = record.second.end;
            interaction.name = record.name.empty() ? defaultName : QString::fromUtf8(record.name);
            interaction.value = record.score.value_or(1.0);
            for (const auto& [key, value] : record.attributes) {
                interaction.attributes.insert(QString::fromUtf8(key), QString::fromUtf8(value));
            }
            if (const auto color = record.attributes.find("color"); color != record.attributes.end()) {
                interaction.color = parseColor(color->second, interaction.color);
            }
            result.interactions.push_back(std::move(interaction));
        }
        result.warning = batchWarning(QStringLiteral("Interaction file"), batch, format);
    } catch (const std::exception& error) {
        result.warning = QString::fromUtf8(error.what());
    }
    return result;
}

GenomicCytobandReadResult readGenomicCytobands(const QString& pathOrUrl) {
    GenomicCytobandReadResult result;
    try {
        const igv::Resource resource{.uri = utf8(pathOrUrl), .format = "cytoband"};
        auto reader = igv::open_cytoband(resource);
        auto batch = reader->read_all(kMaxResidentRecords);
        result.cytobands.reserve(static_cast<qsizetype>(batch.records.size()));
        for (const igv::Feature& record : batch.records) {
            const auto stain = record.attributes.find("stain");
            result.cytobands.push_back({
                QString::fromUtf8(record.interval.contig), record.interval.start, record.interval.end,
                QString::fromUtf8(record.name),
                stain == record.attributes.end() ? QStringLiteral("gneg") : QString::fromUtf8(stain->second)});
        }
        result.warning = batchWarning(QStringLiteral("Cytoband file"), batch, igv::Format::bed);
    } catch (const std::exception& error) {
        result.warning = QString::fromUtf8(error.what());
    }
    return result;
}
