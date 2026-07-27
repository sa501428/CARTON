#include "TabSession.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

#include "MatrixAnalysis.h"

TabSession::TabSession(QObject* parent)
    : QObject(parent), m_regionSet(new RegionSetModel(this)) {
    connect(m_regionSet, &RegionSetModel::regionsChanged, this, [this]() {
        if (!m_initializing) rebuildCells();
    });
    initialize(QStringLiteral("single"));
}

TabSession::~TabSession() = default;

TabSession::Type TabSession::parseType(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("multi-map") || normalized == QStringLiteral("multimap") || normalized == QStringLiteral("2"))
        return Type::MultiMap;
    if (normalized == QStringLiteral("multi-region") || normalized == QStringLiteral("multiregion") || normalized == QStringLiteral("3"))
        return Type::MultiRegion;
    if (normalized == QStringLiteral("map-region") || normalized == QStringLiteral("map-by-region") || normalized == QStringLiteral("4"))
        return Type::MapRegion;
    if (normalized == QStringLiteral("pairwise") || normalized == QStringLiteral("pairwise-region") || normalized == QStringLiteral("5"))
        return Type::Pairwise;
    if (normalized == QStringLiteral("rotated-45") || normalized == QStringLiteral("diagonal-45") || normalized == QStringLiteral("6"))
        return Type::Rotated45;
    if (normalized == QStringLiteral("bullseye") || normalized == QStringLiteral("7"))
        return Type::Bullseye;
    if (normalized == QStringLiteral("virtual-4c") || normalized == QStringLiteral("pseudo-4c") || normalized == QStringLiteral("8"))
        return Type::Virtual4C;
    if (normalized == QStringLiteral("processing") || normalized == QStringLiteral("experimental-processing") || normalized == QStringLiteral("9"))
        return Type::Processing;
    return Type::Single;
}

QString TabSession::typeName(Type type) {
    switch (type) {
    case Type::MultiMap: return QStringLiteral("multi-map");
    case Type::MultiRegion: return QStringLiteral("multi-region");
    case Type::MapRegion: return QStringLiteral("map-region");
    case Type::Pairwise: return QStringLiteral("pairwise");
    case Type::Rotated45: return QStringLiteral("rotated-45");
    case Type::Bullseye: return QStringLiteral("bullseye");
    case Type::Virtual4C: return QStringLiteral("virtual-4c");
    case Type::Processing: return QStringLiteral("processing");
    default: return QStringLiteral("single");
    }
}

QString TabSession::typeDisplayName(Type type) {
    switch (type) {
    case Type::MultiMap: return QStringLiteral("Multi-map");
    case Type::MultiRegion: return QStringLiteral("Multi-region");
    case Type::MapRegion: return QStringLiteral("Maps × regions");
    case Type::Pairwise: return QStringLiteral("Pairwise regions");
    case Type::Rotated45: return QStringLiteral("45° diagonal heatmaps");
    case Type::Bullseye: return QStringLiteral("SIP bullseye");
    case Type::Virtual4C: return QStringLiteral("Virtual 4C");
    case Type::Processing: return QStringLiteral("Experimental processing");
    default: return QStringLiteral("Single map");
    }
}

QString TabSession::type() const { return typeName(m_type); }
QString TabSession::typeLabel() const { return typeDisplayName(m_type); }
QString TabSession::title() const { return m_title; }
QVariantList TabSession::cells() const { return m_cellModel; }
int TabSession::cellCount() const { return m_cells.size(); }
int TabSession::mapCount() const { return m_maps.size(); }
int TabSession::regionCount() const { return m_regionSet->rowCount(); }
int TabSession::layoutColumns() const { return m_layoutColumns; }
bool TabSession::transposed() const { return m_transposed; }
QString TabSession::diagonalMode() const { return m_diagonalMode; }
QString TabSession::layerScope() const { return m_layerScope; }
bool TabSession::linkNavigation() const { return m_linkNavigation; }
bool TabSession::linkCrosshair() const { return m_linkCrosshair; }
bool TabSession::linkColorScale() const { return m_linkColorScale; }
int TabSession::activeCellIndex() const { return m_activeCellIndex; }
RegionSetModel* TabSession::regionSet() const { return m_regionSet; }
qint64 TabSession::windowSize() const { return m_regionSet->windowSize(); }
int TabSession::analysisPaneHeight() const { return m_analysisPaneHeight; }
qint64 TabSession::diagonalMaxDistance() const { return m_diagonalMaxDistance; }
qint64 TabSession::bullseyeCenterX() const { return m_bullseyeCenterX; }
qint64 TabSession::bullseyeCenterY() const { return m_bullseyeCenterY; }
int TabSession::bullseyeRadiusBins() const { return m_bullseyeRadiusBins; }
qint64 TabSession::bullseyeRadiusBp() const {
    return static_cast<qint64>(m_bullseyeRadiusBins) *
           std::max(1, activeController() ? activeController()->resolution() : 1);
}
bool TabSession::bullseyePinned() const { return m_bullseyePinned; }
qint64 TabSession::virtual4CAnchor() const { return m_virtual4CAnchor; }
QString TabSession::virtual4CAxis() const { return m_virtual4CAxis; }
QString TabSession::processingOperator() const { return m_processingOperator; }
double TabSession::processingParameter() const { return m_processingParameter; }
double TabSession::processingThreshold() const { return m_processingThreshold; }
int TabSession::processingMaximumBins() const { return m_processingMaximumBins; }

int TabSession::rowCount() const {
    if (m_cells.isEmpty()) return 0;
    int maximum = 0;
    for (const CellSpec& cell : m_cells) maximum = std::max(maximum, cell.row + 1);
    return maximum;
}

int TabSession::columnCount() const {
    if (m_cells.isEmpty()) return 0;
    int maximum = 0;
    for (const CellSpec& cell : m_cells) maximum = std::max(maximum, cell.column + 1);
    return maximum;
}

