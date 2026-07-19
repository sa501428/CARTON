#include "DatasetRegistry.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>
#include <QUuid>

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString stripChrPrefix(QString name) {
    name = name.trimmed();
    if (name.startsWith(QStringLiteral("chr"), Qt::CaseInsensitive)) name.remove(0, 3);
    return name;
}

QByteArray readTextBytes(const QString& pathOrUrl) {
    if (pathOrUrl.startsWith(QStringLiteral("http://")) || pathOrUrl.startsWith(QStringLiteral("https://"))) {
        QByteArray bytes;
        CURL* curl = curl_easy_init();
        if (!curl) return {};
        const QByteArray encoded = pathOrUrl.toUtf8();
        curl_easy_setopt(curl, CURLOPT_URL, encoded.constData());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                         +[](void* contents, size_t size, size_t count, void* target) -> size_t {
                             const size_t total = size * count;
                             static_cast<QByteArray*>(target)->append(static_cast<const char*>(contents),
                                                                     static_cast<qsizetype>(total));
                             return total;
                         });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
        const CURLcode result = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return result == CURLE_OK ? bytes : QByteArray();
    }
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

DatasetRegistry* DatasetRegistry::instance() {
    static DatasetRegistry registry;
    return &registry;
}

DatasetRegistry::DatasetRegistry(QObject* parent)
    : QObject(parent),
      m_tileCache(std::make_shared<HicTileCache>()),
      m_resourcesModel(new WorkspaceListModel(this)) {
    QSettings settings;
    m_cacheLimitMB = std::clamp(settings.value(QStringLiteral("carton/cacheLimitMB"), 128).toInt(), 16, 4096);
    const std::size_t maxRecords = static_cast<std::size_t>(m_cacheLimitMB) * 1024ULL * 1024ULL /
                                   sizeof(contactRecord);
    m_tileCache->setLimits(maxRecords, std::max<std::size_t>(32, static_cast<std::size_t>(m_cacheLimitMB / 2)));
}

QAbstractItemModel* DatasetRegistry::resourcesModel() const { return m_resourcesModel; }
int DatasetRegistry::resourceCount() const {
    QMutexLocker locker(&m_mutex);
    return m_summaries.size();
}
int DatasetRegistry::cacheLimitMB() const { return m_cacheLimitMB; }
int DatasetRegistry::cacheTileCount() const { return static_cast<int>(m_tileCache->tileCount()); }
int DatasetRegistry::cacheRecordCount() const { return static_cast<int>(m_tileCache->recordCount()); }
double DatasetRegistry::cacheMemoryMB() const {
    return static_cast<double>(m_tileCache->recordCount() * sizeof(contactRecord)) / (1024.0 * 1024.0);
}

void DatasetRegistry::setCacheLimitMB(int value) {
    value = std::clamp(value, 16, 4096);
    if (m_cacheLimitMB == value) return;
    m_cacheLimitMB = value;
    const std::size_t maxRecords = static_cast<std::size_t>(value) * 1024ULL * 1024ULL / sizeof(contactRecord);
    m_tileCache->setLimits(maxRecords, std::max<std::size_t>(32, static_cast<std::size_t>(value / 2)));
    QSettings settings;
    settings.setValue(QStringLiteral("carton/cacheLimitMB"), value);
    emit cacheStatsChanged();
}

QString DatasetRegistry::canonicalSource(const QString& pathOrUrl) {
    const QUrl url = QUrl::fromUserInput(pathOrUrl);
    if (url.isLocalFile() || url.scheme().isEmpty()) {
        QFileInfo info(url.isLocalFile() ? url.toLocalFile() : pathOrUrl);
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    }
    QUrl normalized(url);
    normalized.setFragment(QString());
    return normalized.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString();
}

QString DatasetRegistry::canonicalResourceId(const QString& kind, const QString& pathOrUrl) const {
    return kind.trimmed().toLower() + QLatin1Char(':') + canonicalSource(pathOrUrl);
}

QString DatasetRegistry::chromosomeKey(const QString& name) {
    return stripChrPrefix(name).toCaseFolded();
}

QString DatasetRegistry::displayName(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    const QString fileName = QFileInfo(path).fileName();
    return fileName.isEmpty() ? pathOrUrl : fileName;
}

void DatasetRegistry::recordResourceLocked(const ResourceSummary& summary) {
    m_summaries.insert(summary.id, summary);
    scheduleModelRefresh();
}

void DatasetRegistry::scheduleModelRefresh() {
    if (m_refreshPending) return;
    m_refreshPending = true;
    QMetaObject::invokeMethod(this, [this]() { refreshModel(); }, Qt::QueuedConnection);
}

