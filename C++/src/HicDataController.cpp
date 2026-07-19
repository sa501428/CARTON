#include "HicDataController.h"
#include "GenomicTrackReader.h"
#include "WorkspaceListModel.h"

#include <QtConcurrent>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace {
constexpr int kRecentLimit = 10;
constexpr int kMinPearsonResolution = 50000;
constexpr int kMaxPearsonBins = 1200;

size_t appendCurlText(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* output = static_cast<QByteArray*>(userp);
    output->append(static_cast<const char*>(contents), static_cast<qsizetype>(total));
    return total;
}

quint64 recordKey(qint64 x, qint64 y) {
    const quint64 hx = static_cast<quint64>(std::hash<qint64>{}(x));
    const quint64 hy = static_cast<quint64>(std::hash<qint64>{}(y));
    return hx ^ (hy + 0x9e3779b97f4a7c15ULL + (hx << 6U) + (hx >> 2U));
}

// Track/annotation/cytoband files almost always use UCSC-style "chr1" names,
// while .hic files (especially those produced by older Juicer pipelines) often
// store bare names like "1". Without this, every 1D overlay silently renders
// zero features whenever the two files disagree on the prefix.
QString stripChrPrefix(const QString& name) {
    return name.startsWith(QStringLiteral("chr"), Qt::CaseInsensitive) ? name.mid(3) : name;
}

bool chrNamesEqual(const QString& a, const QString& b) {
    if (a.compare(b, Qt::CaseInsensitive) == 0) return true;
    return stripChrPrefix(a).compare(stripChrPrefix(b), Qt::CaseInsensitive) == 0;
}
}

HicDataController::HicDataController(QObject* parent)
    : QObject(parent),
      m_cache(std::make_unique<HicTileCache>()) {
    m_datasetsModel = new WorkspaceListModel(this);
    m_bookmarksModel = new WorkspaceListModel(this);
    m_tracksModel = new WorkspaceListModel(this);
    m_annotationsModel = new WorkspaceListModel(this);
    m_searchResultsModel = new WorkspaceListModel(this);
    QSettings settings;
    m_cacheLimitMB = std::clamp(settings.value(settingsKey() + QStringLiteral("/cacheLimitMB"), 128).toInt(), 16, 4096);
    const std::size_t maxRecords = static_cast<std::size_t>(m_cacheLimitMB) * 1024ULL * 1024ULL / sizeof(contactRecord);
    m_cache->setLimits(maxRecords, std::max<std::size_t>(24, static_cast<std::size_t>(m_cacheLimitMB / 4)));
    AnnotationLayer baseLayer;
    baseLayer.name = QStringLiteral("Selection");
    baseLayer.color = QColor("#111111");
    m_annotationLayers.push_back(baseLayer);
    connect(this, &HicDataController::tracksChanged, this, &HicDataController::refreshTracksModel);
    connect(this, &HicDataController::annotationsChanged, this, &HicDataController::refreshAnnotationsModel);
    connect(this, &HicDataController::viewHistoryChanged, this, &HicDataController::refreshBookmarksModel);
    refreshDatasetsModel();
    refreshBookmarksModel();
    refreshTracksModel();
    refreshAnnotationsModel();

    connect(&m_metadataWatcher, &QFutureWatcher<HicFileMetadata>::finished, this, [this]() {
        setBusy(false);
        try {
            applyMetadata(m_metadataWatcher.result());
            setStatus(QStringLiteral("Loaded %1.").arg(m_filePath));
        } catch (const std::exception& e) {
            setStatus(QStringLiteral("Failed to open file: %1").arg(e.what()));
        }
    });

    connect(&m_controlMetadataWatcher, &QFutureWatcher<HicFileMetadata>::finished, this, [this]() {
        setBusy(false);
        try {
            m_controlMetadata = m_controlMetadataWatcher.result();
            m_controlReady = true;
            if (!m_genomeId.isEmpty() && !m_controlMetadata.genomeID.empty() &&
                m_genomeId != QString::fromStdString(m_controlMetadata.genomeID)) {
                m_controlReady = false;
                setStatus(QStringLiteral("Control map genome %1 does not match %2.")
                              .arg(QString::fromStdString(m_controlMetadata.genomeID), m_genomeId));
            } else {
                setStatus(QStringLiteral("Control map ready: %1").arg(m_controlFilePath));
            }
        } catch (const std::exception& e) {
            m_controlReady = false;
            setStatus(QStringLiteral("Failed to open control map: %1").arg(e.what()));
        }
        emit controlReadyChanged();
        if (m_controlReady && matrixNeedsControl(m_matrixType)) {
            clearLoadedRegion();
            scheduleRequest();
        }
    });

    connect(&m_tileWatcher, &QFutureWatcher<TileResult>::finished, this, [this]() {
        TileResult result = m_tileWatcher.result();
        if (result.requestId != m_requestSerial) {
            if (m_reloadPending) {
                m_reloadPending = false;
                scheduleRequest();
            } else {
                setBusy(false);
            }
            return;
        }
        setBusy(false);
        if (!result.error.isEmpty()) {
            setStatus(result.error);
            if (m_reloadPending) {
                m_reloadPending = false;
                scheduleRequest();
            }
            return;
        }

        orientTileForRequestedAxes(result.tile);
        if (result.hasControl) {
            orientTileForRequestedAxes(result.controlTile);
        }
        std::vector<contactRecord> displayRecords = transformRecordsForDisplay(m_matrixType, result.tile.records,
                                                                               result.hasControl ? result.controlTile.records : std::vector<contactRecord>());
        std::vector<contactRecord> displayControlRecords;
        if (result.hasControl && matrixIsVs(m_matrixType)) {
            if (m_matrixType == QStringLiteral("pearsonvs")) {
                displayControlRecords = transformPearsonLike(result.controlTile.records);
            } else if (m_matrixType == QStringLiteral("logvs")) {
                displayControlRecords = result.controlTile.records;
            } else if (m_matrixType == QStringLiteral("logeovs")) {
                displayControlRecords = result.controlTile.records;
                for (contactRecord& record : displayControlRecords) {
                    record.counts = record.counts > 0 ? static_cast<float>(std::log(record.counts)) : 0.0f;
                }
            } else {
                displayControlRecords = result.controlTile.records;
            }
        }
        {
            QMutexLocker locker(&m_mutex);
            m_records = std::move(displayRecords);
            m_controlRecords = std::move(displayControlRecords);
            rebuildHoverLookupLocked();
            m_loadedKey = result.tile.key;
            m_hasLoadedKey = true;
        }
        updateAutoColorScale(m_records, m_controlRecords);
        if (!result.fromCache) {
            m_cache->put(std::move(result.tile));
            emit cacheStatsChanged();
        }

        setStatus(QStringLiteral("%1 records in view%2.")
                      .arg(recordCount())
                      .arg(result.fromCache ? QStringLiteral(" (cached)") : QString()));
        emit recordsChanged();
        if (m_reloadPending) {
            m_reloadPending = false;
            scheduleRequest();
        }
    });
}

HicDataController::~HicDataController() {
    m_metadataWatcher.cancel();
    m_metadataWatcher.waitForFinished();
    m_controlMetadataWatcher.cancel();
    m_controlMetadataWatcher.waitForFinished();
    m_tileWatcher.cancel();
    m_tileWatcher.waitForFinished();
}

QString HicDataController::filePath() const { return m_filePath; }
QString HicDataController::controlFilePath() const { return m_controlFilePath; }
bool HicDataController::controlReady() const { return m_controlReady; }
QString HicDataController::genomeId() const { return m_genomeId; }
QString HicDataController::status() const { return m_status; }
bool HicDataController::busy() const { return m_busy; }
QString HicDataController::chrX() const { return m_chrX; }
QString HicDataController::chrY() const { return m_chrY; }
QString HicDataController::matrixType() const { return m_matrixType; }
QString HicDataController::norm() const { return m_norm; }
int HicDataController::resolution() const { return m_resolution; }
qint64 HicDataController::x0() const { return m_x0; }
qint64 HicDataController::x1() const { return m_x1; }
qint64 HicDataController::y0() const { return m_y0; }
qint64 HicDataController::y1() const { return m_y1; }
double HicDataController::colorMax() const { return m_colorMax; }
double HicDataController::colorMin() const { return m_colorMin; }
bool HicDataController::colorMaxAuto() const { return m_colorMaxAuto; }
QString HicDataController::colorMap() const { return m_colorMap; }
QColor HicDataController::customLowColor() const { return m_customLowColor; }
QColor HicDataController::customHighColor() const { return m_customHighColor; }
int HicDataController::trackCount() const { return static_cast<int>(m_tracks.size()); }
int HicDataController::annotationCount() const {
    int count = 0;
    for (const AnnotationLayer& layer : m_annotationLayers) {
        count += layer.annotations.size();
    }
    return count;
}
int HicDataController::cytobandCount() const { return m_cytobands.size(); }
bool HicDataController::canUndoView() const { return !m_undoStack.isEmpty(); }
bool HicDataController::canRedoView() const { return !m_redoStack.isEmpty(); }
bool HicDataController::resolutionLocked() const { return m_resolutionLocked; }
bool HicDataController::xLocusLocked() const { return m_xLocusLocked; }
bool HicDataController::yLocusLocked() const { return m_yLocusLocked; }
bool HicDataController::wholeGenomeView() const { return isAllChromosome(m_chrX) || isAllChromosome(m_chrY); }
bool HicDataController::axisEndpointsOnly() const { return m_axisEndpointsOnly; }
bool HicDataController::showGridlines() const { return m_showGridlines; }
bool HicDataController::showChromosomeContext() const { return m_showChromosomeContext; }
bool HicDataController::darkMode() const { return m_darkMode; }
bool HicDataController::showTilesDebug() const { return m_showTilesDebug; }
int HicDataController::sparseFeatureLimit() const { return m_sparseFeatureLimit; }
QString HicDataController::selectedAnnotationId() const { return m_selectedAnnotationId; }
int HicDataController::cacheLimitMB() const { return m_cacheLimitMB; }
int HicDataController::cacheRecordCount() const { return static_cast<int>(m_cache->recordCount()); }
int HicDataController::cacheTileCount() const { return static_cast<int>(m_cache->tileCount()); }
double HicDataController::cacheMemoryMB() const {
    return static_cast<double>(m_cache->recordCount() * sizeof(contactRecord)) / (1024.0 * 1024.0);
}
QString HicDataController::renderingBackend() const {
#ifdef Q_OS_MACOS
    return QStringLiteral("Metal · Qt Scene Graph");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Direct3D · Qt Scene Graph");
#else
    return QStringLiteral("Vulkan/OpenGL · Qt Scene Graph");
#endif
}
QString HicDataController::matrixDimensions() const {
    if (m_resolution <= 0) return QStringLiteral("—");
    const qint64 columns = std::max<qint64>(1, (m_x1 - m_x0 + m_resolution - 1) / m_resolution);
    const qint64 rows = std::max<qint64>(1, (m_y1 - m_y0 + m_resolution - 1) / m_resolution);
    return QStringLiteral("%1 × %2 bins").arg(columns).arg(rows);
}
bool HicDataController::symmetricColorScale() const { return m_symmetricColorScale; }
double HicDataController::colorPercentile() const { return m_colorPercentile; }
QColor HicDataController::missingValueColor() const { return m_missingValueColor; }
bool HicDataController::zeroTransparent() const { return m_zeroTransparent; }
QAbstractItemModel* HicDataController::datasetsModel() const { return m_datasetsModel; }
QAbstractItemModel* HicDataController::bookmarksModel() const { return m_bookmarksModel; }
QAbstractItemModel* HicDataController::tracksModel() const { return m_tracksModel; }
QAbstractItemModel* HicDataController::annotationsModel() const { return m_annotationsModel; }
QAbstractItemModel* HicDataController::searchResultsModel() const { return m_searchResultsModel; }
QString HicDataController::workspaceSearch() const { return m_workspaceSearch; }

int HicDataController::recordCount() const {
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_records.size());
}

void HicDataController::openFile(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Choose a local or remote .hic file."));
        return;
    }
    if (m_metadataWatcher.isRunning() || m_controlMetadataWatcher.isRunning() || m_tileWatcher.isRunning()) {
        setStatus(QStringLiteral("Please wait for the current load to finish."));
        return;
    }

    m_filePath = path;
    m_cache->clear();
    emit cacheStatsChanged();
    clearLoadedRegion();
    {
        QMutexLocker locker(&m_mutex);
        m_records.clear();
        m_controlRecords.clear();
        m_recordHoverLookup.clear();
        m_controlHoverLookup.clear();
        m_records.shrink_to_fit();
        m_controlRecords.shrink_to_fit();
    }
    emit filePathChanged();
    emit recordsChanged();
    addRecent(QStringLiteral("recentMaps"), path);
    setBusy(true);
    setStatus(QStringLiteral("Reading .hic header..."));
    m_metadataWatcher.setFuture(QtConcurrent::run([path]() {
        return inspectHicFile(path.toStdString());
    }));
}

void HicDataController::openControlFile(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Choose a control .hic file."));
        return;
    }
    if (m_metadataWatcher.isRunning() || m_controlMetadataWatcher.isRunning() || m_tileWatcher.isRunning()) {
        setStatus(QStringLiteral("Please wait for the current map load to finish."));
        return;
    }

    m_controlFilePath = path;
    m_controlReady = false;
    m_controlMetadata = HicFileMetadata{};
    clearLoadedRegion();
    {
        QMutexLocker locker(&m_mutex);
        m_controlRecords.clear();
        m_controlHoverLookup.clear();
        m_controlRecords.shrink_to_fit();
    }
    emit controlFilePathChanged();
    emit controlReadyChanged();
    emit recordsChanged();
    addRecent(QStringLiteral("recentControlMaps"), path);
    m_cache->clear();
    emit cacheStatsChanged();
    setBusy(true);
    setStatus(QStringLiteral("Reading control .hic header..."));
    m_controlMetadataWatcher.setFuture(QtConcurrent::run([path]() {
        return inspectHicFile(path.toStdString());
    }));
}

void HicDataController::openRecentMap(const QString& path) {
    openFile(QUrl::fromUserInput(path));
}

void HicDataController::openRecentControlMap(const QString& path) {
    openControlFile(QUrl::fromUserInput(path));
}

void HicDataController::loadTrack(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    loadTrackFromPath(path);
}

void HicDataController::loadTrackFromPath(const QString& path) {
    const GenomicTrackReadResult parsed = readGenomicTrack(path);
    if (parsed.features.empty()) {
        setStatus(parsed.warning.isEmpty()
                      ? QStringLiteral("No intervals found in 1D track: %1").arg(path)
                      : parsed.warning);
        return;
    }
    TrackLayer track;
    track.name = QFileInfo(path).baseName();
    track.source = path;
    track.format = parsed.format;
    const QString normalizedFormat = parsed.format.toLower();
    track.renderMode = (normalizedFormat == QStringLiteral("bed") ||
                        normalizedFormat == QStringLiteral("bigbed"))
                           ? QStringLiteral("feature")
                           : QStringLiteral("signal");
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    for (const GenomicTrackFeature& feature : parsed.features) {
        TrackFeature segment;
        segment.chr = feature.chr;
        segment.start = feature.start;
        segment.end = feature.end;
        segment.label = feature.name;
        segment.value = feature.value;
        segment.color = feature.color;
        if (segment.end > segment.start) {
            track.features.push_back(segment);
            minValue = std::min(minValue, segment.value);
            maxValue = std::max(maxValue, segment.value);
        }
    }
    if (track.features.isEmpty()) {
        setStatus(QStringLiteral("No valid intervals found in 1D track: %1").arg(path));
        return;
    }
    track.minValue = std::min(0.0, minValue);
    track.maxValue = std::max(0.0, maxValue);
    if (track.maxValue <= track.minValue) track.maxValue = track.minValue + 1.0;
    track.color = track.features.front().color;
    const int loaded = track.features.size();
    m_tracks.push_back(std::move(track));
    setStatus(QStringLiteral("Loaded %1 %2 track intervals%3.")
                  .arg(loaded)
                  .arg(parsed.format.isEmpty() ? QStringLiteral("genomics") : parsed.format)
                  .arg(parsed.warning.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(parsed.warning)));
    emit tracksChanged();
}