HicDataController* TabSession::activeController() const {
    if (m_activeCellIndex < 0 || m_activeCellIndex >= m_cells.size()) return nullptr;
    return m_cells[m_activeCellIndex].controller;
}

QVariantList TabSession::maps() const {
    QVariantList values;
    for (int i = 0; i < m_maps.size(); ++i) {
        QVariantMap item;
        item[QStringLiteral("index")] = i;
        item[QStringLiteral("id")] = m_maps[i].id;
        item[QStringLiteral("label")] = m_maps[i].label.isEmpty() ? QStringLiteral("Map %1").arg(i + 1) : m_maps[i].label;
        item[QStringLiteral("primaryPath")] = m_maps[i].primaryPath;
        item[QStringLiteral("controlPath")] = m_maps[i].controlPath;
        item[QStringLiteral("flipped")] = m_maps[i].flipped;
        values.push_back(item);
    }
    return values;
}

void TabSession::setTitle(const QString& value) {
    const QString next = value.trimmed();
    if (next.isEmpty() || next == m_title) return;
    m_title = next;
    emit titleChanged();
}

void TabSession::setLayoutColumns(int value) {
    value = std::clamp(value, 1, 12);
    if (m_layoutColumns == value) return;
    m_layoutColumns = value;
    rebuildCells();
}

void TabSession::setTransposed(bool value) {
    if (m_transposed == value) return;
    m_transposed = value;
    rebuildCells();
}

void TabSession::setDiagonalMode(const QString& value) {
    const QString next = value == QStringLiteral("blank") ? QStringLiteral("blank") : QStringLiteral("split");
    if (m_diagonalMode == next) return;
    m_diagonalMode = next;
    rebuildCells();
}

void TabSession::setLayerScope(const QString& value) {
    const QString next = value.trimmed().toLower();
    if (m_layerScope == next) return;
    m_layerScope = next;
    emit layerScopeChanged();
}

void TabSession::setLinkNavigation(bool value) {
    if (m_linkNavigation == value) return;
    m_linkNavigation = value;
    emit linkingChanged();
}

void TabSession::setLinkCrosshair(bool value) {
    if (m_linkCrosshair == value) return;
    m_linkCrosshair = value;
    emit linkingChanged();
}

void TabSession::setLinkColorScale(bool value) {
    if (m_linkColorScale == value) return;
    m_linkColorScale = value;
    emit linkingChanged();
    if (value) propagateColor(activeController());
}

void TabSession::setActiveCellIndex(int value) {
    if (m_cells.isEmpty()) value = -1;
    else value = std::clamp(value, 0, static_cast<int>(m_cells.size()) - 1);
    if (m_activeCellIndex == value) return;
    m_activeCellIndex = value;
    updateCellModel();
    emit cellsChanged();
    emit activeCellChanged();
}

void TabSession::setWindowSize(qint64 value) { m_regionSet->setWindowSize(value); }

void TabSession::setAnalysisPaneHeight(int value) {
    value = std::clamp(value, 100, 1200);
    if (m_analysisPaneHeight == value) return;
    m_analysisPaneHeight = value;
    emit analysisSettingsChanged();
}

void TabSession::setDiagonalMaxDistance(qint64 value) {
    value = std::clamp<qint64>(value, 1000, 1000000000LL);
    if (m_diagonalMaxDistance == value) return;
    m_diagonalMaxDistance = value;
    for (const CellSpec& cell : m_cells) {
        if (cell.controller)
            cell.controller->setAnalysisPaddingBins(static_cast<int>(std::clamp<qint64>(
                (value + std::max(1, cell.controller->resolution()) - 1) /
                    std::max(1, cell.controller->resolution()), 1, 2000)));
    }
    emit analysisSettingsChanged();
}

void TabSession::setBullseyeCenterX(qint64 value) {
    value = std::max<qint64>(0, value);
    if (m_bullseyeCenterX == value) return;
    m_bullseyeCenterX = value;
    emit analysisSettingsChanged();
}

void TabSession::setBullseyeCenterY(qint64 value) {
    value = std::max<qint64>(0, value);
    if (m_bullseyeCenterY == value) return;
    m_bullseyeCenterY = value;
    emit analysisSettingsChanged();
}

void TabSession::setBullseyeRadiusBins(int value) {
    value = std::clamp(value, 1, 100);
    if (m_bullseyeRadiusBins == value) return;
    m_bullseyeRadiusBins = value;
    for (const CellSpec& cell : m_cells) if (cell.controller)
        cell.controller->setAnalysisPaddingBins(value + 2);
    emit analysisSettingsChanged();
}

void TabSession::setBullseyeRadiusBp(qint64 value) {
    const int resolution = std::max(1, activeController() ? activeController()->resolution() : 1);
    setBullseyeRadiusBins(static_cast<int>(std::clamp<qint64>(
        (std::max<qint64>(resolution, value) + resolution / 2) / resolution, 1, 100)));
}

void TabSession::setBullseyePinned(bool value) {
    if (m_bullseyePinned == value) return;
    m_bullseyePinned = value;
    emit analysisSettingsChanged();
}

void TabSession::setVirtual4CAnchor(qint64 value) {
    value = std::max<qint64>(0, value);
    if (m_virtual4CAnchor == value) return;
    m_virtual4CAnchor = value;
    emit analysisSettingsChanged();
}

void TabSession::setVirtual4CAxis(const QString& value) {
    const QString next = value.trimmed().toLower() == QStringLiteral("column")
        ? QStringLiteral("column") : QStringLiteral("row");
    if (m_virtual4CAxis == next) return;
    m_virtual4CAxis = next;
    emit analysisSettingsChanged();
}