void DatasetRegistry::refreshModel() {
    QVariantList values;
    {
        QMutexLocker locker(&m_mutex);
        m_refreshPending = false;
        QList<ResourceSummary> summaries = m_summaries.values();
        std::sort(summaries.begin(), summaries.end(), [](const ResourceSummary& a, const ResourceSummary& b) {
            if (a.kind != b.kind) return a.kind < b.kind;
            return a.name.localeAwareCompare(b.name) < 0;
        });
        values.reserve(summaries.size());
        for (const ResourceSummary& summary : summaries) {
            QVariantMap item;
            item[QStringLiteral("id")] = summary.id;
            item[QStringLiteral("kind")] = summary.kind;
            item[QStringLiteral("name")] = summary.name;
            item[QStringLiteral("path")] = summary.source;
            item[QStringLiteral("count")] = summary.itemCount;
            item[QStringLiteral("custom")] = summary.custom;
            values.push_back(item);
        }
    }
    m_resourcesModel->setEntries(values);
    emit resourcesChanged();
}

PooledHicMetadataResult DatasetRegistry::loadHicMetadata(const QString& pathOrUrl) {
    PooledHicMetadataResult result;
    result.source = canonicalSource(pathOrUrl);
    result.id = canonicalResourceId(QStringLiteral("hic"), result.source);
    std::shared_ptr<HicEntry> entry;
    {
        QMutexLocker locker(&m_mutex);
        entry = m_hicEntries.value(result.id);
        if (!entry) {
            entry = std::make_shared<HicEntry>();
            entry->loading = true;
            m_hicEntries.insert(result.id, entry);
        } else {
            while (entry->loading) entry->ready.wait(&m_mutex);
            result.metadata = entry->metadata;
            result.error = entry->error;
            return result;
        }
    }

    std::shared_ptr<const HicFileMetadata> metadata;
    QString error;
    try {
        metadata = std::make_shared<const HicFileMetadata>(inspectHicFile(result.source.toStdString()));
    } catch (const std::exception& exception) {
        error = QString::fromUtf8(exception.what());
    }

    {
        QMutexLocker locker(&m_mutex);
        entry->metadata = metadata;
        entry->error = error;
        entry->loading = false;
        entry->ready.wakeAll();
        if (metadata) {
            recordResourceLocked({result.id, QStringLiteral("hic"), displayName(result.source), result.source,
                                  static_cast<qint64>(metadata->chromosomes.size()), false});
        }
    }
    result.metadata = metadata;
    result.error = error;
    return result;
}

PooledTrackResult DatasetRegistry::loadTrack(const QString& pathOrUrl) {
    PooledTrackResult result;
    const QString source = canonicalSource(pathOrUrl);
    result.id = canonicalResourceId(QStringLiteral("track"), source);
    {
        QMutexLocker locker(&m_mutex);
        if (const auto found = m_tracks.constFind(result.id); found != m_tracks.cend()) {
            result.data = found.value();
            return result;
        }
    }

    const GenomicTrackReadResult parsed = readGenomicTrack(source);
    if (parsed.features.isEmpty()) {
        result.error = parsed.warning.isEmpty()
            ? QStringLiteral("No intervals found in 1D track: %1").arg(source)
            : parsed.warning;
        return result;
    }

    auto data = std::make_shared<PooledTrackData>();
    data->id = result.id;
    data->source = source;
    data->name = QFileInfo(source).baseName();
    data->format = parsed.format;
    data->warning = parsed.warning;
    data->features = parsed.features;
    for (GenomicTrackFeature& feature : data->features) feature.chr = chromosomeKey(feature.chr);
    std::sort(data->features.begin(), data->features.end(), [](const auto& a, const auto& b) {
        if (a.chr != b.chr) return a.chr < b.chr;
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });
    data->minimum = std::numeric_limits<double>::infinity();
    data->maximum = -std::numeric_limits<double>::infinity();
    for (qsizetype index = 0; index < data->features.size(); ++index) {
        const GenomicTrackFeature& feature = data->features[index];
        data->minimum = std::min(data->minimum, feature.value);
        data->maximum = std::max(data->maximum, feature.value);
        auto range = data->chromosomeRanges.find(feature.chr);
        if (range == data->chromosomeRanges.end()) {
            data->chromosomeRanges.insert(feature.chr, {index, index + 1, feature.end - feature.start});
        } else {
            range->end = index + 1;
            range->maximumSpan = std::max(range->maximumSpan, feature.end - feature.start);
        }
    }
    if (!std::isfinite(data->minimum)) data->minimum = 0.0;
    if (!std::isfinite(data->maximum) || data->maximum <= data->minimum) data->maximum = data->minimum + 1.0;

    {
        QMutexLocker locker(&m_mutex);
        const auto existing = m_tracks.constFind(result.id);
        if (existing != m_tracks.cend()) data = std::const_pointer_cast<PooledTrackData>(existing.value());
        else {
            m_tracks.insert(result.id, data);
            recordResourceLocked({result.id, QStringLiteral("track"), displayName(source), source,
                                  data->features.size(), false});
        }
    }
    result.data = data;
    return result;
}