void HicDataController::loadAnnotations(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    loadAnnotationsFromPath(path);
}

void HicDataController::loadAnnotationsFromPath(const QString& path) {
    const QString text = readTextResource(path);
    if (text.isEmpty()) {
        setStatus(QStringLiteral("Could not open 2D annotations: %1").arg(path));
        return;
    }

    if (m_annotationLayers.isEmpty()) {
        addAnnotationLayer(QStringLiteral("Selection"));
    }
    AnnotationLayer& layer = m_annotationLayers[m_activeAnnotationLayer];
    layer.undoStack = layer.annotations;

    QTextStream in(const_cast<QString*>(&text), QIODevice::ReadOnly);
    int loaded = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 6) {
            continue;
        }
        Annotation2D annotation;
        annotation.id = QStringLiteral("a%1").arg(QDateTime::currentMSecsSinceEpoch() + loaded);
        annotation.chr1 = parts[0];
        annotation.start1 = parts[1].toLongLong();
        annotation.end1 = parts[2].toLongLong();
        annotation.chr2 = parts[3];
        annotation.start2 = parts[4].toLongLong();
        annotation.end2 = parts[5].toLongLong();
        annotation.name = parts.size() >= 7 ? parts[6] : QFileInfo(path).baseName();
        if (parts.size() >= 9) {
            const QColor parsed(parts[8]);
            if (parsed.isValid()) {
                annotation.color = parsed;
            }
        } else {
            annotation.color = layer.color;
        }
        if (annotation.end1 > annotation.start1 && annotation.end2 > annotation.start2) {
            layer.annotations.push_back(annotation);
            ++loaded;
        }
    }
    setStatus(QStringLiteral("Loaded %1 2D annotations.").arg(loaded));
    emit annotationsChanged();
}

void HicDataController::loadCytobands(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    const QString text = readTextResource(path);
    if (text.isEmpty()) {
        setStatus(QStringLiteral("Could not open cytobands: %1").arg(path));
        return;
    }

    QVector<Cytoband> parsed;
    QTextStream in(const_cast<QString*>(&text), QIODevice::ReadOnly);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(QStringLiteral("track"))) continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;
        Cytoband band;
        band.chr = parts[0];
        band.start = parts[1].toLongLong();
        band.end = parts[2].toLongLong();
        band.name = parts.size() > 3 ? parts[3] : QString();
        band.stain = parts.size() > 4 ? parts[4].toLower() : QStringLiteral("gneg");
        if (band.stain == QStringLiteral("acen")) band.color = QColor("#dc5a67");
        else if (band.stain == QStringLiteral("stalk")) band.color = QColor("#6baed6");
        else if (band.stain == QStringLiteral("gvar")) band.color = QColor("#94a3b8");
        else if (band.stain.startsWith(QStringLiteral("gpos"))) {
            bool ok = false;
            const int darkness = band.stain.mid(4).toInt(&ok);
            const int shade = ok ? std::clamp(238 - darkness * 2, 38, 220) : 105;
            band.color = QColor(shade, shade, shade);
        } else band.color = QColor("#e5e7eb");
        if (band.end > band.start) parsed.push_back(band);
    }
    if (parsed.isEmpty()) {
        setStatus(QStringLiteral("No valid cytobands found in %1.").arg(path));
        return;
    }
    m_cytobands = std::move(parsed);
    setStatus(QStringLiteral("Loaded %1 cytobands.").arg(m_cytobands.size()));
    emit cytobandsChanged();
}

void HicDataController::clearCytobands() {
    if (m_cytobands.isEmpty()) return;
    m_cytobands.clear();
    emit cytobandsChanged();
}

QVariantList HicDataController::visibleCytobands(bool xAxis) const {
    QVariantList values;
    const QString chr = xAxis ? m_chrX : m_chrY;
    const qint64 start = xAxis ? m_x0 : m_y0;
    const qint64 end = xAxis ? m_x1 : m_y1;
    for (const Cytoband& band : m_cytobands) {
        if (!chrNamesEqual(band.chr, chr) || band.end <= start || band.start >= end) continue;
        QVariantMap item;
        item["start"] = std::max(start, band.start);
        item["end"] = std::min(end, band.end);
        item["name"] = band.name;
        item["stain"] = band.stain;
        item["color"] = band.color;
        values.push_back(item);
    }
    return values;
}

void HicDataController::clearTracks() {
    m_tracks.clear();
    emit tracksChanged();
}

void HicDataController::clearAnnotations() {
    for (AnnotationLayer& layer : m_annotationLayers) {
        layer.undoStack = layer.annotations;
        layer.annotations.clear();
    }
    m_selectedAnnotationId.clear();
    emit annotationsChanged();
}

QVariantList HicDataController::recentMaps() const {
    return recentList(QStringLiteral("recentMaps"));
}

QVariantList HicDataController::recentControlMaps() const {
    return recentList(QStringLiteral("recentControlMaps"));
}

QVariantList HicDataController::savedLocations() const {
    QSettings settings;
    return settings.value(settingsKey() + QStringLiteral("/savedLocations")).toList();
}

QVariantList HicDataController::savedStates() const {
    QSettings settings;
    return settings.value(settingsKey() + QStringLiteral("/savedStates")).toList();
}

QVariantList HicDataController::chromosomeNames() const {
    QVariantList names;
    if (!m_metadata.chromosomes.empty()) {
        names.push_back(QStringLiteral("All"));
    }
    for (const chromosome& chr : m_metadata.chromosomes) {
        if (chr.index > 0) {
            names.push_back(QString::fromStdString(chr.name));
        }
    }
    return names;
}

QVariantList HicDataController::chromosomeBoundaries() const {
    QVariantList values;
    qint64 offset = 0;
    for (const chromosome& chr : m_metadata.chromosomes) {
        if (chr.index <= 0) {
            continue;
        }
        QVariantMap item;
        item["name"] = QString::fromStdString(chr.name);
        item["start"] = offset;
        offset += chr.length;
        item["end"] = offset;
        values.push_back(item);
    }
    return values;
}

QVariantList HicDataController::resolutions() const {
    QVariantList values;
    for (int32_t resolution : m_metadata.bpResolutions) {
        values.push_back(resolution);
    }
    return values;
}

QVariantList HicDataController::normalizations() const {
    QVariantList values;
    for (const std::string& norm : m_metadata.normalizations) {
        values.push_back(QString::fromStdString(norm));
    }
    if (values.empty()) {
        values.push_back(QStringLiteral("NONE"));
    }
    return values;
}

QVariantList HicDataController::matrixTypes() const {
    return {
        QStringLiteral("observed"), QStringLiteral("log"), QStringLiteral("expected"), QStringLiteral("oe"),
        QStringLiteral("logoe"), QStringLiteral("explogoe"), QStringLiteral("control"),
        QStringLiteral("logcontrol"), QStringLiteral("controloe"), QStringLiteral("pearson"),
        QStringLiteral("controlpearson"), QStringLiteral("pearsonvs"), QStringLiteral("vs"),
        QStringLiteral("logvs"), QStringLiteral("oevs"), QStringLiteral("logeovs"),
        QStringLiteral("ratio"), QStringLiteral("ratio1"), QStringLiteral("logratio"),
        QStringLiteral("diff"), QStringLiteral("oeratio")
    };
}

QVariantList HicDataController::trackSummaries() const {
    QVariantList values;
    for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
        const TrackLayer& track = m_tracks[static_cast<std::size_t>(i)];
        QVariantMap item;
        item["index"] = i;
        item["name"] = track.name;
        item["source"] = track.source;
        item["format"] = track.format;
        item["renderMode"] = track.renderMode;
        item["positiveColor"] = track.color;
        item["negativeColor"] = track.negativeColor;
        item["min"] = track.minValue;
        item["max"] = track.maxValue;
        item["logScale"] = track.logScale;
        item["reduction"] = track.reduction;
        item["binSize"] = track.binSize;
        item["effectiveBinSize"] = track.binSize > 0 ? track.binSize : std::max(1, m_resolution);
        item["coverage"] = track.coverage;
        item["eigenvector"] = track.eigenvector;
        item["featureCount"] = track.features.size();
        item["visible"] = track.visible;
        item["collapsed"] = track.collapsed;
        item["autoscale"] = track.autoscale;
        item["height"] = track.height;
        values.push_back(item);
    }
    return values;
}

QVariantList HicDataController::annotationLayerSummaries() const {
    QVariantList values;
    for (int i = 0; i < m_annotationLayers.size(); ++i) {
        const AnnotationLayer& layer = m_annotationLayers[i];
        QVariantMap item;
        item["index"] = i;
        item["name"] = layer.name;
        item["color"] = layer.color;
        item["visible"] = layer.visible;
        item["transparent"] = layer.transparent;
        item["sparse"] = layer.sparse;
        item["enlarged"] = layer.enlarged;
        item["active"] = i == m_activeAnnotationLayer;
        item["count"] = layer.annotations.size();
        item["canUndo"] = !layer.undoStack.isEmpty();
        values.push_back(item);
    }
    return values;
}

QVariantList HicDataController::visibleTrackSegments(bool xAxis) const {
    return visibleTrackSegmentsForPixels(xAxis, 0);
}

QVariantList HicDataController::visibleTrackSegmentsForPixels(bool xAxis, int pixelCount) const {
    QVariantList values;
    const QString chr = xAxis ? m_chrX : m_chrY;
    const qint64 start = xAxis ? m_x0 : m_y0;
    const qint64 end = xAxis ? m_x1 : m_y1;
    const qint64 span = std::max<qint64>(1, end - start);
    pixelCount = std::clamp(pixelCount, 0, 8192);

    struct RenderedSegment {
        qint64 start = 0;
        qint64 end = 0;
        double value = 0.0;
        double rawValue = 0.0;
        QColor color;
        QString name;
        qint64 renderedBinSize = 0;
    };

    for (int trackIndex = 0; trackIndex < static_cast<int>(m_tracks.size()); ++trackIndex) {
        const TrackLayer& track = m_tracks[static_cast<std::size_t>(trackIndex)];
        if (!track.visible || track.collapsed) continue;
        const auto signedLog = [](double value) { return std::copysign(std::log1p(std::abs(value)), value); };
        QVector<const TrackFeature*> visible;
        for (const TrackFeature& segment : track.features) {
            if (chrNamesEqual(segment.chr, chr) && segment.end > start && segment.start < end)
                visible.push_back(&segment);
        }

        QVector<RenderedSegment> rendered;
        if (track.renderMode == QStringLiteral("signal") && pixelCount > 0 &&
            track.reduction != QStringLiteral("none")) {
            // Quantitative tracks are first aligned to the Hi-C map's genomic
            // bins (or a per-track fixed override). If several genomic bins
            // land in one screen pixel, apply the selected IGV-style windowing
            // function again at that coarser display resolution.
            const qint64 sourceBinSize = std::max<qint64>(1, track.binSize > 0 ? track.binSize : m_resolution);
            const double basesPerPixel = static_cast<double>(span) / pixelCount;
            const qint64 binsPerPixel = std::max<qint64>(1, static_cast<qint64>(std::ceil(basesPerPixel / sourceBinSize)));
            const qint64 renderBinSize = sourceBinSize > std::numeric_limits<qint64>::max() / binsPerPixel
                                             ? std::numeric_limits<qint64>::max()
                                             : sourceBinSize * binsPerPixel;
            const qint64 alignedStart = start >= 0 ? (start / renderBinSize) * renderBinSize : start;
            const int binCount = static_cast<int>(std::clamp<qint64>(
                (end - alignedStart + renderBinSize - 1) / renderBinSize, 1, pixelCount + 2));
            QVector<double> sums(binCount, 0.0);
            QVector<double> weights(binCount, 0.0);
            QVector<double> maxima(binCount, -std::numeric_limits<double>::infinity());
            QVector<double> minima(binCount, std::numeric_limits<double>::infinity());
            QVector<double> rawSums(binCount, 0.0);
            QVector<double> rawMaxima(binCount, -std::numeric_limits<double>::infinity());
            QVector<double> rawMinima(binCount, std::numeric_limits<double>::infinity());
            QVector<bool> occupied(binCount, false);

            for (const TrackFeature* segment : visible) {
                const double clippedStart = static_cast<double>(std::max(segment->start, start));
                const double clippedEnd = static_cast<double>(std::min(segment->end, end));
                if (clippedEnd <= clippedStart) continue;
                const double displayValue = track.logScale ? signedLog(segment->value) : segment->value;
                const int firstBin = std::clamp(static_cast<int>(std::floor((clippedStart - alignedStart) / renderBinSize)),
                                                0, binCount - 1);
                const int lastBin = std::clamp(static_cast<int>(std::ceil((clippedEnd - alignedStart) / renderBinSize)) - 1,
                                               firstBin, binCount - 1);
                for (int bin = firstBin; bin <= lastBin; ++bin) {
                    const double binStart = alignedStart + bin * static_cast<double>(renderBinSize);
                    const double binEnd = alignedStart + (bin + 1) * static_cast<double>(renderBinSize);
                    const double overlap = std::max(0.0, std::min(clippedEnd, binEnd) - std::max(clippedStart, binStart));
                    if (overlap <= 0.0) continue;
                    occupied[bin] = true;
                    sums[bin] += displayValue * overlap;
                    rawSums[bin] += segment->value * overlap;
                    weights[bin] += overlap;
                    maxima[bin] = std::max(maxima[bin], displayValue);
                    minima[bin] = std::min(minima[bin], displayValue);
                    rawMaxima[bin] = std::max(rawMaxima[bin], segment->value);
                    rawMinima[bin] = std::min(rawMinima[bin], segment->value);
                }
            }

            rendered.reserve(binCount);
            for (int bin = 0; bin < binCount; ++bin) {
                if (!occupied[bin] || weights[bin] <= 0.0) continue;
                RenderedSegment segment;
                segment.start = alignedStart + bin * renderBinSize;
                segment.end = segment.start + renderBinSize;
                segment.renderedBinSize = renderBinSize;
                if (track.reduction == QStringLiteral("max")) {
                    segment.value = maxima[bin];
                    segment.rawValue = rawMaxima[bin];
                } else if (track.reduction == QStringLiteral("min")) {
                    segment.value = minima[bin];
                    segment.rawValue = rawMinima[bin];
                } else {
                    segment.value = sums[bin] / weights[bin];
                    segment.rawValue = rawSums[bin] / weights[bin];
                }
                segment.color = segment.value < 0 ? track.negativeColor : track.color;
                rendered.push_back(std::move(segment));
            }
        } else if (track.renderMode == QStringLiteral("signal") && pixelCount > 0) {
            // "None" preserves individual values, subject only to a generous
            // draw-command cap so a million-record track cannot stall Canvas.
            const qsizetype maxSegments = std::max(200, pixelCount * 4);
            const qsizetype stride = std::max<qsizetype>(1, (visible.size() + maxSegments - 1) / maxSegments);
            rendered.reserve(std::min<qsizetype>(visible.size(), maxSegments));
            for (qsizetype index = 0; index < visible.size(); index += stride) {
                const TrackFeature* source = visible[index];
                RenderedSegment segment;
                segment.start = source->start;
                segment.end = source->end;
                segment.name = source->label;
                segment.rawValue = source->value;
                segment.value = track.logScale ? signedLog(source->value) : source->value;
                segment.color = segment.value < 0 ? track.negativeColor : track.color;
                rendered.push_back(std::move(segment));
            }
        } else if (track.renderMode == QStringLiteral("feature") && pixelCount > 0 &&
                   visible.size() > std::max(200, pixelCount * 4)) {
            // At broad loci, dense BED/bigBed annotations can contain far more
            // intervals than there are drawable columns. Merge their pixel
            // extents instead of flooding the scene graph with redundant boxes.
            QVector<QPair<int, int>> pixelIntervals;
            pixelIntervals.reserve(visible.size());
            for (const TrackFeature* segment : visible) {
                const int first = std::clamp(static_cast<int>((std::max(segment->start, start) - start) * pixelCount / span),
                                             0, pixelCount - 1);
                const int last = std::clamp(static_cast<int>(std::ceil(
                                                static_cast<double>(std::min(segment->end, end) - start) * pixelCount / span)),
                                            first + 1, pixelCount);
                pixelIntervals.push_back(qMakePair(first, last));
            }
            std::sort(pixelIntervals.begin(), pixelIntervals.end(), [](const auto& a, const auto& b) {
                return a.first < b.first || (a.first == b.first && a.second < b.second);
            });
            for (const auto& interval : pixelIntervals) {
                if (!rendered.isEmpty() && interval.first <= rendered.back().end) {
                    rendered.back().end = std::max<qint64>(rendered.back().end, interval.second);
                    continue;
                }
                RenderedSegment segment;
                // Temporarily keep pixel coordinates; convert to genomic
                // coordinates after the merge has bounded the result size.
                segment.start = interval.first;
                segment.end = interval.second;
                segment.value = 1.0;
                segment.rawValue = 1.0;
                segment.color = track.color;
                rendered.push_back(std::move(segment));
            }
            for (RenderedSegment& segment : rendered) {
                segment.start = start + static_cast<qint64>(std::floor(segment.start * static_cast<double>(span) / pixelCount));
                segment.end = start + static_cast<qint64>(std::ceil(segment.end * static_cast<double>(span) / pixelCount));
                segment.end = std::max(segment.start + 1, std::min(segment.end, end));
            }
        } else {
            rendered.reserve(visible.size());
            for (const TrackFeature* source : visible) {
                RenderedSegment segment;
                segment.start = source->start;
                segment.end = source->end;
                segment.name = source->label;
                segment.rawValue = source->value;
                segment.value = track.logScale ? signedLog(source->value) : source->value;
                segment.color = segment.value < 0
                                    ? track.negativeColor
                                    : (source->color.isValid() ? source->color : track.color);
                rendered.push_back(std::move(segment));
            }
        }

        double rangeMin = track.minValue;
        double rangeMax = track.maxValue;
        if (track.autoscale && track.renderMode == QStringLiteral("signal") && !rendered.isEmpty()) {
            rangeMin = 0.0;
            rangeMax = 0.0;
            for (const RenderedSegment& segment : rendered) {
                rangeMin = std::min(rangeMin, segment.value);
                rangeMax = std::max(rangeMax, segment.value);
            }
            if (rangeMax <= rangeMin) rangeMax = rangeMin + 1.0;
        }
        const double displayMin = track.autoscale ? rangeMin : (track.logScale ? signedLog(rangeMin) : rangeMin);
        const double displayMax = track.autoscale ? rangeMax : (track.logScale ? signedLog(rangeMax) : rangeMax);
        for (const RenderedSegment& segment : rendered) {
            QVariantMap item;
            item["trackIndex"] = trackIndex;
            item["trackName"] = track.name;
            item["kind"] = track.renderMode;
            item["format"] = track.format;
            item["binSize"] = track.binSize > 0 ? track.binSize : std::max(1, m_resolution);
            item["renderedBinSize"] = segment.renderedBinSize;
            item["name"] = segment.name;
            item["start"] = segment.start;
            item["end"] = segment.end;
            item["rawValue"] = segment.rawValue;
            item["value"] = segment.value;
            item["min"] = displayMin;
            item["max"] = displayMax;
            item["color"] = segment.color;
            values.push_back(item);
        }
    }
    return values;
}