void TabSession::setProcessingOperator(const QString& value) {
    QString next = value.trimmed().toLower();
    next.replace(QLatin1Char(' '), QLatin1Char('-'));
    if (next == QStringLiteral("gaussian-smoothing")) next = QStringLiteral("gaussian");
    else if (next == QStringLiteral("difference-of-gaussians")) next = QStringLiteral("dog");
    else if (next == QStringLiteral("laplacian-of-gaussian")) next = QStringLiteral("log");
    else if (next == QStringLiteral("steerable-filter")) next = QStringLiteral("steerable");
    else if (next == QStringLiteral("gabor-filter")) next = QStringLiteral("gabor");
    else if (next == QStringLiteral("local-binary-pattern")) next = QStringLiteral("lbp");
    else if (next == QStringLiteral("polar-transform")) next = QStringLiteral("polar");
    if (next.isEmpty() || m_processingOperator == next) return;
    m_processingOperator = next;
    if (next == QStringLiteral("gabor") && m_processingParameter > 12.0)
        m_processingParameter = 12.0;
    emit analysisSettingsChanged();
}

void TabSession::setProcessingParameter(double value) {
    value = std::isfinite(value) ? std::clamp(value, 0.1, 100.0) : 1.0;
    if (qFuzzyCompare(m_processingParameter, value)) return;
    m_processingParameter = value;
    emit analysisSettingsChanged();
}

void TabSession::setProcessingThreshold(double value) {
    if (!std::isfinite(value)) value = 0.0;
    if (qFuzzyCompare(m_processingThreshold, value)) return;
    m_processingThreshold = value;
    emit analysisSettingsChanged();
}

void TabSession::setProcessingMaximumBins(int value) {
    value = value > 512 ? 1024 : 512;
    if (m_processingMaximumBins == value) return;
    m_processingMaximumBins = value;
    emit analysisSettingsChanged();
}

bool TabSession::isMultiSourceType() const {
    return m_type == Type::MultiMap || m_type == Type::MapRegion || m_type == Type::Rotated45 ||
           m_type == Type::Bullseye || m_type == Type::Virtual4C || m_type == Type::Processing;
}

void TabSession::initialize(const QString& value) {
    m_initializing = true;
    const Type nextType = parseType(value);
    for (CellSpec& cell : m_cells) if (cell.controller) cell.controller->deleteLater();
    m_cells.clear();
    m_cellModel.clear();
    m_regionSet->clear();
    m_maps.clear();
    m_type = nextType;
    ensureMapCount(nextType == Type::MultiMap || nextType == Type::MapRegion ||
                   nextType == Type::Rotated45 || nextType == Type::Bullseye ||
                   nextType == Type::Virtual4C || nextType == Type::Processing ? 2 : 1);
    m_layoutColumns = nextType == Type::Pairwise ? 10 : 2;
    m_transposed = false;
    m_diagonalMode = QStringLiteral("split");
    m_layerScope = QStringLiteral("default");
    m_linkNavigation = isMultiSourceType();
    m_linkCrosshair = true;
    m_linkColorScale = false;
    m_title = typeDisplayName(nextType);
    m_activeCellIndex = 0;
    m_initializing = false;
    rebuildCells();
    emit structureChanged();
    emit titleChanged();
}

void TabSession::ensureMapCount(int count) {
    while (m_maps.size() < count) {
        MapSpec map;
        map.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        map.label = QStringLiteral("Map %1").arg(m_maps.size() + 1);
        m_maps.push_back(map);
    }
}

void TabSession::addMap() {
    if (!isMultiSourceType()) return;
    ensureMapCount(m_maps.size() + 1);
    rebuildCells();
}

void TabSession::removeMap(int mapIndex) {
    if (!isMultiSourceType() || m_maps.size() <= 1 ||
        mapIndex < 0 || mapIndex >= m_maps.size()) return;
    m_maps.removeAt(mapIndex);
    for (int i = 0; i < m_maps.size(); ++i) if (m_maps[i].label.startsWith(QStringLiteral("Map ")))
        m_maps[i].label = QStringLiteral("Map %1").arg(i + 1);
    rebuildCells();
}

HicDataController* TabSession::createController(const QString& key) {
    auto* controller = new HicDataController(this);
    controller->setMinimapEnabled(m_type == Type::Single);
    if (m_type == Type::Bullseye) controller->setAnalysisPaddingBins(m_bullseyeRadiusBins + 2);
    else if (m_type == Type::Rotated45)
        controller->setAnalysisPaddingBins(static_cast<int>(std::clamp<qint64>(
            m_diagonalMaxDistance / std::max(1, controller->resolution()), 1, 2000)));
    connect(controller, &HicDataController::metadataChanged, this, [this, controller]() {
        for (CellSpec& cell : m_cells) if (cell.controller == controller) {
            openMapForCell(cell);
            const auto pending = m_pendingCellStates.find(cell.key);
            if (pending != m_pendingCellStates.end()) {
                controller->restoreSessionState(pending.value(), m_type == Type::Single || m_type == Type::MultiMap);
                m_pendingCellStates.erase(pending);
            }
            applyCellRegion(cell);
            applyCellMode(cell);
            break;
        }
    });
    connect(controller, &HicDataController::controlReadyChanged, this, [this, controller]() {
        for (CellSpec& cell : m_cells) if (cell.controller == controller) {
            applyCellMode(cell);
            break;
        }
    });
    connect(controller, &HicDataController::colorMaxChanged, this, [this, controller]() {
        if (m_linkColorScale) propagateColor(controller);
    });
    connect(controller, &HicDataController::colorMapChanged, this, [this, controller]() {
        if (m_linkColorScale) propagateColor(controller);
    });
    connect(controller, &HicDataController::viewChanged, this, [this, controller]() {
        if (m_type == Type::Rotated45)
            controller->setAnalysisPaddingBins(static_cast<int>(std::clamp<qint64>(
                (m_diagonalMaxDistance + std::max(1, controller->resolution()) - 1) /
                    std::max(1, controller->resolution()), 1, 2000)));
        else if (m_type == Type::Bullseye)
            controller->setAnalysisPaddingBins(m_bullseyeRadiusBins + 2);
        if (m_type == Type::Rotated45 && controller->chrX() != controller->chrY() &&
            !controller->chrX().isEmpty()) {
            controller->setViewRegion(controller->chrX(), controller->x0(), controller->x1(),
                                      controller->chrX(), controller->x0(), controller->x1());
            return;
        }
        if (m_type == Type::Bullseye || m_type == Type::Virtual4C || m_type == Type::Processing)
            emit analysisSettingsChanged();
    });
    controller->setObjectName(key);
    return controller;
}

