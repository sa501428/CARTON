#include "HicDataController.h"

#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <exception>

HicDataController::HicDataController(QObject* parent)
    : QObject(parent),
      m_cache(std::make_unique<HicTileCache>()) {
    connect(&m_metadataWatcher, &QFutureWatcher<HicFileMetadata>::finished, this, [this]() {
        setBusy(false);
        try {
            applyMetadata(m_metadataWatcher.result());
            setStatus(QStringLiteral("Loaded %1.").arg(m_filePath));
        } catch (const std::exception& e) {
            setStatus(QStringLiteral("Failed to open file: %1").arg(e.what()));
        }
    });

    connect(&m_tileWatcher, &QFutureWatcher<TileResult>::finished, this, [this]() {
        TileResult result = m_tileWatcher.result();
        if (result.requestId != m_requestSerial) {
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

        {
            QMutexLocker locker(&m_mutex);
            m_records = std::move(result.tile.records);
        }
        if (!result.fromCache) {
            m_cache->put(std::move(result.tile));
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
    m_tileWatcher.cancel();
    m_tileWatcher.waitForFinished();
}

QString HicDataController::filePath() const { return m_filePath; }
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
QString HicDataController::colorMap() const { return m_colorMap; }
QColor HicDataController::customLowColor() const { return m_customLowColor; }
QColor HicDataController::customHighColor() const { return m_customHighColor; }

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
    if (m_metadataWatcher.isRunning() || m_tileWatcher.isRunning()) {
        setStatus(QStringLiteral("Please wait for the current load to finish."));
        return;
    }

    m_filePath = path;
    m_cache->clear();
    {
        QMutexLocker locker(&m_mutex);
        m_records.clear();
    }
    emit filePathChanged();
    emit recordsChanged();
    setBusy(true);
    setStatus(QStringLiteral("Reading .hic header..."));
    m_metadataWatcher.setFuture(QtConcurrent::run([path]() {
        return inspectHicFile(path.toStdString());
    }));
}

QVariantList HicDataController::chromosomeNames() const {
    QVariantList names;
    for (const chromosome& chr : m_metadata.chromosomes) {
        if (chr.index > 0) {
            names.push_back(QString::fromStdString(chr.name));
        }
    }
    return names;
}

QVariantList HicDataController::resolutions() const {
    QVariantList values;
    for (int32_t resolution : m_metadata.bpResolutions) {
        values.push_back(resolution);
    }
    return values;
}

void HicDataController::requestVisibleRegion() {
    if (m_filePath.isEmpty() || m_chrX.isEmpty() || m_chrY.isEmpty() || m_resolution <= 0) {
        return;
    }
    clampRegion();
    scheduleRequest();
}

void HicDataController::zoom(double factor, double centerX, double centerY) {
    if (factor <= 0.0 || m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }

    const qint64 width = std::max<qint64>(m_resolution, m_x1 - m_x0);
    const qint64 height = std::max<qint64>(m_resolution, m_y1 - m_y0);
    const qint64 nextWidth = std::max<qint64>(m_resolution * 20LL, static_cast<qint64>(width / factor));
    const qint64 nextHeight = std::max<qint64>(m_resolution * 20LL, static_cast<qint64>(height / factor));
    const qint64 centerGenomeX = m_x0 + static_cast<qint64>(width * centerX);
    const qint64 centerGenomeY = m_y0 + static_cast<qint64>(height * centerY);

    m_x0 = centerGenomeX - nextWidth / 2;
    m_x1 = m_x0 + nextWidth;
    m_y0 = centerGenomeY - nextHeight / 2;
    m_y1 = m_y0 + nextHeight;
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::pan(double dxFraction, double dyFraction) {
    const qint64 width = m_x1 - m_x0;
    const qint64 height = m_y1 - m_y0;
    const qint64 dx = static_cast<qint64>(width * dxFraction);
    const qint64 dy = static_cast<qint64>(height * dyFraction);
    m_x0 += dx;
    m_x1 += dx;
    m_y0 += dy;
    m_y1 += dy;
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::resetView() {
    if (m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }
    m_x0 = 0;
    m_y0 = 0;
    m_x1 = chromosomeLength(m_chrX);
    m_y1 = chromosomeLength(m_chrY);
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
    m_matrixType = value;
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setNorm(const QString& value) {
    if (m_norm == value) return;
    m_norm = value;
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setResolution(int value) {
    if (m_resolution == value || value <= 0) return;
    m_resolution = value;
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::setX0(qint64 value) { if (m_x0 != value) { m_x0 = value; emit viewChanged(); } }
void HicDataController::setX1(qint64 value) { if (m_x1 != value) { m_x1 = value; emit viewChanged(); } }
void HicDataController::setY0(qint64 value) { if (m_y0 != value) { m_y0 = value; emit viewChanged(); } }
void HicDataController::setY1(qint64 value) { if (m_y1 != value) { m_y1 = value; emit viewChanged(); } }

void HicDataController::setColorMax(double value) {
    if (qFuzzyCompare(m_colorMax, value)) return;
    m_colorMax = std::max(0.1, value);
    emit colorMaxChanged();
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

std::vector<contactRecord> HicDataController::recordsSnapshot() const {
    QMutexLocker locker(&m_mutex);
    return m_records;
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
    m_genomeId = QString::fromStdString(metadata.genomeID);
    if (!metadata.bpResolutions.empty()) {
        m_resolution = metadata.bpResolutions.front();
    }
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
    return chromosome{};
}

qint64 HicDataController::chromosomeLength(const QString& name) const {
    return chromosomeByName(name).length;
}

void HicDataController::clampRegion() {
    const qint64 maxX = std::max<qint64>(m_resolution, chromosomeLength(m_chrX));
    const qint64 maxY = std::max<qint64>(m_resolution, chromosomeLength(m_chrY));
    qint64 width = std::max<qint64>(m_resolution * 20LL, m_x1 - m_x0);
    qint64 height = std::max<qint64>(m_resolution * 20LL, m_y1 - m_y0);
    width = std::min(width, maxX);
    height = std::min(height, maxY);
    m_x0 = std::clamp<qint64>(m_x0, 0, std::max<qint64>(0, maxX - width));
    m_y0 = std::clamp<qint64>(m_y0, 0, std::max<qint64>(0, maxY - height));
    m_x1 = m_x0 + width;
    m_y1 = m_y0 + height;
}

void HicDataController::scheduleRequest() {
    if (m_filePath.isEmpty()) {
        return;
    }
    if (m_tileWatcher.isRunning()) {
        m_reloadPending = true;
        return;
    }
    const HicTileKey key = makeKey(m_filePath, m_matrixType, m_norm, m_chrX, m_chrY,
                                   m_resolution, m_x0, m_x1, m_y0, m_y1);
    const quint64 requestId = ++m_requestSerial;

    if (auto cached = m_cache->get(key)) {
        {
            QMutexLocker locker(&m_mutex);
            m_records = cached->records;
        }
        setStatus(QStringLiteral("%1 records in view (cached).").arg(recordCount()));
        emit recordsChanged();
        return;
    }

    startTileLoad(key, requestId);
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

    m_tileWatcher.setFuture(QtConcurrent::run([key, requestId]() {
        TileResult result;
        result.requestId = requestId;
        result.tile.key = key;
        try {
            const std::string chrXLoc = key.chrX + ":" + std::to_string(key.x0) + ":" + std::to_string(key.x1);
            const std::string chrYLoc = key.chrY + ":" + std::to_string(key.y0) + ":" + std::to_string(key.y1);
            result.tile.records = straw(key.matrixType, key.norm, key.filePath, chrXLoc, chrYLoc,
                                        key.unit, key.resolution);
        } catch (const std::exception& e) {
            result.error = QStringLiteral("Failed to load visible range: %1").arg(e.what());
        }
        return result;
    }));
}