QVariantList HicDataController::visibleAnnotations() const {
    QVariantList values;
    for (const AnnotationLayer& layer : m_annotationLayers) {
        if (!layer.visible) {
            continue;
        }
        int emitted = 0;
        for (const Annotation2D& annotation : layer.annotations) {
        if (layer.sparse && emitted >= m_sparseFeatureLimit) {
            break;
        }
        const bool direct = chrNamesEqual(annotation.chr1, m_chrX) && chrNamesEqual(annotation.chr2, m_chrY) &&
                            annotation.end1 >= m_x0 && annotation.start1 <= m_x1 &&
                            annotation.end2 >= m_y0 && annotation.start2 <= m_y1;
        const bool reflected = chrNamesEqual(annotation.chr1, m_chrY) && chrNamesEqual(annotation.chr2, m_chrX) &&
                               annotation.end1 >= m_y0 && annotation.start1 <= m_y1 &&
                               annotation.end2 >= m_x0 && annotation.start2 <= m_x1;
        if (!direct && !reflected) {
            continue;
        }
        QVariantMap item;
        item["name"] = annotation.name;
        item["x0"] = direct ? annotation.start1 : annotation.start2;
        item["x1"] = direct ? annotation.end1 : annotation.end2;
        item["y0"] = direct ? annotation.start2 : annotation.start1;
        item["y1"] = direct ? annotation.end2 : annotation.end1;
        item["id"] = annotation.id;
        item["name"] = annotation.name;
        item["color"] = annotation.highlighted || annotation.id == m_selectedAnnotationId ? QColor("#ffb703") : annotation.color;
        item["transparent"] = layer.transparent;
        item["enlarged"] = layer.enlarged;
        values.push_back(item);
        ++emitted;

        if (m_chrX == m_chrY && chrNamesEqual(annotation.chr1, annotation.chr2) &&
            (annotation.start1 != annotation.start2 || annotation.end1 != annotation.end2)) {
            QVariantMap mirror = item;
            mirror["x0"] = item["y0"];
            mirror["x1"] = item["y1"];
            mirror["y0"] = item["x0"];
            mirror["y1"] = item["x1"];
            values.push_back(mirror);
        }
        }
    }
    return values;
}

QString HicDataController::positionText(double xFraction, double yFraction) const {
    const qint64 x = m_x0 + static_cast<qint64>((m_x1 - m_x0) * std::clamp(xFraction, 0.0, 1.0));
    const qint64 y = m_y0 + static_cast<qint64>((m_y1 - m_y0) * std::clamp(yFraction, 0.0, 1.0));
    const qint64 binX = m_resolution > 0 ? (x / m_resolution) * m_resolution : x;
    const qint64 binY = m_resolution > 0 ? (y / m_resolution) * m_resolution : y;

    bool hasPrimaryCount = false;
    bool hasControlCount = false;
    float primaryCount = 0.0f;
    float controlCount = 0.0f;
    {
        QMutexLocker locker(&m_mutex);
        auto findCount = [&](const QHash<quint64, float>& lookup, float& value) {
            auto it = lookup.constFind(recordLookupKey(static_cast<int32_t>(binX), static_cast<int32_t>(binY)));
            if (it == lookup.cend() && m_chrX == m_chrY)
                it = lookup.constFind(recordLookupKey(static_cast<int32_t>(binY), static_cast<int32_t>(binX)));
            if (it == lookup.cend()) return false;
            value = it.value();
            return true;
        };
        hasPrimaryCount = findCount(m_recordHoverLookup, primaryCount);
        hasControlCount = findCount(m_controlHoverLookup, controlCount);
    }

    QString text = QStringLiteral("%1:%2 | %3:%4 | bin %5 bp")
        .arg(m_chrX).arg(x)
        .arg(m_chrY).arg(y)
        .arg(m_resolution);
    if (hasPrimaryCount) {
        text += QStringLiteral(" | value %1").arg(QString::number(primaryCount, 'g', 5));
    }
    if (hasControlCount) {
        text += QStringLiteral(" | control %1").arg(QString::number(controlCount, 'g', 5));
    }
    QStringList trackHits;
    for (const TrackLayer& track : m_tracks) {
        if (!track.visible || track.collapsed) continue;
        QStringList values;
        for (const TrackFeature& feature : track.features) {
            if ((chrNamesEqual(feature.chr, m_chrX) && x >= feature.start && x < feature.end) ||
                (chrNamesEqual(feature.chr, m_chrY) && y >= feature.start && y < feature.end)) {
                const QString label = feature.label.isEmpty() ? QString() : feature.label + QStringLiteral("=");
                values.push_back(label + QString::number(feature.value, 'g', 5));
                if (values.size() >= 3) break;
            }
        }
        if (!values.isEmpty()) trackHits.push_back(track.name + QStringLiteral(": ") + values.join(QStringLiteral(", ")));
    }
    if (!trackHits.isEmpty()) text += QStringLiteral(" | tracks ") + trackHits.join(QStringLiteral("; "));

    QStringList annotationHits;
    for (const AnnotationLayer& layer : m_annotationLayers) {
        if (!layer.visible) continue;
        for (const Annotation2D& annotation : layer.annotations) {
            const bool direct = chrNamesEqual(annotation.chr1, m_chrX) && chrNamesEqual(annotation.chr2, m_chrY) &&
                                x >= annotation.start1 && x < annotation.end1 &&
                                y >= annotation.start2 && y < annotation.end2;
            const bool mirrored = m_chrX == m_chrY && chrNamesEqual(annotation.chr1, annotation.chr2) &&
                                  x >= annotation.start2 && x < annotation.end2 &&
                                  y >= annotation.start1 && y < annotation.end1;
            if (direct || mirrored) {
                annotationHits.push_back(annotation.name.isEmpty() ? layer.name : annotation.name);
                if (annotationHits.size() >= 4) break;
            }
        }
    }
    if (!annotationHits.isEmpty()) text += QStringLiteral(" | 2D ") + annotationHits.join(QStringLiteral(", "));
    return text;
}

void HicDataController::copyPosition(double xFraction, double yFraction) const {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(positionText(xFraction, yFraction));
    }
}

void HicDataController::copyText(const QString& text) const {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void HicDataController::copyTopPosition(double xFraction) const {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        const qint64 x = m_x0 + static_cast<qint64>((m_x1 - m_x0) * std::clamp(xFraction, 0.0, 1.0));
        clipboard->setText(QStringLiteral("%1:%2:%3").arg(m_chrX).arg(x).arg(m_resolution));
    }
}

void HicDataController::copyLeftPosition(double yFraction) const {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        const qint64 y = m_y0 + static_cast<qint64>((m_y1 - m_y0) * std::clamp(yFraction, 0.0, 1.0));
        clipboard->setText(QStringLiteral("%1:%2:%3").arg(m_chrY).arg(y).arg(m_resolution));
    }
}

void HicDataController::jumpToDiagonal(double xFraction, double yFraction) {
    if (m_chrX != m_chrY) {
        return;
    }
    pushViewHistory();
    const qint64 width = std::max<qint64>(m_resolution, m_x1 - m_x0);
    const qint64 height = std::max<qint64>(m_resolution, m_y1 - m_y0);
    const qint64 x = m_x0 + static_cast<qint64>(width * std::clamp(xFraction, 0.0, 1.0));
    const qint64 y = m_y0 + static_cast<qint64>(height * std::clamp(yFraction, 0.0, 1.0));
    const qint64 center = (x + y) / 2;
    m_x0 = center - width / 2;
    m_y0 = center - height / 2;
    m_x1 = m_x0 + width;
    m_y1 = m_y0 + height;
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::goTo(const QString& xLocation, const QString& yLocation) {
    auto parseNumber = [](QString value, bool* ok) -> qint64 {
        value = value.trimmed().remove(',');
        double multiplier = 1.0;
        if (value.endsWith('k', Qt::CaseInsensitive)) {
            multiplier = 1000.0;
            value.chop(1);
        } else if (value.endsWith('m', Qt::CaseInsensitive)) {
            multiplier = 1000000.0;
            value.chop(1);
        } else if (value.endsWith('g', Qt::CaseInsensitive)) {
            multiplier = 1000000000.0;
            value.chop(1);
        }
        const double parsed = value.toDouble(ok);
        return static_cast<qint64>(parsed * multiplier);
    };

    auto findGene = [this](const QString& gene, QString& chr, qint64& start, qint64& end) -> bool {
        for (const TrackLayer& track : m_tracks) {
            for (const TrackFeature& segment : track.features) {
                if (track.name.compare(gene, Qt::CaseInsensitive) == 0 ||
                    segment.label.compare(gene, Qt::CaseInsensitive) == 0) {
                    chr = segment.chr;
                    const chromosome resolved = chromosomeByName(chr);
                    if (!resolved.name.empty()) chr = QString::fromStdString(resolved.name);
                    const qint64 span = std::max<qint64>(m_resolution * 100LL, segment.end - segment.start);
                    const qint64 mid = (segment.start + segment.end) / 2;
                    start = std::max<qint64>(0, mid - span / 2);
                    end = std::min<qint64>(chromosomeLength(chr), start + span);
                    return true;
                }
            }
        }
        return false;
    };

    auto parseLocation = [&](const QString& text, QString& chr, qint64& start, qint64& end, int& requestedResolution) -> bool {
        const QString cleaned = text.trimmed().remove(',');
        const QStringList chrSplit = cleaned.split(':', Qt::SkipEmptyParts);
        if (chrSplit.isEmpty() || chrSplit[0].isEmpty()) {
            return false;
        }
        if (chrSplit.size() == 1 && findGene(chrSplit[0], chr, start, end)) {
            return true;
        }
        chr = chrSplit[0];
        const qint64 chrLength = chromosomeLength(chr);
        if (chrLength <= 0) {
            return false;
        }
        if (chrSplit.size() == 1) {
            start = 0;
            end = chrLength;
            return true;
        }
        const QStringList range = chrSplit[1].split('-', Qt::SkipEmptyParts);
        bool okStart = false;
        start = parseNumber(range.value(0), &okStart);
        if (!okStart) return false;
        if (range.size() > 1) {
            bool okEnd = false;
            end = parseNumber(range[1], &okEnd);
            if (!okEnd) return false;
        } else {
            const qint64 span = std::max<qint64>(m_resolution * 100LL, m_x1 - m_x0);
            start -= span / 2;
            end = start + span;
        }
        if (chrSplit.size() > 2) {
            bool okResolution = false;
            requestedResolution = static_cast<int>(parseNumber(chrSplit[2], &okResolution));
            if (!okResolution) {
                requestedResolution = 0;
            }
        }
        start = std::clamp<qint64>(start, 0, chrLength);
        end = std::clamp<qint64>(end, start + m_resolution, chrLength);
        return end > start;
    };

    QString nextChrX;
    QString nextChrY;
    qint64 nextX0 = 0, nextX1 = 0, nextY0 = 0, nextY1 = 0;
    int requestedResolution = 0;
    if (!parseLocation(xLocation, nextChrX, nextX0, nextX1, requestedResolution)) {
        setStatus(QStringLiteral("Could not parse top location."));
        return;
    }
    if (!parseLocation(yLocation.isEmpty() ? xLocation : yLocation, nextChrY, nextY0, nextY1, requestedResolution)) {
        setStatus(QStringLiteral("Could not parse left location."));
        return;
    }

    pushViewHistory();
    if (!m_xLocusLocked) m_chrX = nextChrX;
    if (!m_yLocusLocked) m_chrY = nextChrY;
    if (requestedResolution > 0 && !m_resolutionLocked) {
        m_resolution = requestedResolution;
    }
    if (!m_xLocusLocked) { m_x0 = nextX0; m_x1 = nextX1; }
    if (!m_yLocusLocked) { m_y0 = nextY0; m_y1 = nextY1; }
    if (requestedResolution <= 0 && !m_resolutionLocked) {
        adaptResolutionToSpan(std::max(m_x1 - m_x0, m_y1 - m_y0));
    }
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::saveCurrentLocation(const QString& name) {
    QVariantList locations = savedLocations();
    QVariantMap state = currentViewState(name.isEmpty() ? QDateTime::currentDateTime().toString(Qt::ISODate) : name);
    locations.push_front(state);
    while (locations.size() > 20) {
        locations.removeLast();
    }
    QSettings settings;
    settings.setValue(settingsKey() + QStringLiteral("/savedLocations"), locations);
    emit viewHistoryChanged();
}

void HicDataController::restoreSavedLocation(int index) {
    const QVariantList locations = savedLocations();
    if (index < 0 || index >= locations.size()) {
        return;
    }
    if (applyViewState(locations[index].toMap())) {
        scheduleRequest();
    }
}

void HicDataController::saveCurrentState(const QString& name) {
    QVariantList states = savedStates();
    QVariantMap state = currentViewState(name.isEmpty() ? QDateTime::currentDateTime().toString(Qt::ISODate) : name);
    state["filePath"] = m_filePath;
    state["controlFilePath"] = m_controlFilePath;
    state["matrixType"] = m_matrixType;
    state["norm"] = m_norm;
    state["colorMap"] = m_colorMap;
    state["colorMin"] = m_colorMin;
    state["colorMax"] = m_colorMax;
    states.push_front(state);
    while (states.size() > 20) {
        states.removeLast();
    }
    QSettings settings;
    settings.setValue(settingsKey() + QStringLiteral("/savedStates"), states);
    emit viewHistoryChanged();
}

void HicDataController::restoreSavedState(int index) {
    const QVariantList states = savedStates();
    if (index < 0 || index >= states.size()) {
        return;
    }
    const QVariantMap state = states[index].toMap();
    if (state.contains("controlFilePath")) {
        m_controlFilePath = state.value("controlFilePath").toString();
        emit controlFilePathChanged();
    }
    if (state.contains("matrixType")) m_matrixType = state.value("matrixType").toString();
    if (state.contains("norm")) m_norm = state.value("norm").toString();
    if (state.contains("colorMap")) m_colorMap = state.value("colorMap").toString();
    if (state.contains("colorMin")) m_colorMin = state.value("colorMin").toDouble();
    if (state.contains("colorMax")) m_colorMax = state.value("colorMax").toDouble();
    if (applyViewState(state)) {
        emit colorMapChanged();
        emit colorMaxChanged();
        scheduleRequest();
    }
}

void HicDataController::exportState(const QUrl& url) const {
    const QString path = localPathFromUrl(url);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QJsonDocument doc = QJsonDocument::fromVariant(currentViewState(QStringLiteral("Exported State")));
    file.write(doc.toJson(QJsonDocument::Indented));
    file.commit();
}

void HicDataController::importState(const QUrl& url) {
    QFile file(localPathFromUrl(url));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not import state."));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || !applyViewState(doc.object().toVariantMap())) {
        setStatus(QStringLiteral("Invalid CARTON state file."));
        return;
    }
    scheduleRequest();
}

void HicDataController::exportFigurePdf(const QUrl& url, int width, int height) const {
    const QString path = localPathFromUrl(url);
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSize(std::max(300, width), std::max(300, height)), QPageSize::Point));
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.fillRect(QRect(0, 0, width, height), Qt::white);
    painter.setPen(QPen(Qt::black, 2));
    painter.drawText(24, 32, QStringLiteral("CARTON Hi-C View"));
    painter.setPen(Qt::darkGray);
    painter.drawText(24, 56, QStringLiteral("%1 %2:%3-%4 by %5:%6-%7 %8 bp")
                         .arg(m_matrixType, m_chrX).arg(m_x0).arg(m_x1)
                         .arg(m_chrY).arg(m_y0).arg(m_y1).arg(m_resolution));
    const int side = std::min(width - 80, height - 110);
    const QRect plot(40, 80, side, side);
    painter.setPen(QPen(Qt::lightGray, 1));
    painter.drawRect(plot);
    const std::vector<contactRecord> snapshot = recordsSnapshot();
    const double spanX = std::max<qint64>(1, m_x1 - m_x0);
    const double spanY = std::max<qint64>(1, m_y1 - m_y0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#d7191c"));
    const int stride = std::max(1, static_cast<int>(snapshot.size() / 40000));
    for (std::size_t i = 0; i < snapshot.size(); i += stride) {
        const contactRecord& rec = snapshot[i];
        const int x = plot.left() + static_cast<int>((rec.binX - m_x0) / spanX * plot.width());
        const int y = plot.top() + static_cast<int>((rec.binY - m_y0) / spanY * plot.height());
        painter.drawRect(QRect(x, y, 1, 1));
    }
    painter.end();
}