void TabSession::rebuildCells() {
    QHash<QString, QPointer<HicDataController>> existing;
    for (const CellSpec& cell : m_cells) if (cell.controller) existing.insert(cell.key, cell.controller);
    QVector<CellSpec> next;
    const QVariantList regions = m_regionSet->entries();

    auto append = [&](CellSpec cell) {
        if (!cell.blank) {
            cell.controller = existing.take(cell.key);
            if (!cell.controller) cell.controller = createController(cell.key);
        }
        next.push_back(std::move(cell));
    };

    if (m_type == Type::Single) {
        CellSpec cell;
        cell.key = QStringLiteral("single");
        cell.label = m_maps[0].label;
        append(cell);
    } else if (m_type == Type::MultiMap || m_type == Type::Rotated45 || m_type == Type::Bullseye ||
               m_type == Type::Virtual4C || m_type == Type::Processing) {
        for (int map = 0; map < m_maps.size(); ++map) {
            CellSpec cell;
            cell.key = QStringLiteral("map:%1").arg(m_maps[map].id);
            cell.mapIndex = map;
            cell.row = map / m_layoutColumns;
            cell.column = map % m_layoutColumns;
            cell.label = m_maps[map].label;
            append(cell);
        }
    } else if (m_type == Type::MultiRegion) {
        const int count = std::max(1, static_cast<int>(regions.size()));
        for (int region = 0; region < count; ++region) {
            CellSpec cell;
            cell.key = regions.isEmpty() ? QStringLiteral("region:placeholder") : QStringLiteral("region:%1").arg(region);
            cell.regionIndex = regions.isEmpty() ? -1 : region;
            cell.row = region / m_layoutColumns;
            cell.column = region % m_layoutColumns;
            cell.label = regions.isEmpty() ? QStringLiteral("Load BEDPE regions")
                                           : regions[region].toMap().value(QStringLiteral("label")).toString();
            append(cell);
        }
    } else if (m_type == Type::MapRegion) {
        const int regionCount = std::max(1, static_cast<int>(regions.size()));
        for (int region = 0; region < regionCount; ++region) for (int map = 0; map < m_maps.size(); ++map) {
            CellSpec cell;
            cell.key = QStringLiteral("map:%1:region:%2").arg(m_maps[map].id).arg(regions.isEmpty() ? -1 : region);
            cell.mapIndex = map;
            cell.regionIndex = regions.isEmpty() ? -1 : region;
            cell.row = m_transposed ? map : region;
            cell.column = m_transposed ? region : map;
            const QString regionLabel = regions.isEmpty() ? QStringLiteral("Load BEDPE regions")
                                                          : regions[region].toMap().value(QStringLiteral("label")).toString();
            cell.label = QStringLiteral("%1 · %2").arg(m_maps[map].label, regionLabel);
            append(cell);
        }
    } else {
        if (regions.isEmpty()) {
            CellSpec cell;
            cell.key = QStringLiteral("pairwise:placeholder");
            cell.label = QStringLiteral("Load BED regions");
            append(cell);
        } else {
            const int count = regions.size();
            for (int row = 0; row < count; ++row) for (int column = 0; column < count; ++column) {
                CellSpec cell;
                cell.key = QStringLiteral("pairwise:%1:%2").arg(row).arg(column);
                cell.xRegionIndex = column;
                cell.yRegionIndex = row;
                cell.row = row;
                cell.column = column;
                cell.diagonal = row == column;
                cell.blank = cell.diagonal && m_diagonalMode == QStringLiteral("blank");
                cell.dataRole = cell.diagonal ? QStringLiteral("vs")
                                              : (row < column ? QStringLiteral("primary") : QStringLiteral("control"));
                const QString xLabel = regions[column].toMap().value(QStringLiteral("label")).toString();
                const QString yLabel = regions[row].toMap().value(QStringLiteral("label")).toString();
                cell.label = cell.diagonal ? xLabel : QStringLiteral("%1 × %2").arg(xLabel, yLabel);
                append(cell);
            }
        }
    }

    for (auto it = existing.begin(); it != existing.end(); ++it) if (it.value()) it.value()->deleteLater();
    m_cells = std::move(next);
    m_activeCellIndex = m_cells.isEmpty() ? -1 : std::clamp(m_activeCellIndex, 0, static_cast<int>(m_cells.size()) - 1);
    for (CellSpec& cell : m_cells) {
        openMapForCell(cell);
        applyCellRegion(cell);
        applyCellMode(cell);
    }
    updateCellModel();
    emit cellsChanged();
    emit activeCellChanged();
}

void TabSession::openMapForCell(CellSpec& cell) {
    if (!cell.controller || cell.mapIndex < 0 || cell.mapIndex >= m_maps.size()) return;
    const MapSpec& map = m_maps[cell.mapIndex];
    if (!map.primaryPath.isEmpty() && cell.controller->filePath() != map.primaryPath) {
        cell.controller->openFile(QUrl::fromUserInput(map.primaryPath));
        return;
    }
    if (!map.primaryPath.isEmpty() && cell.controller->chromosomeNames().isEmpty()) return;
    if (!map.controlPath.isEmpty() && cell.controller->controlFilePath() != map.controlPath)
        cell.controller->openControlFile(QUrl::fromUserInput(map.controlPath));
}