PooledAnnotationResult DatasetRegistry::loadAnnotations(const QString& pathOrUrl) {
    PooledAnnotationResult result;
    const QString source = canonicalSource(pathOrUrl);
    result.id = canonicalResourceId(QStringLiteral("annotation"), source);
    {
        QMutexLocker locker(&m_mutex);
        if (const auto found = m_annotations.constFind(result.id); found != m_annotations.cend()) {
            result.data = found.value();
            return result;
        }
    }

    const QByteArray bytes = readTextBytes(source);
    if (bytes.isEmpty()) {
        result.error = QStringLiteral("Could not open 2D annotations: %1").arg(source);
        return result;
    }
    auto data = std::make_shared<PooledAnnotationData>();
    data->id = result.id;
    data->source = source;
    data->name = QFileInfo(source).baseName();
    QString text = QString::fromUtf8(bytes);
    QTextStream input(&text, QIODevice::ReadOnly);
    int serial = 0;
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("track"))) continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 6) continue;
        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        PooledAnnotation annotation;
        annotation.id = result.id + QStringLiteral("#%1").arg(serial++);
        annotation.chr1 = parts[0];
        annotation.start1 = parts[1].toLongLong(&ok1);
        annotation.end1 = parts[2].toLongLong(&ok2);
        annotation.chr2 = parts[3];
        annotation.start2 = parts[4].toLongLong(&ok3);
        annotation.end2 = parts[5].toLongLong(&ok4);
        annotation.name = parts.size() > 6 ? parts[6] : data->name;
        if (parts.size() > 8) {
            const QColor parsedColor(parts[8]);
            if (parsedColor.isValid()) annotation.color = parsedColor;
        }
        if (ok1 && ok2 && ok3 && ok4 && annotation.end1 > annotation.start1 && annotation.end2 > annotation.start2)
            data->annotations.push_back(std::move(annotation));
    }
    if (data->annotations.isEmpty()) {
        result.error = QStringLiteral("No valid BEDPE annotations found in %1").arg(source);
        return result;
    }
    {
        QMutexLocker locker(&m_mutex);
        const auto existing = m_annotations.constFind(result.id);
        if (existing != m_annotations.cend()) data = existing.value();
        else {
            m_annotations.insert(result.id, data);
            recordResourceLocked({result.id, QStringLiteral("annotation"), displayName(source), source,
                                  data->annotations.size(), false});
        }
    }
    result.data = data;
    return result;
}

PooledAnnotationResult DatasetRegistry::annotationById(const QString& id) const {
    PooledAnnotationResult result;
    result.id = id;
    QMutexLocker locker(&m_mutex);
    result.data = m_annotations.value(id);
    if (!result.data) result.error = QStringLiteral("Unknown annotation resource: %1").arg(id);
    return result;
}