bool HicDataController::exportFigurePng(const QUrl& url, int width, int height) const {
    width = std::clamp(width, 300, 12000);
    height = std::clamp(height, 300, 12000);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#0b0f14"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QColor("#e6edf3"));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    titleFont.setPointSize(13);
    painter.setFont(titleFont);
    painter.drawText(28, 30, QStringLiteral("CARTON · %1").arg(m_matrixType.toUpper()));
    QFont detailFont = painter.font();
    detailFont.setBold(false);
    detailFont.setPointSize(9);
    painter.setFont(detailFont);
    painter.setPen(QColor("#9aa7b5"));
    painter.drawText(28, 50, QStringLiteral("%1:%2–%3  ×  %4:%5–%6  ·  %7 bp  ·  %8")
                         .arg(m_chrX).arg(m_x0).arg(m_x1).arg(m_chrY).arg(m_y0).arg(m_y1)
                         .arg(m_resolution).arg(m_norm));

    const int legendHeight = 34;
    const QRect plot(64, 72, std::max(1, width - 96), std::max(1, height - 112 - legendHeight));
    painter.fillRect(plot, QColor("#ffffff"));
    const double spanX = std::max<qint64>(1, m_x1 - m_x0);
    const double spanY = std::max<qint64>(1, m_y1 - m_y0);
    const bool divergent = matrixIsDivergent(m_matrixType);
    auto valueColor = [&](double value) {
        if (!std::isfinite(value)) return m_missingValueColor;
        if (value == 0.0 && m_zeroTransparent) return QColor(255, 255, 255, 0);
        if (divergent || m_symmetricColorScale) {
            const double range = std::max(0.000001, std::max(std::abs(m_colorMin), std::abs(m_colorMax)));
            const double t = std::clamp(value / range, -1.0, 1.0);
            return t < 0.0
                ? QColor::fromRgbF(1.0 + t, 1.0 + t, 1.0)
                : QColor::fromRgbF(1.0, 1.0 - t, 1.0 - t);
        }
        const double t = std::clamp((value - m_colorMin) / std::max(0.000001, m_colorMax - m_colorMin), 0.0, 1.0);
        if (m_colorMap == QStringLiteral("Grayscale")) return QColor::fromRgbF(1.0 - t * 0.93, 1.0 - t * 0.93, 1.0 - t * 0.93);
        return QColor::fromRgbF(1.0, 1.0 - t * 0.91, 1.0 - t * 0.89);
    };
    const std::vector<contactRecord> snapshot = recordsSnapshot();
    const std::vector<contactRecord> controlSnapshot = controlRecordsSnapshot();
    const bool splitVs = m_chrX == m_chrY && !controlSnapshot.empty() &&
                         (m_matrixType == QStringLiteral("vs") || m_matrixType.endsWith(QStringLiteral("vs")));
    const int stride = std::max(1, static_cast<int>((snapshot.size() + controlSnapshot.size()) / 400000));
    const int cellW = std::max(1, static_cast<int>(std::ceil(m_resolution / spanX * plot.width())));
    const int cellH = std::max(1, static_cast<int>(std::ceil(m_resolution / spanY * plot.height())));
    const bool mirror = m_chrX == m_chrY && !splitVs;
    painter.setPen(Qt::NoPen);
    auto drawRecords = [&](const std::vector<contactRecord>& records, bool control) {
      for (std::size_t i = 0; i < records.size(); i += static_cast<std::size_t>(stride)) {
        const contactRecord& rec = records[i];
        if (rec.binX < m_x0 || rec.binX >= m_x1 || rec.binY < m_y0 || rec.binY >= m_y1) continue;
        painter.setBrush(valueColor(rec.counts));
        const int drawBinX = splitVs ? (control ? std::min(rec.binX, rec.binY) : std::max(rec.binX, rec.binY)) : rec.binX;
        const int drawBinY = splitVs ? (control ? std::max(rec.binX, rec.binY) : std::min(rec.binX, rec.binY)) : rec.binY;
        const int x = plot.left() + static_cast<int>((drawBinX - m_x0) / spanX * plot.width());
        const int y = plot.top() + static_cast<int>((drawBinY - m_y0) / spanY * plot.height());
        painter.drawRect(x, y, cellW, cellH);
        if (mirror && rec.binX != rec.binY) {
            const int mx = plot.left() + static_cast<int>((rec.binY - m_x0) / spanX * plot.width());
            const int my = plot.top() + static_cast<int>((rec.binX - m_y0) / spanY * plot.height());
            painter.drawRect(mx, my, cellW, cellH);
        }
      }
    };
    drawRecords(snapshot, false);
    drawRecords(controlSnapshot, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor("#52606d"));
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
    const QRect legend(plot.left(), plot.bottom() + 18, std::min(280, plot.width()), 10);
    for (int x = 0; x < legend.width(); ++x) {
        const double t = x / static_cast<double>(std::max(1, legend.width() - 1));
        const double value = m_colorMin + t * (m_colorMax - m_colorMin);
        painter.setPen(valueColor(value));
        painter.drawLine(legend.left() + x, legend.top(), legend.left() + x, legend.bottom());
    }
    painter.setPen(QColor("#c5d0dc"));
    painter.drawText(legend.left(), legend.bottom() + 14, QString::number(m_colorMin, 'g', 4));
    painter.drawText(legend.right() - 48, legend.bottom() + 14, QString::number(m_colorMax, 'g', 4));
    painter.end();
    return image.save(localPathFromUrl(url), "PNG");
}

bool HicDataController::exportVisibleMatrix(const QUrl& url) const {
    QSaveFile file(localPathFromUrl(url));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << "chrX\tstartX\tendX\tchrY\tstartY\tendY\tvalue\tmatrixType\tnormalization\tresolution\n";
    const std::vector<contactRecord> snapshot = recordsSnapshot();
    for (const contactRecord& rec : snapshot) {
        if (rec.binX < m_x0 || rec.binX >= m_x1 || rec.binY < m_y0 || rec.binY >= m_y1) continue;
        out << m_chrX << '\t' << rec.binX << '\t' << rec.binX + m_resolution << '\t'
            << m_chrY << '\t' << rec.binY << '\t' << rec.binY + m_resolution << '\t'
            << QString::number(rec.counts, 'g', 9) << '\t' << m_matrixType << '\t' << m_norm << '\t' << m_resolution << '\n';
    }
    return file.commit();
}

QVariantList HicDataController::colorHistogram(int bins) const {
    bins = std::clamp(bins, 8, 128);
    QVector<quint64> counts(bins, 0);
    const double low = m_colorMin;
    const double span = std::max(0.000001, m_colorMax - m_colorMin);
    quint64 maximum = 1;
    const std::vector<contactRecord> snapshot = recordsSnapshot();
    for (const contactRecord& rec : snapshot) {
        if (!std::isfinite(rec.counts) || rec.binX < m_x0 || rec.binX >= m_x1 || rec.binY < m_y0 || rec.binY >= m_y1) continue;
        const int index = std::clamp(static_cast<int>((rec.counts - low) / span * bins), 0, bins - 1);
        maximum = std::max(maximum, ++counts[index]);
    }
    QVariantList result;
    for (int i = 0; i < bins; ++i) {
        QVariantMap item;
        item["value"] = low + (i + 0.5) / bins * span;
        item["count"] = static_cast<qulonglong>(counts[i]);
        item["fraction"] = counts[i] / static_cast<double>(maximum);
        result.push_back(item);
    }
    return result;
}

void HicDataController::removeSavedLocation(int index) {
    QVariantList locations = savedLocations();
    if (index < 0 || index >= locations.size()) return;
    locations.removeAt(index);
    QSettings settings;
    settings.setValue(settingsKey() + QStringLiteral("/savedLocations"), locations);
    emit viewHistoryChanged();
}

void HicDataController::renameGenome(const QString& genomeId) {
    if (genomeId.trimmed().isEmpty()) {
        return;
    }
    m_genomeId = genomeId.trimmed();
    emit metadataChanged();
}

