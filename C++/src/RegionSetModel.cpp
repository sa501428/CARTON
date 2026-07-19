#include "RegionSetModel.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

RegionSetModel::RegionSetModel(QObject* parent) : QAbstractListModel(parent) {}

int RegionSetModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant RegionSetModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size() || role != EntryRole) return {};
    return m_entries[index.row()];
}

QHash<int, QByteArray> RegionSetModel::roleNames() const {
    return {{EntryRole, QByteArrayLiteral("entry")}};
}

QString RegionSetModel::kind() const {
    if (m_kind == Kind::Paired) return QStringLiteral("bedpe");
    if (m_kind == Kind::Projected) return QStringLiteral("bedpe-as-bed");
    if (m_kind == Kind::Single) return QStringLiteral("bed");
    return QStringLiteral("none");
}
QString RegionSetModel::sourcePath() const { return m_sourcePath; }
QString RegionSetModel::errorString() const { return m_errorString; }
qint64 RegionSetModel::windowSize() const { return m_windowSize; }
QVariantList RegionSetModel::entries() const { return m_entries; }

void RegionSetModel::setWindowSize(qint64 value) {
    value = std::clamp<qint64>(value, 1000, 1000000000LL);
    if (m_windowSize == value) return;
    m_windowSize = value;
    rebuildEntries();
}

QString RegionSetModel::localPath(const QUrl& url) {
    if (url.isLocalFile()) return url.toLocalFile();
    return url.toString();
}

QString RegionSetModel::chromosomeKey(const QString& value) {
    QString name = value.trimmed();
    if (name.startsWith(QStringLiteral("chr"), Qt::CaseInsensitive)) name.remove(0, 3);
    return name.toCaseFolded();
}

QVector<RegionSetModel::AxisRegion> RegionSetModel::mergeRegions(QVector<AxisRegion> regions) {
    std::sort(regions.begin(), regions.end(), [](const AxisRegion& a, const AxisRegion& b) {
        const QString ak = chromosomeKey(a.chr);
        const QString bk = chromosomeKey(b.chr);
        if (ak != bk) return ak < bk;
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });
    QVector<AxisRegion> merged;
    for (const AxisRegion& region : regions) {
        if (region.end <= region.start) continue;
        if (!merged.isEmpty() && chromosomeKey(merged.back().chr) == chromosomeKey(region.chr) &&
            region.start <= merged.back().end) {
            merged.back().end = std::max(merged.back().end, region.end);
            if (merged.back().label.isEmpty()) merged.back().label = region.label;
        } else {
            merged.push_back(region);
        }
    }
    return merged;
}

bool RegionSetModel::loadBed(const QUrl& url) { return parseBed(localPath(url), false); }
bool RegionSetModel::loadBedpeAsBed(const QUrl& url) { return parseBed(localPath(url), true); }
bool RegionSetModel::loadBedpe(const QUrl& url) { return parseBedpe(localPath(url)); }

bool RegionSetModel::parseBed(const QString& path, bool fromBedpe) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("Could not open region file: %1").arg(path));
        return false;
    }
    QVector<AxisRegion> parsed;
    QTextStream input(&file);
    int lineNumber = 0;
    while (!input.atEnd()) {
        ++lineNumber;
        const QString line = input.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("track"))) continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < (fromBedpe ? 6 : 3)) continue;
        auto appendAxis = [&](int offset, const QString& suffix) {
            bool okStart = false, okEnd = false;
            AxisRegion region;
            region.chr = parts[offset];
            region.start = parts[offset + 1].toLongLong(&okStart);
            region.end = parts[offset + 2].toLongLong(&okEnd);
            const QString baseLabel = parts.size() > 6 ? parts[6] : QFileInfo(path).baseName();
            region.label = baseLabel + suffix;
            if (okStart && okEnd && region.end > region.start) parsed.push_back(std::move(region));
        };
        appendAxis(0, fromBedpe ? QStringLiteral(" A") : QString());
        if (fromBedpe) appendAxis(3, QStringLiteral(" B"));
    }
    parsed = mergeRegions(std::move(parsed));
    if (parsed.isEmpty()) {
        setError(QStringLiteral("No valid regions found in %1").arg(path));
        return false;
    }
    m_kind = fromBedpe ? Kind::Projected : Kind::Single;
    m_sourcePath = path;
    m_axisRegions = std::move(parsed);
    m_pairedRegions.clear();
    setError(QString());
    rebuildEntries();
    return true;
}

