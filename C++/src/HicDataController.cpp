#include "HicDataController.h"

#include <QtConcurrent>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTextStream>

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

        orientTileForRequestedAxes(result.tile);
        {
            QMutexLocker locker(&m_mutex);
            m_records = result.tile.records;
        }
        updateAutoColorScale(result.tile.records);
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
bool HicDataController::colorMaxAuto() const { return m_colorMaxAuto; }
QString HicDataController::colorMap() const { return m_colorMap; }
QColor HicDataController::customLowColor() const { return m_customLowColor; }
QColor HicDataController::customHighColor() const { return m_customHighColor; }
int HicDataController::trackCount() const { return static_cast<int>(m_tracks.size()); }
int HicDataController::annotationCount() const { return static_cast<int>(m_annotations.size()); }
bool HicDataController::canUndoView() const { return !m_undoStack.isEmpty(); }
bool HicDataController::canRedoView() const { return !m_redoStack.isEmpty(); }

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

void HicDataController::loadTrack(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open 1D track: %1").arg(path));
        return;
    }

    QTextStream in(&file);
    int loaded = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("track")) {
            continue;
        }
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3) {
            continue;
        }
        TrackSegment segment;
        segment.chr = parts[0];
        segment.start = parts[1].toLongLong();
        segment.end = parts[2].toLongLong();
        segment.name = QFileInfo(path).baseName();
        if (parts.size() >= 4) {
            bool ok = false;
            const double v = parts[3].toDouble(&ok);
            segment.value = ok ? v : 1.0;
        } else {
            segment.value = 1.0;
        }
        if (segment.end > segment.start) {
            m_tracks.push_back(segment);
            ++loaded;
        }
    }
    setStatus(QStringLiteral("Loaded %1 1D track intervals.").arg(loaded));
    emit tracksChanged();
}

void HicDataController::loadAnnotations(const QUrl& url) {
    const QString path = localPathFromUrl(url);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open 2D annotations: %1").arg(path));
        return;
    }

    QTextStream in(&file);
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
        }
        if (annotation.end1 > annotation.start1 && annotation.end2 > annotation.start2) {
            m_annotations.push_back(annotation);
            ++loaded;
        }
    }
    setStatus(QStringLiteral("Loaded %1 2D annotations.").arg(loaded));
    emit annotationsChanged();
}

void HicDataController::clearTracks() {
    m_tracks.clear();
    emit tracksChanged();
}