void HicDataController::setWholeGenomeView() {
    if (m_metadata.chromosomes.empty()) {
        return;
    }
    pushViewHistory();
    m_chrX = QStringLiteral("All");
    m_chrY = QStringLiteral("All");
    m_x0 = 0;
    m_y0 = 0;
    const qint64 length = genomeLength();
    m_x1 = length;
    m_y1 = length;
    adaptResolutionToSpan(length);
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::undoView() {
    if (m_undoStack.isEmpty()) {
        return;
    }
    QVariantMap current;
    current = currentViewState(QStringLiteral("redo"));
    m_redoStack.push_back(current);
    restoreView(m_undoStack.takeLast());
}

void HicDataController::redoView() {
    if (m_redoStack.isEmpty()) {
        return;
    }
    QVariantMap current;
    current = currentViewState(QStringLiteral("undo"));
    m_undoStack.push_back(current);
    restoreView(m_redoStack.takeLast());
}

void HicDataController::beginInteraction() {
    if (m_interactionActive) {
        return;
    }
    pushViewHistory();
    m_interactionActive = true;
}

void HicDataController::endInteraction() {
    m_interactionActive = false;
}

void HicDataController::zoomToFractions(double xStartFraction, double yStartFraction,
                                        double xEndFraction, double yEndFraction) {
    if (m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }
    const double xa = std::clamp(std::min(xStartFraction, xEndFraction), 0.0, 1.0);
    const double xb = std::clamp(std::max(xStartFraction, xEndFraction), 0.0, 1.0);
    const double ya = std::clamp(std::min(yStartFraction, yEndFraction), 0.0, 1.0);
    const double yb = std::clamp(std::max(yStartFraction, yEndFraction), 0.0, 1.0);
    if (xb - xa < 0.01 || yb - ya < 0.01) {
        return;
    }

    pushViewHistory();
    const qint64 width = std::max<qint64>(1, m_x1 - m_x0);
    const qint64 height = std::max<qint64>(1, m_y1 - m_y0);
    const qint64 nextX0 = m_x0 + static_cast<qint64>(width * xa);
    const qint64 nextX1 = m_x0 + static_cast<qint64>(width * xb);
    const qint64 nextY0 = m_y0 + static_cast<qint64>(height * ya);
    const qint64 nextY1 = m_y0 + static_cast<qint64>(height * yb);
    if (!m_xLocusLocked) { m_x0 = nextX0; m_x1 = nextX1; }
    if (!m_yLocusLocked) { m_y0 = nextY0; m_y1 = nextY1; }
    adaptResolutionToSpan(std::max(m_x1 - m_x0, m_y1 - m_y0));
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::requestVisibleRegion() {
    if (m_filePath.isEmpty() || m_chrX.isEmpty() || m_chrY.isEmpty() || m_resolution <= 0) {
        return;
    }
    clampRegion();
    scheduleRequest();
}

void HicDataController::zoom(double factor, double centerX, double centerY) {
    if (!std::isfinite(factor) || factor <= 0.0 || m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }

    centerX = std::clamp(centerX, 0.0, 1.0);
    centerY = std::clamp(centerY, 0.0, 1.0);
    pushViewHistory();
    const qint64 width = std::max<qint64>(m_resolution, m_x1 - m_x0);
    const qint64 height = std::max<qint64>(m_resolution, m_y1 - m_y0);
    const qint64 minSpan = minimumZoomSpan();
    const qint64 maxWidth = std::max<qint64>(m_resolution, chromosomeLength(m_chrX));
    const qint64 maxHeight = std::max<qint64>(m_resolution, chromosomeLength(m_chrY));
    qint64 nextWidth = width;
    qint64 nextHeight = height;

    if (!m_xLocusLocked && !m_yLocusLocked) {
        // Apply one scale to both genomic spans. Clamping each axis separately
        // makes rectangular viewports lose their aspect ratio as soon as the
        // shorter axis reaches its minimum span or a chromosome boundary.
        const double minScaleX = static_cast<double>(std::min(minSpan, maxWidth)) / width;
        const double minScaleY = static_cast<double>(std::min(minSpan, maxHeight)) / height;
        const double maxScaleX = static_cast<double>(maxWidth) / width;
        const double maxScaleY = static_cast<double>(maxHeight) / height;
        const double lowerScale = std::max(minScaleX, minScaleY);
        const double upperScale = std::min(maxScaleX, maxScaleY);
        const double scale = std::clamp(1.0 / factor, lowerScale, upperScale);
        nextWidth = std::clamp<qint64>(qRound64(width * scale),
                                       std::min(minSpan, maxWidth), maxWidth);
        nextHeight = std::clamp<qint64>(qRound64(height * scale),
                                        std::min(minSpan, maxHeight), maxHeight);
    } else {
        if (!m_xLocusLocked) {
            nextWidth = std::clamp<qint64>(qRound64(width / factor),
                                           std::min(minSpan, maxWidth), maxWidth);
        }
        if (!m_yLocusLocked) {
            nextHeight = std::clamp<qint64>(qRound64(height / factor),
                                            std::min(minSpan, maxHeight), maxHeight);
        }
    }
    const qint64 centerGenomeX = m_x0 + static_cast<qint64>(width * centerX);
    const qint64 centerGenomeY = m_y0 + static_cast<qint64>(height * centerY);

    if (!m_xLocusLocked) { m_x0 = centerGenomeX - nextWidth / 2; m_x1 = m_x0 + nextWidth; }
    if (!m_yLocusLocked) { m_y0 = centerGenomeY - nextHeight / 2; m_y1 = m_y0 + nextHeight; }
    adaptResolutionToSpan(std::max(nextWidth, nextHeight));
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::pan(double dxFraction, double dyFraction) {
    if (!m_interactionActive) {
        pushViewHistory();
    }
    const qint64 width = m_x1 - m_x0;
    const qint64 height = m_y1 - m_y0;
    const qint64 dx = static_cast<qint64>(width * dxFraction);
    const qint64 dy = static_cast<qint64>(height * dyFraction);
    if (!m_xLocusLocked) { m_x0 += dx; m_x1 += dx; }
    if (!m_yLocusLocked) { m_y0 += dy; m_y1 += dy; }
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::fitViewToAspectRatio(double aspectRatio) {
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0) {
        return;
    }
    m_viewportAspectRatio = std::clamp(aspectRatio, 0.2, 5.0);
    if (m_chrX.isEmpty() || m_chrY.isEmpty() || !applyViewportAspectRatio()) {
        return;
    }
    adaptResolutionToSpan(std::max(m_x1 - m_x0, m_y1 - m_y0));
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    scheduleRequest();
}

bool HicDataController::applyViewportAspectRatio() {
    const double aspectRatio = m_viewportAspectRatio;
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0 || m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return false;
    }
    const qint64 maxX = std::max<qint64>(m_resolution, chromosomeLength(m_chrX));
    const qint64 maxY = std::max<qint64>(m_resolution, chromosomeLength(m_chrY));
    const qint64 currentXSpan = std::max<qint64>(m_resolution, m_x1 - m_x0);
    const qint64 currentYSpan = std::max<qint64>(m_resolution, m_y1 - m_y0);
    qint64 targetXSpan = currentXSpan;
    qint64 targetYSpan = currentYSpan;

    if (!m_xLocusLocked) {
        targetXSpan = std::max<qint64>(m_resolution, qRound64(currentYSpan * aspectRatio));
        if (targetXSpan > maxX) {
            targetXSpan = maxX;
            if (!m_yLocusLocked) {
                targetYSpan = std::max<qint64>(m_resolution, qRound64(targetXSpan / aspectRatio));
            }
        }
    } else if (!m_yLocusLocked) {
        targetYSpan = std::max<qint64>(m_resolution, qRound64(currentXSpan / aspectRatio));
    }
    if (targetYSpan > maxY) {
        targetYSpan = maxY;
        if (!m_xLocusLocked) {
            targetXSpan = std::max<qint64>(m_resolution, qRound64(targetYSpan * aspectRatio));
        }
    }

    targetXSpan = std::min(targetXSpan, maxX);
    targetYSpan = std::min(targetYSpan, maxY);
    if (targetXSpan == currentXSpan && targetYSpan == currentYSpan) return false;

    const qint64 centerX = m_x0 + currentXSpan / 2;
    const qint64 centerY = m_y0 + currentYSpan / 2;
    if (!m_xLocusLocked) {
        m_x0 = centerX - targetXSpan / 2;
        m_x1 = m_x0 + targetXSpan;
    }
    if (!m_yLocusLocked) {
        m_y0 = centerY - targetYSpan / 2;
        m_y1 = m_y0 + targetYSpan;
    }
    clampRegion();
    return true;
}

void HicDataController::resetView() {
    if (m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }
    pushViewHistory();
    m_x0 = 0;
    m_y0 = 0;
    m_x1 = chromosomeLength(m_chrX);
    m_y1 = chromosomeLength(m_chrY);
    adaptResolutionToSpan(std::max(m_x1 - m_x0, m_y1 - m_y0));
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::syncViewFrom(HicDataController* other, bool includeColor) {
    if (!other || other == this) return;
    const bool sameView = m_chrX == other->m_chrX && m_chrY == other->m_chrY &&
                          m_x0 == other->m_x0 && m_x1 == other->m_x1 &&
                          m_y0 == other->m_y0 && m_y1 == other->m_y1 &&
                          m_resolution == other->m_resolution;
    const bool sameColor = !includeColor || (m_colorMin == other->m_colorMin && m_colorMax == other->m_colorMax &&
                                             m_colorMap == other->m_colorMap);
    if (sameView && sameColor) return;
    m_chrX = other->m_chrX;
    m_chrY = other->m_chrY;
    m_x0 = other->m_x0;
    m_x1 = other->m_x1;
    m_y0 = other->m_y0;
    m_y1 = other->m_y1;
    if (!m_resolutionLocked) m_resolution = other->m_resolution;
    if (includeColor) {
        m_colorMin = other->m_colorMin;
        m_colorMax = other->m_colorMax;
        m_colorMap = other->m_colorMap;
        m_colorMaxAuto = other->m_colorMaxAuto;
        emit colorMaxChanged();
        emit colorMapChanged();
    }
    clearLoadedRegion();
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setChrX(const QString& value) {
    if (m_chrX == value) return;
    m_chrX = value;
    if (m_chrY.isEmpty()) m_chrY = value;
    resetView();
    emit viewChanged();
}

void HicDataController::setChrY(const QString& value) {
    if (m_chrY == value) return;
    m_chrY = value;
    resetView();
    emit viewChanged();
}

void HicDataController::setMatrixType(const QString& value) {
    if (m_matrixType == value) return;
    if (!validateMatrixMode(value)) {
        emit viewChanged();
        return;
    }
    m_matrixType = value;
    clearLoadedRegion();
    m_colorMaxAuto = true;
    if (matrixIsDivergent(m_matrixType)) {
        m_colorMin = matrixIsPearson(m_matrixType) ? -1.0 : -5.0;
        m_colorMax = matrixIsPearson(m_matrixType) ? 1.0 : 5.0;
    } else {
        m_colorMin = 0.0;
        m_colorMax = 50.0;
    }
    emit colorMaxChanged();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setNorm(const QString& value) {
    if (m_norm == value) return;
    m_norm = value;
    clearLoadedRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setResolution(int value) {
    if (m_resolution == value || value <= 0) return;
    if (m_resolutionLocked) {
        setStatus(QStringLiteral("Resolution is locked."));
        return;
    }
    m_resolution = value;
    clearLoadedRegion();
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setX0(qint64 value) { if (m_x0 != value) { m_x0 = value; emit viewChanged(); } }
void HicDataController::setX1(qint64 value) { if (m_x1 != value) { m_x1 = value; emit viewChanged(); } }
void HicDataController::setY0(qint64 value) { if (m_y0 != value) { m_y0 = value; emit viewChanged(); } }
void HicDataController::setY1(qint64 value) { if (m_y1 != value) { m_y1 = value; emit viewChanged(); } }

void HicDataController::setColorMax(double value) {
    if (!std::isfinite(value) || qFuzzyCompare(m_colorMax, value)) return;
    m_colorMaxAuto = false;
    m_colorMax = value;
    if (m_colorMax <= m_colorMin) {
        m_colorMin = m_colorMax - std::max(1.0, std::abs(m_colorMax) * 0.1);
    }
    emit colorMaxChanged();
}

void HicDataController::setColorMin(double value) {
    if (!std::isfinite(value) || qFuzzyCompare(m_colorMin, value)) return;
    m_colorMaxAuto = false;
    m_colorMin = value;
    if (m_colorMin >= m_colorMax) {
        m_colorMax = m_colorMin + std::max(1.0, std::abs(m_colorMin) * 0.1);
    }
    emit colorMaxChanged();
}

void HicDataController::resetColorScale() {
    m_colorMaxAuto = true;
    if (matrixIsDivergent(m_matrixType)) {
        m_colorMin = matrixIsPearson(m_matrixType) ? -1.0 : -5.0;
        m_colorMax = matrixIsPearson(m_matrixType) ? 1.0 : 5.0;
    } else {
        m_colorMin = 0.0;
        m_colorMax = 50.0;
    }
    emit colorMaxChanged();
    scheduleRequest();
}

void HicDataController::setColorMap(const QString& value) {
    if (m_colorMap == value) return;
    m_colorMap = value;
    emit colorMapChanged();
}

void HicDataController::setCustomLowColor(const QColor& value) {
    if (m_customLowColor == value || !value.isValid()) return;
    m_customLowColor = value;
    emit colorMapChanged();
}

void HicDataController::setCustomHighColor(const QColor& value) {
    if (m_customHighColor == value || !value.isValid()) return;
    m_customHighColor = value;
    emit colorMapChanged();
}

void HicDataController::setResolutionLocked(bool value) {
    if (m_resolutionLocked == value) return;
    m_resolutionLocked = value;
    emit viewChanged();
}

void HicDataController::setXLocusLocked(bool value) {
    if (m_xLocusLocked == value) return;
    m_xLocusLocked = value;
    emit viewChanged();
}

void HicDataController::setYLocusLocked(bool value) {
    if (m_yLocusLocked == value) return;
    m_yLocusLocked = value;
    emit viewChanged();
}

void HicDataController::setAxisEndpointsOnly(bool value) {
    if (m_axisEndpointsOnly == value) return;
    m_axisEndpointsOnly = value;
    emit displayOptionsChanged();
}

void HicDataController::setShowGridlines(bool value) {
    if (m_showGridlines == value) return;
    m_showGridlines = value;
    emit displayOptionsChanged();
}

void HicDataController::setShowChromosomeContext(bool value) {
    if (m_showChromosomeContext == value) return;
    m_showChromosomeContext = value;
    emit displayOptionsChanged();
}

void HicDataController::setDarkMode(bool value) {
    if (m_darkMode == value) return;
    m_darkMode = value;
    emit displayOptionsChanged();
}

void HicDataController::setShowTilesDebug(bool value) {
    if (m_showTilesDebug == value) return;
    m_showTilesDebug = value;
    emit displayOptionsChanged();
}

void HicDataController::setSparseFeatureLimit(int value) {
    const int clamped = std::max(1, value);
    if (m_sparseFeatureLimit == clamped) return;
    m_sparseFeatureLimit = clamped;
    emit annotationsChanged();
}

void HicDataController::setCacheLimitMB(int value) {
    value = std::clamp(value, 16, 4096);
    if (m_cacheLimitMB == value) return;
    m_cacheLimitMB = value;
    const std::size_t maxRecords = static_cast<std::size_t>(value) * 1024ULL * 1024ULL / sizeof(contactRecord);
    m_cache->setLimits(maxRecords, std::max<std::size_t>(24, static_cast<std::size_t>(value / 4)));
    QSettings settings;
    settings.setValue(settingsKey() + QStringLiteral("/cacheLimitMB"), value);
    emit cacheStatsChanged();
}

void HicDataController::setSymmetricColorScale(bool value) {
    if (m_symmetricColorScale == value) return;
    m_symmetricColorScale = value;
    if (value) {
        const double extent = std::max(std::abs(m_colorMin), std::abs(m_colorMax));
        m_colorMin = -extent;
        m_colorMax = extent;
        m_colorMaxAuto = false;
    }
    emit colorMaxChanged();
}

void HicDataController::setColorPercentile(double value) {
    value = std::clamp(value, 50.0, 100.0);
    if (qFuzzyCompare(m_colorPercentile, value)) return;
    m_colorPercentile = value;
    m_colorMaxAuto = true;
    updateAutoColorScale(recordsSnapshot(), controlRecordsSnapshot());
}

void HicDataController::setMissingValueColor(const QColor& value) {
    if (!value.isValid() || value == m_missingValueColor) return;
    m_missingValueColor = value;
    emit colorMapChanged();
}

void HicDataController::setZeroTransparent(bool value) {
    if (m_zeroTransparent == value) return;
    m_zeroTransparent = value;
    emit colorMapChanged();
}

void HicDataController::setWorkspaceSearch(const QString& value) {
    if (m_workspaceSearch == value) return;
    m_workspaceSearch = value;
    refreshSearchResultsModel();
    emit workspaceSearchChanged();
}

void HicDataController::setTrackName(int index, const QString& name) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks[static_cast<std::size_t>(index)].name = name;
    emit tracksChanged();
}

void HicDataController::setTrackColor(int index, const QColor& positiveColor, const QColor& negativeColor) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    TrackLayer& track = m_tracks[static_cast<std::size_t>(index)];
    if (positiveColor.isValid()) {
        track.color = positiveColor;
        for (TrackFeature& feature : track.features) feature.color = positiveColor;
    }
    if (negativeColor.isValid()) track.negativeColor = negativeColor;
    emit tracksChanged();
}

void HicDataController::setTrackRange(int index, double minValue, double maxValue, bool logScale) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) return;
    TrackLayer& track = m_tracks[static_cast<std::size_t>(index)];
    track.minValue = minValue;
    track.maxValue = maxValue;
    track.logScale = logScale;
    emit tracksChanged();
}

void HicDataController::setTrackReduction(int index, const QString& reduction) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    static const QStringList supported = {QStringLiteral("min"), QStringLiteral("mean"),
                                          QStringLiteral("max"), QStringLiteral("none")};
    m_tracks[static_cast<std::size_t>(index)].reduction = supported.contains(reduction) ? reduction : QStringLiteral("mean");
    emit tracksChanged();
}

void HicDataController::setTrackBinSize(int index, qint64 binSize) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks[static_cast<std::size_t>(index)].binSize = std::clamp<qint64>(binSize, 0, 1000000000LL);
    emit tracksChanged();
}

void HicDataController::setTrackVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks[static_cast<std::size_t>(index)].visible = visible;
    emit tracksChanged();
}