void TabSession::applyCellRegion(CellSpec& cell) {
    if (!cell.controller || cell.controller->chromosomeNames().isEmpty()) return;
    const QVariantList regions = m_regionSet->entries();
    if ((m_type == Type::MultiRegion || m_type == Type::MapRegion) && cell.regionIndex >= 0 && cell.regionIndex < regions.size()) {
        const QVariantMap region = regions[cell.regionIndex].toMap();
        cell.controller->setViewRegion(region.value(QStringLiteral("chrX")).toString(),
                                       region.value(QStringLiteral("x0")).toLongLong(), region.value(QStringLiteral("x1")).toLongLong(),
                                       region.value(QStringLiteral("chrY")).toString(),
                                       region.value(QStringLiteral("y0")).toLongLong(), region.value(QStringLiteral("y1")).toLongLong());
    } else if (m_type == Type::Pairwise && cell.xRegionIndex >= 0 && cell.yRegionIndex >= 0 &&
               cell.xRegionIndex < regions.size() && cell.yRegionIndex < regions.size()) {
        const QVariantMap x = regions[cell.xRegionIndex].toMap();
        const QVariantMap y = regions[cell.yRegionIndex].toMap();
        cell.controller->setViewRegion(x.value(QStringLiteral("chr")).toString(),
                                       x.value(QStringLiteral("start")).toLongLong(), x.value(QStringLiteral("end")).toLongLong(),
                                       y.value(QStringLiteral("chr")).toString(),
                                       y.value(QStringLiteral("start")).toLongLong(), y.value(QStringLiteral("end")).toLongLong());
    }
}

void TabSession::applyCellMode(CellSpec& cell) {
    if (!cell.controller || m_type != Type::Pairwise) return;
    if (cell.dataRole == QStringLiteral("primary")) cell.controller->setMatrixType(QStringLiteral("observed"));
    else if (cell.dataRole == QStringLiteral("control") && cell.controller->controlReady())
        cell.controller->setMatrixType(QStringLiteral("control"));
    else if (cell.dataRole == QStringLiteral("vs") && cell.controller->controlReady())
        cell.controller->setMatrixType(QStringLiteral("vs"));
}

void TabSession::updateCellModel() {
    m_cellModel.clear();
    m_cellModel.reserve(m_cells.size());
    for (int index = 0; index < m_cells.size(); ++index) {
        const CellSpec& cell = m_cells[index];
        QVariantMap item;
        item[QStringLiteral("index")] = index;
        item[QStringLiteral("key")] = cell.key;
        item[QStringLiteral("controller")] = QVariant::fromValue(cell.controller.data());
        item[QStringLiteral("mapIndex")] = cell.mapIndex;
        item[QStringLiteral("regionIndex")] = cell.regionIndex;
        item[QStringLiteral("xRegionIndex")] = cell.xRegionIndex;
        item[QStringLiteral("yRegionIndex")] = cell.yRegionIndex;
        item[QStringLiteral("row")] = cell.row;
        item[QStringLiteral("column")] = cell.column;
        item[QStringLiteral("label")] = cell.label;
        item[QStringLiteral("dataRole")] = cell.dataRole;
        item[QStringLiteral("diagonal")] = cell.diagonal;
        item[QStringLiteral("blank")] = cell.blank;
        item[QStringLiteral("selected")] = index == m_activeCellIndex;
        item[QStringLiteral("flipped")] = cell.mapIndex >= 0 && cell.mapIndex < m_maps.size()
            ? m_maps[cell.mapIndex].flipped : false;
        m_cellModel.push_back(item);
    }
}