bool RegionSetModel::parseBedpe(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("Could not open BEDPE file: %1").arg(path));
        return false;
    }
    QVector<PairedRegion> parsed;
    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("track"))) continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 6) continue;
        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        PairedRegion region;
        region.x.chr = parts[0];
        region.x.start = parts[1].toLongLong(&ok1);
        region.x.end = parts[2].toLongLong(&ok2);
        region.y.chr = parts[3];
        region.y.start = parts[4].toLongLong(&ok3);
        region.y.end = parts[5].toLongLong(&ok4);
        region.label = parts.size() > 6 ? parts[6] : QStringLiteral("Region %1").arg(parsed.size() + 1);
        region.x.label = region.label;
        region.y.label = region.label;
        if (ok1 && ok2 && ok3 && ok4 && region.x.end > region.x.start && region.y.end > region.y.start)
            parsed.push_back(std::move(region));
    }
    if (parsed.isEmpty()) {
        setError(QStringLiteral("No valid BEDPE regions found in %1").arg(path));
        return false;
    }
    m_kind = Kind::Paired;
    m_sourcePath = path;
    m_pairedRegions = std::move(parsed);
    m_axisRegions.clear();
    setError(QString());
    rebuildEntries();
    return true;
}

QVariantMap RegionSetModel::axisEntry(const AxisRegion& region, int index) const {
    const qint64 center = region.start + (region.end - region.start) / 2;
    const qint64 start = std::max<qint64>(0, center - m_windowSize / 2);
    QVariantMap item;
    item[QStringLiteral("index")] = index;
    item[QStringLiteral("label")] = region.label.isEmpty() ? QStringLiteral("Region %1").arg(index + 1) : region.label;
    item[QStringLiteral("chr")] = region.chr;
    item[QStringLiteral("rawStart")] = region.start;
    item[QStringLiteral("rawEnd")] = region.end;
    item[QStringLiteral("center")] = center;
    item[QStringLiteral("start")] = start;
    item[QStringLiteral("end")] = start + m_windowSize;
    return item;
}

QVariantMap RegionSetModel::pairedEntry(const PairedRegion& region, int index) const {
    const QVariantMap x = axisEntry(region.x, index);
    const QVariantMap y = axisEntry(region.y, index);
    QVariantMap item;
    item[QStringLiteral("index")] = index;
    item[QStringLiteral("label")] = region.label;
    item[QStringLiteral("chrX")] = x.value(QStringLiteral("chr"));
    item[QStringLiteral("x0")] = x.value(QStringLiteral("start"));
    item[QStringLiteral("x1")] = x.value(QStringLiteral("end"));
    item[QStringLiteral("rawX0")] = x.value(QStringLiteral("rawStart"));
    item[QStringLiteral("rawX1")] = x.value(QStringLiteral("rawEnd"));
    item[QStringLiteral("chrY")] = y.value(QStringLiteral("chr"));
    item[QStringLiteral("y0")] = y.value(QStringLiteral("start"));
    item[QStringLiteral("y1")] = y.value(QStringLiteral("end"));
    item[QStringLiteral("rawY0")] = y.value(QStringLiteral("rawStart"));
    item[QStringLiteral("rawY1")] = y.value(QStringLiteral("rawEnd"));
    return item;
}

void RegionSetModel::rebuildEntries() {
    beginResetModel();
    m_entries.clear();
    if (m_kind == Kind::Single || m_kind == Kind::Projected) {
        for (int i = 0; i < m_axisRegions.size(); ++i) m_entries.push_back(axisEntry(m_axisRegions[i], i));
    } else if (m_kind == Kind::Paired) {
        for (int i = 0; i < m_pairedRegions.size(); ++i) m_entries.push_back(pairedEntry(m_pairedRegions[i], i));
    }
    endResetModel();
    emit regionsChanged();
}

void RegionSetModel::clear() {
    m_kind = Kind::None;
    m_sourcePath.clear();
    m_axisRegions.clear();
    m_pairedRegions.clear();
    setError(QString());
    rebuildEntries();
}

QVariantMap RegionSetModel::state() const {
    QVariantMap result;
    result[QStringLiteral("kind")] = kind();
    result[QStringLiteral("sourcePath")] = m_sourcePath;
    result[QStringLiteral("windowSize")] = m_windowSize;
    return result;
}

bool RegionSetModel::restoreState(const QVariantMap& state) {
    setWindowSize(state.value(QStringLiteral("windowSize"), 2000000).toLongLong());
    const QUrl url = QUrl::fromUserInput(state.value(QStringLiteral("sourcePath")).toString());
    const QString nextKind = state.value(QStringLiteral("kind")).toString();
    if (nextKind == QStringLiteral("bedpe")) return loadBedpe(url);
    if (nextKind == QStringLiteral("bedpe-as-bed")) return loadBedpeAsBed(url);
    if (nextKind == QStringLiteral("bed")) return loadBed(url);
    clear();
    return true;
}

void RegionSetModel::setError(const QString& error) {
    if (m_errorString == error) return;
    m_errorString = error;
    emit errorStringChanged();
}