void HicDataController::setTrackCollapsed(int index, bool collapsed) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks[static_cast<std::size_t>(index)].collapsed = collapsed;
    emit tracksChanged();
}

void HicDataController::setTrackHeight(int index, int height) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks[static_cast<std::size_t>(index)].height = std::clamp(height, 20, 240);
    emit tracksChanged();
}

void HicDataController::setTrackAutoscale(int index, bool autoscale) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    TrackLayer& track = m_tracks[static_cast<std::size_t>(index)];
    track.autoscale = autoscale;
    if (autoscale && !track.features.isEmpty()) {
        double low = 0.0;
        double high = 0.0;
        for (const TrackFeature& feature : track.features) {
            low = std::min(low, feature.value);
            high = std::max(high, feature.value);
        }
        track.minValue = low;
        track.maxValue = high > low ? high : low + 1.0;
    }
    emit tracksChanged();
}

void HicDataController::moveTrack(int from, int to) {
    if (from < 0 || from >= static_cast<int>(m_tracks.size()) || to < 0 || to >= static_cast<int>(m_tracks.size()) || from == to) return;
    auto item = m_tracks[static_cast<std::size_t>(from)];
    m_tracks.erase(m_tracks.begin() + from);
    m_tracks.insert(m_tracks.begin() + to, item);
    emit tracksChanged();
}

void HicDataController::removeTrack(int index) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks.erase(m_tracks.begin() + index);
    emit tracksChanged();
}

void HicDataController::addAnnotationLayer(const QString& name) {
    AnnotationLayer layer;
    layer.name = name.trimmed().isEmpty() ? QStringLiteral("Layer %1").arg(m_annotationLayers.size() + 1) : name.trimmed();
    m_annotationLayers.push_back(layer);
    m_activeAnnotationLayer = m_annotationLayers.size() - 1;
    emit annotationsChanged();
}

void HicDataController::duplicateAnnotationLayer(int index) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    AnnotationLayer copy = m_annotationLayers[index];
    copy.name += QStringLiteral(" copy");
    m_annotationLayers.push_back(copy);
    emit annotationsChanged();
}

void HicDataController::mergeVisibleAnnotationLayers(const QString& name) {
    AnnotationLayer merged;
    merged.name = name.trimmed().isEmpty() ? QStringLiteral("Merged") : name.trimmed();
    for (const AnnotationLayer& layer : m_annotationLayers) {
        if (layer.visible) {
            merged.annotations += layer.annotations;
        }
    }
    m_annotationLayers.push_back(merged);
    m_activeAnnotationLayer = m_annotationLayers.size() - 1;
    emit annotationsChanged();
}

void HicDataController::removeAnnotationLayer(int index) {
    if (index < 0 || index >= m_annotationLayers.size() || m_annotationLayers.size() <= 1) return;
    m_annotationLayers.removeAt(index);
    m_activeAnnotationLayer = std::clamp(m_activeAnnotationLayer, 0, static_cast<int>(m_annotationLayers.size()) - 1);
    emit annotationsChanged();
}

void HicDataController::clearAnnotationLayer(int index) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    AnnotationLayer& layer = m_annotationLayers[index];
    layer.undoStack = layer.annotations;
    layer.annotations.clear();
    emit annotationsChanged();
}

void HicDataController::moveAnnotationLayer(int from, int to) {
    if (from < 0 || from >= m_annotationLayers.size() || to < 0 || to >= m_annotationLayers.size() || from == to) return;
    m_annotationLayers.move(from, to);
    m_activeAnnotationLayer = to;
    emit annotationsChanged();
}

void HicDataController::setAnnotationLayerVisible(int index, bool visible) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    m_annotationLayers[index].visible = visible;
    emit annotationsChanged();
}

void HicDataController::setAnnotationLayerTransparent(int index, bool transparent) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    m_annotationLayers[index].transparent = transparent;
    emit annotationsChanged();
}

void HicDataController::setAnnotationLayerSparse(int index, bool sparse) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    m_annotationLayers[index].sparse = sparse;
    emit annotationsChanged();
}

void HicDataController::setAnnotationLayerEnlarged(int index, bool enlarged) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    m_annotationLayers[index].enlarged = enlarged;
    emit annotationsChanged();
}

void HicDataController::setAnnotationLayerColor(int index, const QColor& color) {
    if (index < 0 || index >= m_annotationLayers.size() || !color.isValid()) return;
    AnnotationLayer& layer = m_annotationLayers[index];
    layer.color = color;
    for (Annotation2D& annotation : layer.annotations) {
        annotation.color = color;
    }
    emit annotationsChanged();
}

void HicDataController::setActiveAnnotationLayer(int index) {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    m_activeAnnotationLayer = index;
    emit annotationsChanged();
}

void HicDataController::exportAnnotationLayer(int index, const QUrl& url) const {
    if (index < 0 || index >= m_annotationLayers.size()) return;
    QSaveFile file(localPathFromUrl(url));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    const AnnotationLayer& layer = m_annotationLayers[index];
    for (const Annotation2D& annotation : layer.annotations) {
        out << annotation.chr1 << '\t' << annotation.start1 << '\t' << annotation.end1 << '\t'
            << annotation.chr2 << '\t' << annotation.start2 << '\t' << annotation.end2 << '\t'
            << annotation.name << "\t0\t" << annotation.color.name() << '\n';
    }
    file.commit();
}

void HicDataController::addAnnotationFromFractions(double xStartFraction, double yStartFraction,
                                                   double xEndFraction, double yEndFraction) {
    if (m_annotationLayers.isEmpty()) addAnnotationLayer(QStringLiteral("Selection"));
    AnnotationLayer& layer = m_annotationLayers[m_activeAnnotationLayer];
    layer.undoStack = layer.annotations;
    const double xa = std::clamp(std::min(xStartFraction, xEndFraction), 0.0, 1.0);
    const double xb = std::clamp(std::max(xStartFraction, xEndFraction), 0.0, 1.0);
    const double ya = std::clamp(std::min(yStartFraction, yEndFraction), 0.0, 1.0);
    const double yb = std::clamp(std::max(yStartFraction, yEndFraction), 0.0, 1.0);
    Annotation2D annotation;
    annotation.id = QStringLiteral("hand_%1").arg(QDateTime::currentMSecsSinceEpoch());
    annotation.name = QStringLiteral("Hand annotation");
    annotation.chr1 = m_chrX;
    annotation.chr2 = m_chrY;
    annotation.start1 = m_x0 + static_cast<qint64>((m_x1 - m_x0) * xa);
    annotation.end1 = m_x0 + static_cast<qint64>((m_x1 - m_x0) * xb);
    annotation.start2 = m_y0 + static_cast<qint64>((m_y1 - m_y0) * ya);
    annotation.end2 = m_y0 + static_cast<qint64>((m_y1 - m_y0) * yb);
    annotation.color = layer.color;
    if (annotation.end1 > annotation.start1 && annotation.end2 > annotation.start2) {
        layer.annotations.push_back(annotation);
        m_selectedAnnotationId = annotation.id;
        emit annotationsChanged();
    }
}

void HicDataController::selectAnnotationAt(double xFraction, double yFraction) {
    const qint64 x = m_x0 + static_cast<qint64>((m_x1 - m_x0) * std::clamp(xFraction, 0.0, 1.0));
    const qint64 y = m_y0 + static_cast<qint64>((m_y1 - m_y0) * std::clamp(yFraction, 0.0, 1.0));
    m_selectedAnnotationId.clear();
    for (const AnnotationLayer& layer : m_annotationLayers) {
        if (!layer.visible) continue;
        for (const Annotation2D& annotation : layer.annotations) {
            if (chrNamesEqual(annotation.chr1, m_chrX) && chrNamesEqual(annotation.chr2, m_chrY) &&
                x >= annotation.start1 && x <= annotation.end1 && y >= annotation.start2 && y <= annotation.end2) {
                m_selectedAnnotationId = annotation.id;
                emit annotationsChanged();
                return;
            }
        }
    }
    emit annotationsChanged();
}

void HicDataController::deleteSelectedAnnotation() {
    if (m_selectedAnnotationId.isEmpty()) return;
    for (AnnotationLayer& layer : m_annotationLayers) {
        for (int i = 0; i < layer.annotations.size(); ++i) {
            if (layer.annotations[i].id == m_selectedAnnotationId) {
                layer.undoStack = layer.annotations;
                layer.annotations.removeAt(i);
                m_selectedAnnotationId.clear();
                emit annotationsChanged();
                return;
            }
        }
    }
}

void HicDataController::setSelectedAnnotationColor(const QColor& color) {
    if (m_selectedAnnotationId.isEmpty() || !color.isValid()) return;
    for (AnnotationLayer& layer : m_annotationLayers) {
        for (Annotation2D& annotation : layer.annotations) {
            if (annotation.id == m_selectedAnnotationId) {
                annotation.color = color;
                emit annotationsChanged();
                return;
            }
        }
    }
}

void HicDataController::setSelectedAnnotationAttribute(const QString& key, const QString& value) {
    if (m_selectedAnnotationId.isEmpty() || key.trimmed().isEmpty()) return;
    for (AnnotationLayer& layer : m_annotationLayers) {
        for (Annotation2D& annotation : layer.annotations) {
            if (annotation.id == m_selectedAnnotationId) {
                annotation.attributes[key.trimmed()] = value;
                emit annotationsChanged();
                return;
            }
        }
    }
}

void HicDataController::toggleSelectedAnnotationHighlight() {
    if (m_selectedAnnotationId.isEmpty()) return;
    for (AnnotationLayer& layer : m_annotationLayers) {
        for (Annotation2D& annotation : layer.annotations) {
            if (annotation.id == m_selectedAnnotationId) {
                annotation.highlighted = !annotation.highlighted;
                emit annotationsChanged();
                return;
            }
        }
    }
}

std::vector<contactRecord> HicDataController::recordsSnapshot() const {
    QMutexLocker locker(&m_mutex);
    return m_records;
}

std::vector<contactRecord> HicDataController::controlRecordsSnapshot() const {
    QMutexLocker locker(&m_mutex);
    return m_controlRecords;
}

void HicDataController::renderRecordsSnapshot(std::vector<contactRecord>& records,
                                              std::vector<contactRecord>& controlRecords,
                                              int& dataResolution,
                                              int maxRecordsPerLayer) const {
    QMutexLocker locker(&m_mutex);
    const int sourceResolution = (!m_records.empty() || !m_controlRecords.empty()) && m_loadedKey.resolution > 0
        ? m_loadedKey.resolution
        : std::max(1, m_resolution);
    dataResolution = sourceResolution;
    const bool reflectIntra = m_chrX == m_chrY;
    const auto recordIsVisible = [this, sourceResolution, reflectIntra](const contactRecord& record) {
        const auto overlaps = [sourceResolution](qint64 bin, qint64 start, qint64 end) {
            return bin < end && bin + sourceResolution > start;
        };
        const bool direct = overlaps(record.binX, m_x0, m_x1) && overlaps(record.binY, m_y0, m_y1);
        const bool reflected = reflectIntra && overlaps(record.binY, m_x0, m_x1) && overlaps(record.binX, m_y0, m_y1);
        return direct || reflected;
    };

    auto filterVisible = [&recordIsVisible](const std::vector<contactRecord>& source) {
        std::vector<contactRecord> visible;
        visible.reserve(source.size());
        for (const contactRecord& record : source) {
            if (recordIsVisible(record)) visible.push_back(record);
        }
        return visible;
    };

    std::vector<contactRecord> visibleRecords = filterVisible(m_records);
    std::vector<contactRecord> visibleControlRecords = filterVisible(m_controlRecords);
    const std::size_t limit = static_cast<std::size_t>(std::max(1, maxRecordsPerLayer));
    const std::size_t largestLayer = std::max(visibleRecords.size(), visibleControlRecords.size());
    int aggregationFactor = largestLayer > limit
        ? std::max(2, static_cast<int>(std::ceil(std::sqrt(largestLayer / static_cast<double>(limit)))))
        : 1;

    auto aggregate = [sourceResolution](const std::vector<contactRecord>& source, int factor) {
        if (factor <= 1) return source;
        const qint64 bucketSize = static_cast<qint64>(sourceResolution) * factor;
        std::unordered_map<quint64, contactRecord> buckets;
        buckets.reserve(source.size() / static_cast<std::size_t>(factor) + 1);
        for (const contactRecord& record : source) {
            const qint64 bucketX = (static_cast<qint64>(record.binX) / bucketSize) * bucketSize;
            const qint64 bucketY = (static_cast<qint64>(record.binY) / bucketSize) * bucketSize;
            const quint64 key = (static_cast<quint64>(static_cast<quint32>(bucketX)) << 32U) |
                                static_cast<quint32>(bucketY);
            const auto found = buckets.find(key);
            if (found == buckets.end()) {
                contactRecord aggregated = record;
                aggregated.binX = static_cast<int32_t>(bucketX);
                aggregated.binY = static_cast<int32_t>(bucketY);
                buckets.emplace(key, aggregated);
            } else if (std::abs(record.counts) > std::abs(found->second.counts)) {
                found->second.counts = record.counts;
            }
        }
        std::vector<contactRecord> result;
        result.reserve(buckets.size());
        for (const auto& entry : buckets) result.push_back(entry.second);
        return result;
    };

    if (aggregationFactor == 1) {
        records = std::move(visibleRecords);
        controlRecords = std::move(visibleControlRecords);
        return;
    }

    while (true) {
        records = aggregate(visibleRecords, aggregationFactor);
        controlRecords = aggregate(visibleControlRecords, aggregationFactor);
        if ((records.size() <= limit && controlRecords.size() <= limit) || aggregationFactor >= 1024) break;
        aggregationFactor *= 2;
    }

    const qint64 aggregatedResolution = static_cast<qint64>(sourceResolution) * aggregationFactor;
    dataResolution = static_cast<int>(std::min<qint64>(aggregatedResolution, std::numeric_limits<int>::max()));
}

QString HicDataController::localPathFromUrl(const QUrl& url) {
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    if (url.scheme().startsWith(QStringLiteral("http"))) {
        return url.toString();
    }
    return url.toString();
}

QString HicDataController::settingsKey() {
    return QStringLiteral("carton");
}

HicTileKey HicDataController::makeKey(const QString& filePath, const QString& matrixType, const QString& norm,
                                      const QString& chrX, const QString& chrY, int resolution,
                                      qint64 x0, qint64 x1, qint64 y0, qint64 y1) {
    HicTileKey key;
    key.filePath = filePath.toStdString();
    key.matrixType = matrixType.toStdString();
    key.norm = norm.toStdString();
    key.unit = "BP";
    key.chrX = chrX.toStdString();
    key.chrY = chrY.toStdString();
    key.resolution = resolution;
    key.x0 = x0;
    key.x1 = x1;
    key.y0 = y0;
    key.y1 = y1;
    return key;
}

void HicDataController::setStatus(const QString& value) {
    if (m_status == value) return;
    m_status = value;
    emit statusChanged();
}

void HicDataController::setBusy(bool value) {
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}

void HicDataController::applyMetadata(const HicFileMetadata& metadata) {
    m_metadata = metadata;
    clearLoadedRegion();
    m_genomeId = QString::fromStdString(metadata.genomeID);
    if (!m_controlMetadata.chromosomes.empty()) {
        const bool wasReady = m_controlReady;
        m_controlReady = m_genomeId.isEmpty() || m_controlMetadata.genomeID.empty() ||
                         m_genomeId == QString::fromStdString(m_controlMetadata.genomeID);
        if (m_controlReady != wasReady) emit controlReadyChanged();
    }
    if (!metadata.bpResolutions.empty()) {
        m_resolution = metadata.bpResolutions.front();
    }
    m_norm = QStringLiteral("NONE");
    for (const chromosome& chr : metadata.chromosomes) {
        if (chr.index > 0) {
            m_chrX = QString::fromStdString(chr.name);
            m_chrY = m_chrX;
            break;
        }
    }
    resetView();
    emit metadataChanged();
    emit viewChanged();
}