PooledAnnotationResult DatasetRegistry::createCustomAnnotations(const QString& name,
                                                                const QVector<PooledAnnotation>& annotations) {
    PooledAnnotationResult result;
    result.id = QStringLiteral("annotation:custom:%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto data = std::make_shared<PooledAnnotationData>();
    data->id = result.id;
    data->name = name.trimmed().isEmpty() ? QStringLiteral("Custom annotations") : name.trimmed();
    data->source = result.id;
    data->custom = true;
    data->annotations = annotations;
    {
        QMutexLocker locker(&m_mutex);
        m_annotations.insert(result.id, data);
        recordResourceLocked({result.id, QStringLiteral("annotation"), data->name, data->source,
                              data->annotations.size(), true});
    }
    result.data = data;
    return result;
}

PooledAnnotationResult DatasetRegistry::restoreCustomAnnotations(const QString& resourceId, const QString& name,
                                                                 const QVector<PooledAnnotation>& annotations) {
    if (resourceId.isEmpty()) return createCustomAnnotations(name, annotations);
    {
        QMutexLocker locker(&m_mutex);
        const auto existing = m_annotations.constFind(resourceId);
        if (existing != m_annotations.cend() && existing.value()->custom) {
            existing.value()->name = name.trimmed().isEmpty() ? existing.value()->name : name.trimmed();
            existing.value()->annotations = annotations;
            auto summary = m_summaries.find(resourceId);
            if (summary != m_summaries.end()) {
                summary->name = existing.value()->name;
                summary->itemCount = annotations.size();
                scheduleModelRefresh();
            }
            return {resourceId, QString(), existing.value()};
        }
    }

    PooledAnnotationResult result;
    result.id = resourceId.startsWith(QStringLiteral("annotation:custom:"))
        ? resourceId
        : QStringLiteral("annotation:custom:%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto data = std::make_shared<PooledAnnotationData>();
    data->id = result.id;
    data->name = name.trimmed().isEmpty() ? QStringLiteral("Custom annotations") : name.trimmed();
    data->source = result.id;
    data->custom = true;
    data->annotations = annotations;
    {
        QMutexLocker locker(&m_mutex);
        const auto existing = m_annotations.constFind(result.id);
        if (existing != m_annotations.cend()) {
            if (existing.value()->custom) {
                existing.value()->name = data->name;
                existing.value()->annotations = annotations;
                result.data = existing.value();
                auto summary = m_summaries.find(result.id);
                if (summary != m_summaries.end()) {
                    summary->name = data->name;
                    summary->itemCount = annotations.size();
                    scheduleModelRefresh();
                }
            }
            else result.error = QStringLiteral("Resource ID already belongs to a non-custom annotation: %1").arg(result.id);
            return result;
        }
        m_annotations.insert(result.id, data);
        recordResourceLocked({result.id, QStringLiteral("annotation"), data->name, data->source,
                              data->annotations.size(), true});
    }
    result.data = data;
    return result;
}

PooledAnnotationResult DatasetRegistry::forkAnnotations(const QString&, const QString& name,
                                                        const QVector<PooledAnnotation>& annotations) {
    return createCustomAnnotations(name, annotations);
}

HicTileCache* DatasetRegistry::tileCache() { return m_tileCache.get(); }
void DatasetRegistry::notifyCacheChanged() { emit cacheStatsChanged(); }
void DatasetRegistry::acquireReadSlot() { m_readSlots.acquire(); }
void DatasetRegistry::releaseReadSlot() { m_readSlots.release(); }
void DatasetRegistry::notifyAnnotationChanged(const QString& id) {
    QMutexLocker locker(&m_mutex);
    const auto data = m_annotations.value(id);
    auto summary = m_summaries.find(id);
    if (!data || summary == m_summaries.end()) return;
    summary->itemCount = data->annotations.size();
    scheduleModelRefresh();
}

QVariantList DatasetRegistry::resources(const QString& kind) const {
    QVariantList values;
    QMutexLocker locker(&m_mutex);
    for (const ResourceSummary& summary : m_summaries) {
        if (!kind.isEmpty() && summary.kind.compare(kind, Qt::CaseInsensitive) != 0) continue;
        QVariantMap item;
        item[QStringLiteral("id")] = summary.id;
        item[QStringLiteral("kind")] = summary.kind;
        item[QStringLiteral("name")] = summary.name;
        item[QStringLiteral("path")] = summary.source;
        item[QStringLiteral("count")] = summary.itemCount;
        item[QStringLiteral("custom")] = summary.custom;
        values.push_back(item);
    }
    return values;
}

QString DatasetRegistry::resourcePath(const QString& id) const {
    QMutexLocker locker(&m_mutex);
    return m_summaries.value(id).source;
}

QString DatasetRegistry::resourceKind(const QString& id) const {
    QMutexLocker locker(&m_mutex);
    return m_summaries.value(id).kind;
}

void DatasetRegistry::clearUnusedResources() {
    // The registry is deliberately session-scoped. Resources remain reusable
    // until the session ends; bounded matrix tiles are evicted independently.
}

bool DatasetRegistry::exportWorkspace(const QUrl& url, const QVariantList& tabStates) const {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QVariantMap workspace;
    workspace[QStringLiteral("schemaVersion")] = 2;
    workspace[QStringLiteral("kind")] = QStringLiteral("carton-workspace");
    workspace[QStringLiteral("tabs")] = tabStates;
    file.write(QJsonDocument::fromVariant(workspace).toJson(QJsonDocument::Indented));
    return file.commit();
}

QVariantList DatasetRegistry::importWorkspace(const QUrl& url) const {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return {};
    const QVariantMap state = document.object().toVariantMap();
    if (state.value(QStringLiteral("kind")).toString() == QStringLiteral("carton-workspace"))
        return state.value(QStringLiteral("tabs")).toList();
    return {state};
}