void HicDataController::clearAnnotations() {
    m_annotations.clear();
    emit annotationsChanged();
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

QVariantList HicDataController::visibleTrackSegments(bool xAxis) const {
    QVariantList values;
    const QString chr = xAxis ? m_chrX : m_chrY;
    const qint64 start = xAxis ? m_x0 : m_y0;
    const qint64 end = xAxis ? m_x1 : m_y1;
    for (const TrackSegment& segment : m_tracks) {
        if (segment.chr != chr || segment.end < start || segment.start > end) {
            continue;
        }
        QVariantMap item;
        item["name"] = segment.name;
        item["start"] = segment.start;
        item["end"] = segment.end;
        item["value"] = segment.value;
        item["color"] = segment.color;
        values.push_back(item);
    }
    return values;
}

QVariantList HicDataController::visibleAnnotations() const {
    QVariantList values;
    for (const Annotation2D& annotation : m_annotations) {
        const bool direct = annotation.chr1 == m_chrX && annotation.chr2 == m_chrY &&
                            annotation.end1 >= m_x0 && annotation.start1 <= m_x1 &&
                            annotation.end2 >= m_y0 && annotation.start2 <= m_y1;
        const bool reflected = annotation.chr1 == m_chrY && annotation.chr2 == m_chrX &&
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
        item["color"] = annotation.color;
        values.push_back(item);

        if (m_chrX == m_chrY && annotation.chr1 == annotation.chr2) {
            QVariantMap mirror = item;
            mirror["x0"] = item["y0"];
            mirror["x1"] = item["y1"];
            mirror["y0"] = item["x0"];
            mirror["y1"] = item["x1"];
            values.push_back(mirror);
        }
    }
    return values;
}

QString HicDataController::positionText(double xFraction, double yFraction) const {
    const qint64 x = m_x0 + static_cast<qint64>((m_x1 - m_x0) * std::clamp(xFraction, 0.0, 1.0));
    const qint64 y = m_y0 + static_cast<qint64>((m_y1 - m_y0) * std::clamp(yFraction, 0.0, 1.0));
    return QStringLiteral("%1:%2 | %3:%4 | %5 bp")
        .arg(m_chrX).arg(x)
        .arg(m_chrY).arg(y)
        .arg(m_resolution);
}

void HicDataController::copyPosition(double xFraction, double yFraction) const {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(positionText(xFraction, yFraction));
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
    auto parseLocation = [this](const QString& text, QString& chr, qint64& start, qint64& end) -> bool {
        const QString cleaned = text.trimmed().remove(',');
        const QStringList chrSplit = cleaned.split(':');
        if (chrSplit.isEmpty() || chrSplit[0].isEmpty()) {
            return false;
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
        const QStringList range = chrSplit[1].split('-');
        bool okStart = false;
        start = range.value(0).toLongLong(&okStart);
        if (!okStart) return false;
        if (range.size() > 1) {
            bool okEnd = false;
            end = range[1].toLongLong(&okEnd);
            if (!okEnd) return false;
        } else {
            const qint64 span = std::max<qint64>(m_resolution * 100LL, m_x1 - m_x0);
            end = start + span;
        }
        start = std::clamp<qint64>(start, 0, chrLength);
        end = std::clamp<qint64>(end, start + m_resolution, chrLength);
        return end > start;
    };

    QString nextChrX;
    QString nextChrY;
    qint64 nextX0 = 0, nextX1 = 0, nextY0 = 0, nextY1 = 0;
    if (!parseLocation(xLocation, nextChrX, nextX0, nextX1)) {
        setStatus(QStringLiteral("Could not parse top location."));
        return;
    }
    if (!parseLocation(yLocation.isEmpty() ? xLocation : yLocation, nextChrY, nextY0, nextY1)) {
        setStatus(QStringLiteral("Could not parse left location."));
        return;
    }

    pushViewHistory();
    m_chrX = nextChrX;
    m_chrY = nextChrY;
    m_x0 = nextX0;
    m_x1 = nextX1;
    m_y0 = nextY0;
    m_y1 = nextY1;
    clampRegion();
    emit viewChanged();
    scheduleRequest();
}

void HicDataController::undoView() {
    if (m_undoStack.isEmpty()) {
        return;
    }
    QVariantMap current;
    current["x0"] = m_x0; current["x1"] = m_x1; current["y0"] = m_y0; current["y1"] = m_y1;
    m_redoStack.push_back(current);
    restoreView(m_undoStack.takeLast());
}

void HicDataController::redoView() {
    if (m_redoStack.isEmpty()) {
        return;
    }
    QVariantMap current;
    current["x0"] = m_x0; current["x1"] = m_x1; current["y0"] = m_y0; current["y1"] = m_y1;
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
    m_x0 = nextX0;
    m_x1 = nextX1;
    m_y0 = nextY0;
    m_y1 = nextY1;
    clampRegion();
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
    if (factor <= 0.0 || m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }

    pushViewHistory();
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
    if (!m_interactionActive) {
        pushViewHistory();
    }
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
    pushViewHistory();
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
    m_colorMaxAuto = true;
    m_colorMax = m_matrixType == QStringLiteral("oe") ? 5.0 : 50.0;
    emit colorMaxChanged();
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
    m_colorMaxAuto = false;
    m_colorMax = std::max(0.1, value);
    emit colorMaxChanged();
}

void HicDataController::resetColorScale() {
    m_colorMaxAuto = true;
    m_colorMax = m_matrixType == QStringLiteral("oe") ? 5.0 : 50.0;
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

void HicDataController::updateAutoColorScale(const std::vector<contactRecord>& records) {
    if (!m_colorMaxAuto) {
        return;
    }
    if (m_matrixType == QStringLiteral("oe")) {
        m_colorMax = 5.0;
    } else {
        std::vector<double> sampled;
        sampled.reserve((records.size() / 10) + 1);
        for (std::size_t i = 0; i < records.size(); i += 10) {
            const contactRecord& rec = records[i];
            if (rec.binX != rec.binY && std::isfinite(rec.counts) && rec.counts > 0.0f) {
                sampled.push_back(rec.counts);
            }
        }
        if (!sampled.empty()) {
            const std::size_t index = static_cast<std::size_t>(std::floor(0.95 * (sampled.size() - 1)));
            std::nth_element(sampled.begin(), sampled.begin() + index, sampled.end());
            m_colorMax = std::max(1.0, sampled[index]);
        } else {
            m_colorMax = 1.0;
        }
    }
    emit colorMaxChanged();
}

void HicDataController::pushViewHistory() {
    if (m_restoringView || m_chrX.isEmpty() || m_chrY.isEmpty()) {
        return;
    }
    QVariantMap view;
    view["x0"] = m_x0;
    view["x1"] = m_x1;
    view["y0"] = m_y0;
    view["y1"] = m_y1;
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
    m_x0 = view.value("x0").toLongLong();
    m_x1 = view.value("x1").toLongLong();
    m_y0 = view.value("y0").toLongLong();
    m_y1 = view.value("y1").toLongLong();
    clampRegion();
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
        updateAutoColorScale(cached->records);
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