chromosome HicDataController::chromosomeByName(const QString& name) const {
    const std::string needle = name.toStdString();
    for (const chromosome& chr : m_metadata.chromosomes) {
        if (chr.name == needle) {
            return chr;
        }
    }
    // Fall back to a chr-prefix-insensitive match so a chromosome typed or
    // looked up in a different naming convention than the .hic file's own
    // (e.g. "chr1" vs "1") still resolves.
    for (const chromosome& chr : m_metadata.chromosomes) {
        if (chrNamesEqual(QString::fromStdString(chr.name), name)) {
            return chr;
        }
    }
    return chromosome{};
}

qint64 HicDataController::chromosomeLength(const QString& name) const {
    if (isAllChromosome(name)) {
        return genomeLength();
    }
    return chromosomeByName(name).length;
}

qint64 HicDataController::genomeLength() const {
    qint64 length = 0;
    for (const chromosome& chr : m_metadata.chromosomes) {
        if (chr.index > 0) {
            length += chr.length;
        }
    }
    return std::max<qint64>(m_resolution, length);
}

bool HicDataController::isAllChromosome(const QString& name) const {
    return name.compare(QStringLiteral("All"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0;
}

bool HicDataController::matrixNeedsControl(const QString& matrixType) const {
    return matrixType.contains(QStringLiteral("control")) || matrixType.contains(QStringLiteral("ratio")) ||
           matrixType.contains(QStringLiteral("vs")) || matrixType == QStringLiteral("diff");
}

bool HicDataController::matrixNeedsPrimary(const QString& matrixType) const {
    return !(matrixType == QStringLiteral("control") || matrixType == QStringLiteral("logcontrol") ||
             matrixType == QStringLiteral("controloe") || matrixType == QStringLiteral("controlpearson"));
}

bool HicDataController::controlSupportsCurrentView(QString* reason) const {
    auto fail = [reason](const QString& message) {
        if (reason) *reason = message;
        return false;
    };
    if (!m_controlReady) {
        return fail(QStringLiteral("the control header has not finished loading"));
    }
    if (!m_genomeId.isEmpty() && !m_controlMetadata.genomeID.empty() &&
        m_genomeId != QString::fromStdString(m_controlMetadata.genomeID)) {
        return fail(QStringLiteral("genome %1 does not match %2")
                        .arg(QString::fromStdString(m_controlMetadata.genomeID), m_genomeId));
    }
    const auto hasChromosome = [this](const QString& name) {
        if (isAllChromosome(name)) return true;
        return std::any_of(m_controlMetadata.chromosomes.cbegin(), m_controlMetadata.chromosomes.cend(),
                           [&name](const chromosome& chr) { return chrNamesEqual(QString::fromStdString(chr.name), name); });
    };
    if (!m_chrX.isEmpty() && !hasChromosome(m_chrX)) {
        return fail(QStringLiteral("chromosome %1 is missing").arg(m_chrX));
    }
    if (!m_chrY.isEmpty() && !hasChromosome(m_chrY)) {
        return fail(QStringLiteral("chromosome %1 is missing").arg(m_chrY));
    }
    if (m_resolution > 0 &&
        std::find(m_controlMetadata.bpResolutions.cbegin(), m_controlMetadata.bpResolutions.cend(), m_resolution) ==
            m_controlMetadata.bpResolutions.cend()) {
        return fail(QStringLiteral("%1 bp bins are unavailable").arg(m_resolution));
    }
    if (m_norm != QStringLiteral("NONE") &&
        std::find(m_controlMetadata.normalizations.cbegin(), m_controlMetadata.normalizations.cend(), m_norm.toStdString()) ==
            m_controlMetadata.normalizations.cend()) {
        return fail(QStringLiteral("normalization %1 is unavailable").arg(m_norm));
    }
    return true;
}

bool HicDataController::matrixIsVs(const QString& matrixType) const {
    return matrixType.endsWith(QStringLiteral("vs")) || matrixType == QStringLiteral("vs") ||
           matrixType == QStringLiteral("pearsonvs") || matrixType == QStringLiteral("oevs") ||
           matrixType == QStringLiteral("logvs") || matrixType == QStringLiteral("logeovs");
}

bool HicDataController::matrixIsPearson(const QString& matrixType) const {
    return matrixType.contains(QStringLiteral("pearson"));
}

bool HicDataController::matrixIsDivergent(const QString& matrixType) const {
    return matrixType == QStringLiteral("oe") || matrixType == QStringLiteral("controloe") ||
           matrixType == QStringLiteral("logoe") || matrixType == QStringLiteral("explogoe") ||
           matrixType == QStringLiteral("oeratio") || matrixType == QStringLiteral("diff") ||
           matrixType == QStringLiteral("logratio") || matrixIsPearson(matrixType) ||
           matrixType == QStringLiteral("oevs") || matrixType == QStringLiteral("logeovs");
}

QString HicDataController::primaryDataMatrixType(const QString& matrixType) const {
    if (matrixType == QStringLiteral("oe") || matrixType == QStringLiteral("logoe") ||
        matrixType == QStringLiteral("explogoe") || matrixType == QStringLiteral("oeratio") ||
        matrixType == QStringLiteral("oevs") || matrixType == QStringLiteral("logeovs") ||
        matrixType == QStringLiteral("pearson") || matrixType == QStringLiteral("pearsonvs")) {
        return QStringLiteral("oe");
    }
    if (matrixType == QStringLiteral("expected")) {
        return QStringLiteral("expected");
    }
    return QStringLiteral("observed");
}

QString HicDataController::controlDataMatrixType(const QString& matrixType) const {
    if (matrixType == QStringLiteral("controloe") || matrixType == QStringLiteral("oeratio") ||
        matrixType == QStringLiteral("oevs") || matrixType == QStringLiteral("logeovs") ||
        matrixType == QStringLiteral("controlpearson") || matrixType == QStringLiteral("pearsonvs")) {
        return QStringLiteral("oe");
    }
    return QStringLiteral("observed");
}

bool HicDataController::validateMatrixMode(const QString& matrixType) {
    if (wholeGenomeView() && matrixType != QStringLiteral("observed") && matrixType != QStringLiteral("control") &&
        matrixType != QStringLiteral("log") && matrixType != QStringLiteral("logcontrol")) {
        setStatus(QStringLiteral("%1 is not available for whole-genome view.").arg(matrixType));
        return false;
    }
    if ((matrixIsPearson(matrixType) || matrixIsVs(matrixType)) && m_chrX != m_chrY) {
        setStatus(QStringLiteral("%1 is only available for intrachromosomal views.").arg(matrixType));
        return false;
    }
    if (matrixIsPearson(matrixType)) {
        if (m_resolution > 0 && m_resolution < kMinPearsonResolution) {
            setStatus(QStringLiteral("Pearson modes are available at %1 bp resolution or coarser.").arg(kMinPearsonResolution));
            return false;
        }
        const qint64 span = std::max<qint64>(m_x1 - m_x0, m_y1 - m_y0);
        const qint64 bins = (span + std::max(1, m_resolution) - 1) / std::max(1, m_resolution);
        if (bins > kMaxPearsonBins) {
            setStatus(QStringLiteral("Pearson view is too large (%1 bins); zoom in before enabling Pearson.").arg(bins));
            return false;
        }
    }
    if (matrixNeedsControl(matrixType)) {
        QString reason;
        if (!controlSupportsCurrentView(&reason)) {
            setStatus(QStringLiteral("Cannot use %1: %2.").arg(matrixType, reason));
            return false;
        }
    }
    return true;
}

void HicDataController::clampRegion() {
    const qint64 maxX = std::max<qint64>(m_resolution, chromosomeLength(m_chrX));
    const qint64 maxY = std::max<qint64>(m_resolution, chromosomeLength(m_chrY));
    const qint64 minSpan = std::max<qint64>(m_resolution, m_resolution * 20LL);
    const qint64 spanX = std::clamp<qint64>(std::max<qint64>(minSpan, m_x1 - m_x0), m_resolution, maxX);
    const qint64 spanY = std::clamp<qint64>(std::max<qint64>(minSpan, m_y1 - m_y0), m_resolution, maxY);
    m_x0 = std::clamp<qint64>(m_x0, 0, std::max<qint64>(0, maxX - spanX));
    m_y0 = std::clamp<qint64>(m_y0, 0, std::max<qint64>(0, maxY - spanY));
    m_x1 = m_x0 + spanX;
    m_y1 = m_y0 + spanY;
}

void HicDataController::adaptResolutionToSpan(qint64 span) {
    if (m_resolutionLocked || m_metadata.bpResolutions.empty() || span <= 0) return;
    constexpr double targetVisibleBins = 650.0;
    const double ideal = std::max(1.0, static_cast<double>(span) / targetVisibleBins);
    int best = m_metadata.bpResolutions.front();
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const int candidate : m_metadata.bpResolutions) {
        if (candidate <= 0) continue;
        const double distance = std::abs(std::log(static_cast<double>(candidate) / ideal));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    m_resolution = best;
}

qint64 HicDataController::minimumZoomSpan() const {
    int finestResolution = std::max(1, m_resolution);
    if (!m_resolutionLocked && !m_metadata.bpResolutions.empty()) {
        finestResolution = std::numeric_limits<int>::max();
        for (const int candidate : m_metadata.bpResolutions) {
            if (candidate > 0) {
                finestResolution = std::min(finestResolution, candidate);
            }
        }
        if (finestResolution == std::numeric_limits<int>::max()) {
            finestResolution = std::max(1, m_resolution);
        }
    }
    return static_cast<qint64>(finestResolution) * 20LL;
}

std::vector<contactRecord> HicDataController::transformRecordsForDisplay(const QString& matrixType,
                                                                          const std::vector<contactRecord>& primary,
                                                                          const std::vector<contactRecord>& control) const {
    if (matrixType == QStringLiteral("control") || matrixType == QStringLiteral("logcontrol") ||
        matrixType == QStringLiteral("controloe")) {
        return control;
    }
    if (matrixType == QStringLiteral("log") || matrixType == QStringLiteral("logvs")) {
        return primary;
    }
    if (matrixType == QStringLiteral("pearson")) {
        return transformPearsonLike(primary);
    }
    if (matrixType == QStringLiteral("controlpearson")) {
        return transformPearsonLike(control);
    }
    if (matrixType == QStringLiteral("ratio") || matrixType == QStringLiteral("ratio1") ||
        matrixType == QStringLiteral("logratio") || matrixType == QStringLiteral("diff") ||
        matrixType == QStringLiteral("oeratio")) {
        return mergeRecordPairs(primary, control, matrixType);
    }
    if (matrixType == QStringLiteral("logoe") || matrixType == QStringLiteral("explogoe")) {
        std::vector<contactRecord> out = primary;
        for (contactRecord& record : out) {
            record.counts = record.counts > 0 ? static_cast<float>(std::log(record.counts)) : 0.0f;
        }
        return out;
    }
    if (matrixType == QStringLiteral("pearsonvs")) {
        return transformPearsonLike(primary);
    }
    return primary;
}

std::vector<contactRecord> HicDataController::transformPearsonLike(const std::vector<contactRecord>& records) const {
    const qint64 resolution = std::max<qint64>(1, m_resolution);
    const qint64 origin = (std::min(m_x0, m_y0) / resolution) * resolution;
    const qint64 end = ((std::max(m_x1, m_y1) + resolution - 1) / resolution) * resolution;
    const int bins = static_cast<int>((end - origin) / resolution);
    if (bins <= 0 || bins > kMaxPearsonBins) {
        return {};
    }

    std::vector<double> matrix(static_cast<std::size_t>(bins) * static_cast<std::size_t>(bins), 0.0);
    auto indexFor = [&](qint64 genomePosition) -> int {
        return static_cast<int>((genomePosition - origin) / resolution);
    };
    auto setValue = [&](int row, int col, double value) {
        if (row >= 0 && row < bins && col >= 0 && col < bins) {
            matrix[static_cast<std::size_t>(row) * bins + col] = value;
        }
    };

    for (const contactRecord& record : records) {
        const int x = indexFor(record.binX);
        const int y = indexFor(record.binY);
        setValue(y, x, record.counts);
        if (x != y) {
            setValue(x, y, record.counts);
        }
    }

    std::vector<double> means(bins, 0.0);
    std::vector<double> sumsOfSquares(bins, 0.0);
    for (int row = 0; row < bins; ++row) {
        double sum = 0.0;
        for (int col = 0; col < bins; ++col) {
            sum += matrix[static_cast<std::size_t>(row) * bins + col];
        }
        means[row] = sum / bins;
        double ss = 0.0;
        for (int col = 0; col < bins; ++col) {
            const double centered = matrix[static_cast<std::size_t>(row) * bins + col] - means[row];
            ss += centered * centered;
        }
        sumsOfSquares[row] = ss;
    }

    const int xStart = std::max(0, indexFor(m_x0));
    const int xEnd = std::min(bins, static_cast<int>((m_x1 - origin + resolution - 1) / resolution));
    const int yStart = std::max(0, indexFor(m_y0));
    const int yEnd = std::min(bins, static_cast<int>((m_y1 - origin + resolution - 1) / resolution));
    std::vector<contactRecord> out;
    out.reserve(static_cast<std::size_t>(std::max(0, xEnd - xStart)) * static_cast<std::size_t>(std::max(0, yEnd - yStart)));
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            double dot = 0.0;
            for (int k = 0; k < bins; ++k) {
                const double a = matrix[static_cast<std::size_t>(y) * bins + k] - means[y];
                const double b = matrix[static_cast<std::size_t>(x) * bins + k] - means[x];
                dot += a * b;
            }
            const double denom = std::sqrt(sumsOfSquares[y] * sumsOfSquares[x]);
            const double corr = denom > 0.0 ? std::clamp(dot / denom, -1.0, 1.0) : (x == y ? 1.0 : 0.0);
            contactRecord record;
            record.binX = static_cast<int32_t>(origin + static_cast<qint64>(x) * resolution);
            record.binY = static_cast<int32_t>(origin + static_cast<qint64>(y) * resolution);
            record.counts = static_cast<float>(corr);
            out.push_back(record);
        }
    }
    return out;
}

std::vector<contactRecord> HicDataController::mergeRecordPairs(const std::vector<contactRecord>& primary,
                                                               const std::vector<contactRecord>& control,
                                                               const QString& matrixType) const {
    std::unordered_map<quint64, float> controlByBin;
    controlByBin.reserve(control.size());
    for (const contactRecord& record : control) {
        controlByBin[recordKey(record.binX, record.binY)] = record.counts;
    }
    std::vector<contactRecord> out;
    out.reserve(primary.size());
    for (contactRecord record : primary) {
        const auto it = controlByBin.find(recordKey(record.binX, record.binY));
        if (it == controlByBin.end()) {
            continue;
        }
        const double p = record.counts;
        const double c = it->second;
        if (matrixType == QStringLiteral("diff")) {
            record.counts = static_cast<float>(p - c);
        } else if (matrixType == QStringLiteral("logratio")) {
            record.counts = static_cast<float>(std::log2((p + 1.0) / (c + 1.0)));
        } else if (matrixType == QStringLiteral("ratio1")) {
            record.counts = static_cast<float>(p / (c + 1.0));
        } else {
            record.counts = c == 0.0 ? 0.0f : static_cast<float>(p / c);
        }
        out.push_back(record);
    }
    return out;
}