QString TabSession::pathFromUrl(const QUrl& url) {
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

int TabSession::mapIndexForCell(int cellIndex) const {
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return 0;
    return std::clamp(m_cells[cellIndex].mapIndex, 0, std::max(0, static_cast<int>(m_maps.size()) - 1));
}

void TabSession::setPrimaryFile(int cellIndex, const QUrl& url) {
    const QString path = pathFromUrl(url);
    if (path.isEmpty()) return;
    const int mapIndex = mapIndexForCell(cellIndex);
    m_maps[mapIndex].primaryPath = path;
    m_maps[mapIndex].label = QFileInfo(path).fileName();
    if (m_type == Type::Single) setTitle(m_maps[mapIndex].label);
    for (CellSpec& cell : m_cells) if (cell.mapIndex == mapIndex) openMapForCell(cell);
    rebuildCells();
}

void TabSession::setControlFile(int cellIndex, const QUrl& url) {
    const QString path = pathFromUrl(url);
    if (path.isEmpty()) return;
    const int mapIndex = mapIndexForCell(cellIndex);
    m_maps[mapIndex].controlPath = path;
    for (CellSpec& cell : m_cells) if (cell.mapIndex == mapIndex) openMapForCell(cell);
    emit cellsChanged();
}

bool TabSession::loadRegions(const QUrl& url, const QString& requestedFormat) {
    QString format = requestedFormat.trimmed().toLower();
    if (format.isEmpty()) {
        if (m_type == Type::Pairwise) format = url.toString().contains(QStringLiteral("bedpe"), Qt::CaseInsensitive)
            ? QStringLiteral("bedpe-as-bed") : QStringLiteral("bed");
        else format = QStringLiteral("bedpe");
    }
    bool loaded = false;
    if (format == QStringLiteral("bed")) loaded = m_regionSet->loadBed(url);
    else if (format == QStringLiteral("bedpe-as-bed")) loaded = m_regionSet->loadBedpeAsBed(url);
    else loaded = m_regionSet->loadBedpe(url);
    if (!loaded) emit errorOccurred(m_regionSet->errorString());
    return loaded;
}

QString TabSession::resolvedScope(const QString& requestedScope) const {
    QString scope = requestedScope.trimmed().toLower();
    if (scope.isEmpty() || scope == QStringLiteral("default")) {
        if (m_type == Type::MultiMap || m_type == Type::Rotated45 || m_type == Type::Bullseye ||
            m_type == Type::Virtual4C || m_type == Type::Processing) return QStringLiteral("cell");
        if (m_type == Type::MapRegion) return QStringLiteral("map");
        return QStringLiteral("tab");
    }
    return scope;
}

QList<HicDataController*> TabSession::targetControllers(const QString& requestedScope) const {
    QList<HicDataController*> targets;
    const QString scope = resolvedScope(requestedScope.isEmpty() ? m_layerScope : requestedScope);
    if (m_activeCellIndex < 0 || m_activeCellIndex >= m_cells.size()) return targets;
    const CellSpec& active = m_cells[m_activeCellIndex];
    for (const CellSpec& cell : m_cells) {
        if (!cell.controller) continue;
        bool include = scope == QStringLiteral("tab");
        if (scope == QStringLiteral("cell")) include = cell.key == active.key;
        else if (scope == QStringLiteral("map")) include = cell.mapIndex == active.mapIndex;
        else if (scope == QStringLiteral("region")) {
            if (m_type == Type::Pairwise)
                include = cell.xRegionIndex == active.xRegionIndex || cell.yRegionIndex == active.yRegionIndex;
            else include = cell.regionIndex == active.regionIndex;
        }
        if (include && !targets.contains(cell.controller)) targets.push_back(cell.controller);
    }
    return targets;
}

void TabSession::loadTrack(const QUrl& url, const QString& scope) {
    loadTrackFromPath(pathFromUrl(url), scope);
}

void TabSession::loadTrackFromPath(const QString& pathOrUrl, const QString& scope) {
    for (HicDataController* controller : targetControllers(scope)) controller->loadTrackFromPath(pathOrUrl);
}

void TabSession::loadTrackResource(const QString& resourceId, const QString& scope) {
    for (HicDataController* controller : targetControllers(scope)) controller->loadTrackResource(resourceId);
}

void TabSession::loadAnnotations(const QUrl& url, const QString& scope) {
    const QString path = pathFromUrl(url);
    PooledAnnotationResult pooled = DatasetRegistry::instance()->loadAnnotations(path);
    if (!pooled.data) {
        emit errorOccurred(pooled.error);
        return;
    }
    for (HicDataController* controller : targetControllers(scope)) controller->loadAnnotationResource(pooled.id);
}

void TabSession::loadAnnotationResource(const QString& resourceId, const QString& scope) {
    for (HicDataController* controller : targetControllers(scope)) controller->loadAnnotationResource(resourceId);
}

bool TabSession::cellsShareNavigation(const CellSpec& source, const CellSpec& target) const {
    if (source.key == target.key) return false;
    if (m_type == Type::MultiMap) return true;
    if (m_type == Type::Rotated45 || m_type == Type::Bullseye ||
        m_type == Type::Virtual4C || m_type == Type::Processing) return true;
    if (m_type == Type::MapRegion) return source.regionIndex >= 0 && source.regionIndex == target.regionIndex;
    return false;
}

void TabSession::notifyViewportInteracted(int cellIndex) {
    if (!m_linkNavigation || cellIndex < 0 || cellIndex >= m_cells.size()) return;
    const CellSpec& source = m_cells[cellIndex];
    if (!source.controller) return;
    for (CellSpec& target : m_cells) {
        if (target.controller && cellsShareNavigation(source, target))
            target.controller->syncViewFrom(source.controller, m_linkColorScale);
    }
}

void TabSession::setMapFlipped(int mapIndex, bool flipped) {
    if (mapIndex < 0 || mapIndex >= m_maps.size() || m_maps[mapIndex].flipped == flipped) return;
    m_maps[mapIndex].flipped = flipped;
    updateCellModel();
    emit cellsChanged();
}

void TabSession::updateBullseyeFromFractions(int cellIndex, double xFraction, double yFraction) {
    if (m_bullseyePinned || cellIndex < 0 || cellIndex >= m_cells.size() || !m_cells[cellIndex].controller) return;
    HicDataController* source = m_cells[cellIndex].controller;
    const int resolution = std::max(1, source->resolution());
    m_bullseyeCenterX = ((source->x0() + static_cast<qint64>((source->x1() - source->x0()) *
                            std::clamp(xFraction, 0.0, 1.0))) / resolution) * resolution;
    m_bullseyeCenterY = ((source->y0() + static_cast<qint64>((source->y1() - source->y0()) *
                            std::clamp(yFraction, 0.0, 1.0))) / resolution) * resolution;
    emit analysisSettingsChanged();
}

QString TabSession::createVirtual4CTrack(int cellIndex, const QString& requestedName, const QString& scope) {
    if (cellIndex < 0 || cellIndex >= m_cells.size() || !m_cells[cellIndex].controller) return {};
    HicDataController* source = m_cells[cellIndex].controller;
    const int resolution = std::max(1, source->resolution());
    const qint64 anchor = (m_virtual4CAnchor / resolution) * resolution;
    const bool anchorOnX = m_virtual4CAxis == QStringLiteral("column");
    const qint64 targetStart = anchorOnX ? source->y0() : source->x0();
    const qint64 targetEnd = anchorOnX ? source->y1() : source->x1();
    const QString targetChromosome = anchorOnX ? source->chrY() : source->chrX();
    const auto values = MatrixAnalysis::virtual4C(source->analysisRecordsSnapshot(), resolution, anchor,
                                                   targetStart, targetEnd,
                                                   source->chrX() == source->chrY(), anchorOnX);
    QVector<GenomicTrackFeature> features;
    features.reserve(static_cast<qsizetype>(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        GenomicTrackFeature feature;
        feature.chr = targetChromosome;
        feature.start = targetStart + static_cast<qint64>(index) * resolution;
        feature.end = feature.start + resolution;
        feature.name = QStringLiteral("Virtual 4C");
        feature.value = values[index];
        features.push_back(feature);
    }
    QVariantMap provenance;
    provenance[QStringLiteral("kind")] = QStringLiteral("virtual-4c");
    provenance[QStringLiteral("source")] = source->filePath();
    provenance[QStringLiteral("anchorChromosome")] = anchorOnX ? source->chrX() : source->chrY();
    provenance[QStringLiteral("targetChromosome")] = targetChromosome;
    provenance[QStringLiteral("axis")] = m_virtual4CAxis;
    provenance[QStringLiteral("anchor")] = anchor;
    provenance[QStringLiteral("resolution")] = resolution;
    provenance[QStringLiteral("normalization")] = source->norm();
    const QString name = requestedName.trimmed().isEmpty()
        ? QStringLiteral("Virtual 4C · %1:%2 · %3 bp")
              .arg(anchorOnX ? source->chrX() : source->chrY()).arg(anchor).arg(resolution)
        : requestedName.trimmed();
    const PooledTrackResult result = DatasetRegistry::instance()->createDerivedTrack(name, features, provenance);
    if (!result.data) {
        emit errorOccurred(result.error);
        return {};
    }
    setActiveCellIndex(cellIndex);
    loadTrackResource(result.id, scope);
    return result.id;
}

void TabSession::addScopedAnnotation(int cellIndex, double xStartFraction, double yStartFraction,
                                     double xEndFraction, double yEndFraction, const QString& scope) {
    if (cellIndex < 0 || cellIndex >= m_cells.size() || !m_cells[cellIndex].controller) return;
    setActiveCellIndex(cellIndex);
    HicDataController* source = m_cells[cellIndex].controller;
    const double xa = std::clamp(std::min(xStartFraction, xEndFraction), 0.0, 1.0);
    const double xb = std::clamp(std::max(xStartFraction, xEndFraction), 0.0, 1.0);
    const double ya = std::clamp(std::min(yStartFraction, yEndFraction), 0.0, 1.0);
    const double yb = std::clamp(std::max(yStartFraction, yEndFraction), 0.0, 1.0);
    PooledAnnotation annotation;
    annotation.id = QStringLiteral("hand_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    annotation.name = QStringLiteral("Hand annotation");
    annotation.chr1 = source->chrX();
    annotation.chr2 = source->chrY();
    annotation.start1 = source->x0() + static_cast<qint64>((source->x1() - source->x0()) * xa);
    annotation.end1 = source->x0() + static_cast<qint64>((source->x1() - source->x0()) * xb);
    annotation.start2 = source->y0() + static_cast<qint64>((source->y1() - source->y0()) * ya);
    annotation.end2 = source->y0() + static_cast<qint64>((source->y1() - source->y0()) * yb);
    if (annotation.end1 <= annotation.start1 || annotation.end2 <= annotation.start2) return;
    const PooledAnnotationResult custom = DatasetRegistry::instance()->createCustomAnnotations(
        QStringLiteral("Custom annotation"), {annotation});
    for (HicDataController* target : targetControllers(scope)) target->loadAnnotationResource(custom.id);
}

void TabSession::propagateColor(HicDataController* source) {
    if (!source || !m_linkColorScale) return;
    const auto sourceIt = std::find_if(m_cells.cbegin(), m_cells.cend(), [source](const CellSpec& cell) {
        return cell.controller == source;
    });
    if (sourceIt == m_cells.cend()) return;
    for (CellSpec& target : m_cells) {
        if (target.controller && (isMultiSourceType() || cellsShareNavigation(*sourceIt, target)))
            target.controller->syncColorFrom(source);
    }
}

QVariantMap TabSession::state() const {
    QVariantMap result;
    result[QStringLiteral("schemaVersion")] = 3;
    result[QStringLiteral("tabType")] = type();
    result[QStringLiteral("title")] = m_title;
    result[QStringLiteral("layoutColumns")] = m_layoutColumns;
    result[QStringLiteral("transposed")] = m_transposed;
    result[QStringLiteral("diagonalMode")] = m_diagonalMode;
    result[QStringLiteral("layerScope")] = m_layerScope;
    result[QStringLiteral("linkNavigation")] = m_linkNavigation;
    result[QStringLiteral("linkCrosshair")] = m_linkCrosshair;
    result[QStringLiteral("linkColorScale")] = m_linkColorScale;
    result[QStringLiteral("activeCellIndex")] = m_activeCellIndex;
    result[QStringLiteral("maps")] = maps();
    result[QStringLiteral("regions")] = m_regionSet->state();
    QVariantMap analysis;
    analysis[QStringLiteral("paneHeight")] = m_analysisPaneHeight;
    analysis[QStringLiteral("diagonalMaxDistance")] = m_diagonalMaxDistance;
    analysis[QStringLiteral("bullseyeCenterX")] = m_bullseyeCenterX;
    analysis[QStringLiteral("bullseyeCenterY")] = m_bullseyeCenterY;
    analysis[QStringLiteral("bullseyeRadiusBins")] = m_bullseyeRadiusBins;
    analysis[QStringLiteral("bullseyePinned")] = m_bullseyePinned;
    analysis[QStringLiteral("virtual4CAnchor")] = m_virtual4CAnchor;
    analysis[QStringLiteral("virtual4CAxis")] = m_virtual4CAxis;
    analysis[QStringLiteral("processingOperator")] = m_processingOperator;
    analysis[QStringLiteral("processingParameter")] = m_processingParameter;
    analysis[QStringLiteral("processingThreshold")] = m_processingThreshold;
    analysis[QStringLiteral("processingMaximumBins")] = m_processingMaximumBins;
    result[QStringLiteral("analysis")] = analysis;
    QVariantList cellStates;
    for (const CellSpec& cell : m_cells) {
        if (!cell.controller) continue;
        QVariantMap item;
        item[QStringLiteral("key")] = cell.key;
        item[QStringLiteral("state")] = cell.controller->sessionState();
        cellStates.push_back(item);
    }
    result[QStringLiteral("cells")] = cellStates;
    return result;
}

bool TabSession::restoreState(const QVariantMap& value) {
    initialize(value.value(QStringLiteral("tabType"), QStringLiteral("single")).toString());
    m_title = value.value(QStringLiteral("title"), typeLabel()).toString();
    m_layoutColumns = value.value(QStringLiteral("layoutColumns"), 2).toInt();
    m_transposed = value.value(QStringLiteral("transposed"), false).toBool();
    m_diagonalMode = value.value(QStringLiteral("diagonalMode"), QStringLiteral("split")).toString();
    m_layerScope = value.value(QStringLiteral("layerScope"), QStringLiteral("default")).toString();
    m_linkNavigation = value.value(QStringLiteral("linkNavigation"), isMultiSourceType()).toBool();
    m_linkCrosshair = value.value(QStringLiteral("linkCrosshair"), true).toBool();
    m_linkColorScale = value.value(QStringLiteral("linkColorScale"), false).toBool();
    m_pendingCellStates.clear();
    for (const QVariant& cellValue : value.value(QStringLiteral("cells")).toList()) {
        const QVariantMap cellState = cellValue.toMap();
        m_pendingCellStates.insert(cellState.value(QStringLiteral("key")).toString(),
                                   cellState.value(QStringLiteral("state")).toMap());
    }
    if (m_pendingCellStates.isEmpty() && value.contains(QStringLiteral("chrX")))
        m_pendingCellStates.insert(QStringLiteral("single"), value);
    const QVariantList mapValues = value.value(QStringLiteral("maps")).toList();
    if (!mapValues.isEmpty()) {
        m_maps.clear();
        for (const QVariant& mapValue : mapValues) {
            const QVariantMap mapState = mapValue.toMap();
            MapSpec map;
            map.id = mapState.value(QStringLiteral("id")).toString();
            if (map.id.isEmpty()) map.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            map.primaryPath = mapState.value(QStringLiteral("primaryPath")).toString();
            map.controlPath = mapState.value(QStringLiteral("controlPath")).toString();
            map.label = mapState.value(QStringLiteral("label")).toString();
            map.flipped = mapState.value(QStringLiteral("flipped"), false).toBool();
            m_maps.push_back(map);
        }
    } else if (value.contains(QStringLiteral("filePath"))) {
        m_maps[0].primaryPath = value.value(QStringLiteral("filePath")).toString();
        m_maps[0].controlPath = value.value(QStringLiteral("controlFilePath")).toString();
    }
    const QVariantMap regionState = value.value(QStringLiteral("regions")).toMap();
    if (!regionState.isEmpty() && !m_regionSet->restoreState(regionState)) return false;
    m_activeCellIndex = value.value(QStringLiteral("activeCellIndex"), 0).toInt();
    const QVariantMap analysis = value.value(QStringLiteral("analysis")).toMap();
    m_analysisPaneHeight = std::clamp(analysis.value(QStringLiteral("paneHeight"), 260).toInt(), 100, 1200);
    m_diagonalMaxDistance = std::clamp<qint64>(
        analysis.value(QStringLiteral("diagonalMaxDistance"), 2000000).toLongLong(), 1000, 1000000000LL);
    m_bullseyeCenterX = std::max<qint64>(0, analysis.value(QStringLiteral("bullseyeCenterX"), 0).toLongLong());
    m_bullseyeCenterY = std::max<qint64>(0, analysis.value(QStringLiteral("bullseyeCenterY"), 0).toLongLong());
    m_bullseyeRadiusBins = std::clamp(analysis.value(QStringLiteral("bullseyeRadiusBins"), 12).toInt(), 1, 100);
    m_bullseyePinned = analysis.value(QStringLiteral("bullseyePinned"), false).toBool();
    m_virtual4CAnchor = std::max<qint64>(0, analysis.value(QStringLiteral("virtual4CAnchor"), 0).toLongLong());
    m_virtual4CAxis = analysis.value(QStringLiteral("virtual4CAxis"), QStringLiteral("row")).toString() ==
            QStringLiteral("column") ? QStringLiteral("column") : QStringLiteral("row");
    m_processingOperator = analysis.value(QStringLiteral("processingOperator"), QStringLiteral("gradient-magnitude")).toString();
    const double restoredProcessingParameter =
        analysis.value(QStringLiteral("processingParameter"), 1.0).toDouble();
    m_processingParameter = std::isfinite(restoredProcessingParameter)
        ? std::clamp(restoredProcessingParameter, 0.1, 100.0) : 1.0;
    if (m_processingOperator == QStringLiteral("gabor"))
        m_processingParameter = std::min(12.0, m_processingParameter);
    const double restoredProcessingThreshold =
        analysis.value(QStringLiteral("processingThreshold"), 0.0).toDouble();
    m_processingThreshold = std::isfinite(restoredProcessingThreshold)
        ? restoredProcessingThreshold : 0.0;
    m_processingMaximumBins = analysis.value(QStringLiteral("processingMaximumBins"), 512).toInt() > 512 ? 1024 : 512;
    rebuildCells();
    emit titleChanged();
    emit structureChanged();
    return true;
}

bool TabSession::exportState(const QUrl& url) const {
    QSaveFile file(pathFromUrl(url));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(QJsonDocument::fromVariant(state()).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool TabSession::importState(const QUrl& url) {
    QFile file(pathFromUrl(url));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() && restoreState(document.object().toVariantMap());
}