void HicDataController::addRecent(const QString& group, const QString& path) {
    if (path.isEmpty()) return;
    QSettings settings;
    QStringList values = settings.value(settingsKey() + "/" + group).toStringList();
    values.removeAll(path);
    values.push_front(path);
    while (values.size() > kRecentLimit) values.removeLast();
    settings.setValue(settingsKey() + "/" + group, values);
    if (group == QStringLiteral("recentMaps")) refreshDatasetsModel();
}

void HicDataController::refreshDatasetsModel() {
    QVariantList entries;
    for (const QVariant& value : recentMaps()) {
        const QString path = value.toString();
        QVariantMap entry;
        entry["path"] = path;
        entry["name"] = QFileInfo(path).fileName();
        entries.push_back(entry);
    }
    m_datasetsModel->setEntries(std::move(entries));
    refreshSearchResultsModel();
}

void HicDataController::refreshBookmarksModel() {
    m_bookmarksModel->setEntries(savedLocations());
    refreshSearchResultsModel();
}

void HicDataController::refreshTracksModel() {
    m_tracksModel->setEntries(trackSummaries());
    refreshSearchResultsModel();
}

void HicDataController::refreshAnnotationsModel() {
    m_annotationsModel->setEntries(annotationLayerSummaries());
    refreshSearchResultsModel();
}

void HicDataController::refreshSearchResultsModel() {
    QVariantList results;
    const QString needle = m_workspaceSearch.trimmed().toLower();
    if (needle.isEmpty()) {
        m_searchResultsModel->setEntries({});
        return;
    }
    auto addMatch = [&](const QString& kind, const QString& label, const QString& detail, int index, const QString& path = QString()) {
        if (!label.toLower().contains(needle) && !detail.toLower().contains(needle)) return;
        QVariantMap result;
        result["kind"] = kind;
        result["label"] = label;
        result["detail"] = detail;
        result["index"] = index;
        result["path"] = path;
        results.push_back(result);
    };
    int index = 0;
    for (const QVariant& value : recentMaps()) {
        const QString path = value.toString();
        addMatch(QStringLiteral("dataset"), QFileInfo(path).fileName(), path, index++, path);
    }
    index = 0;
    for (const QVariant& value : savedLocations()) {
        const QVariantMap bookmark = value.toMap();
        addMatch(QStringLiteral("bookmark"), bookmark.value("name").toString(),
                 bookmark.value("chrX").toString(), index++);
    }
    for (const QVariant& value : trackSummaries()) {
        const QVariantMap track = value.toMap();
        addMatch(QStringLiteral("track"), track.value("name").toString(), track.value("source").toString(), track.value("index").toInt());
    }
    for (const QVariant& value : annotationLayerSummaries()) {
        const QVariantMap layer = value.toMap();
        addMatch(QStringLiteral("annotation"), layer.value("name").toString(),
                 QStringLiteral("%1 features").arg(layer.value("count").toInt()), layer.value("index").toInt());
    }
    m_searchResultsModel->setEntries(std::move(results));
}

QVariantList HicDataController::recentList(const QString& group) const {
    QSettings settings;
    QVariantList values;
    for (const QString& path : settings.value(settingsKey() + "/" + group).toStringList()) {
        values.push_back(path);
    }
    return values;
}

QVariantMap HicDataController::currentViewState(const QString& name) const {
    QVariantMap state;
    state["name"] = name;
    state["chrX"] = m_chrX;
    state["chrY"] = m_chrY;
    state["x0"] = m_x0;
    state["x1"] = m_x1;
    state["y0"] = m_y0;
    state["y1"] = m_y1;
    state["resolution"] = m_resolution;
    state["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return state;
}

bool HicDataController::applyViewState(const QVariantMap& state) {
    const QString nextChrX = state.value("chrX").toString();
    const QString nextChrY = state.value("chrY").toString();
    if (nextChrX.isEmpty() || nextChrY.isEmpty()) {
        return false;
    }
    pushViewHistory();
    m_chrX = nextChrX;
    m_chrY = nextChrY;
    m_x0 = state.value("x0").toLongLong();
    m_x1 = state.value("x1").toLongLong();
    m_y0 = state.value("y0").toLongLong();
    m_y1 = state.value("y1").toLongLong();
    if (state.value("resolution").toInt() > 0) {
        m_resolution = state.value("resolution").toInt();
    }
    clampRegion();
    applyViewportAspectRatio();
    emit viewChanged();
    return true;
}

QString HicDataController::readTextResource(const QString& pathOrUrl) const {
    if (pathOrUrl.startsWith(QStringLiteral("http://")) || pathOrUrl.startsWith(QStringLiteral("https://"))) {
        QByteArray bytes;
        CURL* curl = curl_easy_init();
        if (!curl) return {};
        curl_easy_setopt(curl, CURLOPT_URL, pathOrUrl.toUtf8().constData());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendCurlText);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        const CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) return {};
        return QString::fromUtf8(bytes);
    }
    QFile file(pathOrUrl);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void HicDataController::updateAutoColorScale(const std::vector<contactRecord>& records) {
    updateAutoColorScale(records, {});
}

void HicDataController::updateAutoColorScale(const std::vector<contactRecord>& records,
                                             const std::vector<contactRecord>& controlRecords) {
    if (!m_colorMaxAuto) {
        return;
    }
    if (matrixIsPearson(m_matrixType)) {
        m_colorMin = -1.0;
        m_colorMax = 1.0;
    } else if (matrixIsDivergent(m_matrixType)) {
        m_colorMin = -5.0;
        m_colorMax = 5.0;
    } else {
        m_colorMin = 0.0;
        std::vector<double> sampled;
        sampled.reserve(((records.size() + controlRecords.size()) / 10) + 1);
        auto sampleRecords = [&sampled](const std::vector<contactRecord>& source) {
            for (std::size_t i = 0; i < source.size(); i += 10) {
                const contactRecord& rec = source[i];
                if (rec.binX != rec.binY && std::isfinite(rec.counts) && rec.counts > 0.0f) {
                    sampled.push_back(rec.counts);
                }
            }
        };
        sampleRecords(records);
        sampleRecords(controlRecords);
        if (!sampled.empty()) {
            const double quantile = std::clamp(m_colorPercentile / 100.0, 0.5, 1.0);
            const std::size_t index = static_cast<std::size_t>(std::floor(quantile * (sampled.size() - 1)));
            std::nth_element(sampled.begin(), sampled.begin() + index, sampled.end());
            m_colorMax = std::max(1.0, sampled[index]);
        } else {
            m_colorMax = 1.0;
        }
    }
    if (m_symmetricColorScale) {
        const double extent = std::max(0.000001, std::max(std::abs(m_colorMin), std::abs(m_colorMax)));
        m_colorMin = -extent;
        m_colorMax = extent;
    }
    emit colorMaxChanged();
}

void HicDataController::pushViewHistory() {
    if (m_restoringView || m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }
    QVariantMap view;
    view["chrX"] = m_chrX;
    view["chrY"] = m_chrY;
    view["x0"] = m_x0;
    view["x1"] = m_x1;
    view["y0"] = m_y0;
    view["y1"] = m_y1;
    view["resolution"] = m_resolution;
    if (!m_undoStack.isEmpty() && m_undoStack.last() == view) {
        return;
    }
    m_undoStack.push_back(view);
    if (m_undoStack.size() > 100) {
        m_undoStack.removeFirst();
    }
    m_redoStack.clear();
    emit viewHistoryChanged();
}

void HicDataController::restoreView(const QVariantMap& view) {
    m_restoringView = true;
    if (view.contains("chrX")) m_chrX = view.value("chrX").toString();
    if (view.contains("chrY")) m_chrY = view.value("chrY").toString();
    if (view.contains("resolution")) m_resolution = view.value("resolution").toInt();
    m_x0 = view.value("x0").toLongLong();
    m_x1 = view.value("x1").toLongLong();
    m_y0 = view.value("y0").toLongLong();
    m_y1 = view.value("y1").toLongLong();
    clampRegion();
    applyViewportAspectRatio();
    m_restoringView = false;
    emit viewHistoryChanged();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::orientTileForRequestedAxes(HicTile& tile) const {
    const chromosome xChr = chromosomeByName(QString::fromStdString(tile.key.chrX));
    const chromosome yChr = chromosomeByName(QString::fromStdString(tile.key.chrY));
    if (xChr.index > yChr.index) {
        for (contactRecord& record : tile.records) {
            std::swap(record.binX, record.binY);
        }
    }
}

HicTileKey HicDataController::paddedRequestKey(const HicTileKey& visibleKey) const {
    HicTileKey key = visibleKey;
    const qint64 resolution = std::max<qint64>(1, key.resolution);
    const qint64 xSpan = std::max<qint64>(resolution, visibleKey.x1 - visibleKey.x0);
    const qint64 ySpan = std::max<qint64>(resolution, visibleKey.y1 - visibleKey.y0);
    const qint64 xPad = std::min<qint64>(resolution * 20LL, std::max<qint64>(resolution * 2LL, xSpan / 10));
    const qint64 yPad = std::min<qint64>(resolution * 20LL, std::max<qint64>(resolution * 2LL, ySpan / 10));
    const qint64 maxX = chromosomeLength(QString::fromStdString(key.chrX));
    const qint64 maxY = chromosomeLength(QString::fromStdString(key.chrY));
    auto alignDown = [resolution](qint64 value) {
        return (value / resolution) * resolution;
    };
    auto alignUp = [resolution](qint64 value) {
        return ((value + resolution - 1) / resolution) * resolution;
    };
    key.x0 = std::clamp<qint64>(alignDown(visibleKey.x0 - xPad), 0, std::max<qint64>(0, maxX));
    key.y0 = std::clamp<qint64>(alignDown(visibleKey.y0 - yPad), 0, std::max<qint64>(0, maxY));
    key.x1 = std::clamp<qint64>(alignUp(visibleKey.x1 + xPad), resolution, std::max<qint64>(resolution, maxX));
    key.y1 = std::clamp<qint64>(alignUp(visibleKey.y1 + yPad), resolution, std::max<qint64>(resolution, maxY));
    if (key.x1 <= key.x0) key.x1 = std::min<qint64>(maxX, key.x0 + resolution);
    if (key.y1 <= key.y0) key.y1 = std::min<qint64>(maxY, key.y0 + resolution);
    return key;
}

bool HicDataController::loadedKeyCoversCurrentView(const HicTileKey& visibleKey) const {
    if (!m_hasLoadedKey) {
        return false;
    }
    return m_loadedKey.filePath == visibleKey.filePath &&
           m_loadedKey.matrixType == visibleKey.matrixType &&
           m_loadedKey.norm == visibleKey.norm &&
           m_loadedKey.unit == visibleKey.unit &&
           m_loadedKey.chrX == visibleKey.chrX &&
           m_loadedKey.chrY == visibleKey.chrY &&
           m_loadedKey.resolution == visibleKey.resolution &&
           m_loadedKey.x0 <= visibleKey.x0 &&
           m_loadedKey.x1 >= visibleKey.x1 &&
           m_loadedKey.y0 <= visibleKey.y0 &&
           m_loadedKey.y1 >= visibleKey.y1;
}

void HicDataController::clearLoadedRegion() {
    m_hasLoadedKey = false;
}

void HicDataController::scheduleRequest() {
    if (m_filePath.isEmpty()) {
        return;
    }
    if (!validateMatrixMode(m_matrixType)) {
        return;
    }
    if (m_tileWatcher.isRunning()) {
        // Invalidate the active result immediately. The underlying straw call is not
        // forcibly terminated (it is not cooperatively cancellable), but it will no
        // longer replace the visible viewport and only the newest pending view runs.
        ++m_requestSerial;
        m_reloadPending = true;
        return;
    }
    const QString dataMatrixType = primaryDataMatrixType(m_matrixType);
    const HicTileKey visibleKey = makeKey(m_filePath, dataMatrixType, m_norm, m_chrX, m_chrY,
                                          m_resolution, m_x0, m_x1, m_y0, m_y1);
    if (loadedKeyCoversCurrentView(visibleKey)) {
        return;
    }
    const HicTileKey key = paddedRequestKey(visibleKey);
    const quint64 requestId = ++m_requestSerial;

    if (!matrixNeedsControl(m_matrixType) && matrixNeedsPrimary(m_matrixType)) {
        if (const HicTile* cached = m_cache->get(key)) {
            std::vector<contactRecord> displayRecords = transformRecordsForDisplay(m_matrixType, cached->records, {});
            {
                QMutexLocker locker(&m_mutex);
                m_records = std::move(displayRecords);
                m_controlRecords.clear();
                rebuildHoverLookupLocked();
                m_loadedKey = cached->key;
                m_hasLoadedKey = true;
            }
            updateAutoColorScale(m_records);
            setStatus(QStringLiteral("%1 records in view (cached).").arg(recordCount()));
            emit cacheStatsChanged();
            emit recordsChanged();
            return;
        }
    }

    startTileLoad(key, requestId);
}

quint64 HicDataController::recordLookupKey(int32_t binX, int32_t binY) {
    return (static_cast<quint64>(static_cast<quint32>(binX)) << 32U) |
           static_cast<quint32>(binY);
}

void HicDataController::rebuildHoverLookupLocked() {
    m_recordHoverLookup.clear();
    m_controlHoverLookup.clear();
    m_recordHoverLookup.reserve(static_cast<qsizetype>(m_records.size()));
    m_controlHoverLookup.reserve(static_cast<qsizetype>(m_controlRecords.size()));
    for (const contactRecord& record : m_records)
        m_recordHoverLookup.insert(recordLookupKey(record.binX, record.binY), record.counts);
    for (const contactRecord& record : m_controlRecords)
        m_controlHoverLookup.insert(recordLookupKey(record.binX, record.binY), record.counts);
}

void HicDataController::startTileLoad(const HicTileKey& key, quint64 requestId) {
    setBusy(true);
    setStatus(QStringLiteral("Loading %1:%2-%3 by %4:%5-%6 at %7 bp...")
                  .arg(QString::fromStdString(key.chrX))
                  .arg(key.x0)
                  .arg(key.x1)
                  .arg(QString::fromStdString(key.chrY))
                  .arg(key.y0)
                  .arg(key.y1)
                  .arg(key.resolution));

    const QString displayMatrixType = m_matrixType;
    const QString controlPath = m_controlFilePath;
    const QString controlMatrixType = controlDataMatrixType(m_matrixType);
    const bool needsPrimary = matrixNeedsPrimary(m_matrixType);
    const bool needsControl = matrixNeedsControl(m_matrixType);
    m_tileWatcher.setFuture(QtConcurrent::run([key, requestId, displayMatrixType, controlPath, controlMatrixType, needsPrimary, needsControl]() {
        TileResult result;
        result.requestId = requestId;
        result.tile.key = key;
        try {
            const std::string chrXLoc = key.chrX + ":" + std::to_string(key.x0) + ":" + std::to_string(key.x1);
            const std::string chrYLoc = key.chrY + ":" + std::to_string(key.y0) + ":" + std::to_string(key.y1);
            if (needsPrimary) {
                result.tile.records = straw(key.matrixType, key.norm, key.filePath, chrXLoc, chrYLoc,
                                            key.unit, key.resolution);
            }
            if (needsControl && !controlPath.isEmpty()) {
                result.hasControl = true;
                result.controlTile.key = key;
                result.controlTile.key.filePath = controlPath.toStdString();
                result.controlTile.key.matrixType = controlMatrixType.toStdString();
                result.controlTile.records = straw(controlMatrixType.toStdString(), key.norm, controlPath.toStdString(), chrXLoc, chrYLoc,
                                                   key.unit, key.resolution);
            }
        } catch (const std::exception& e) {
            result.error = QStringLiteral("Failed to load visible range: %1").arg(e.what());
        }
        return result;
    }));
}
