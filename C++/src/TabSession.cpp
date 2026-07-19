#include "TabSession.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

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
    return Type::Single;
}

QString TabSession::typeName(Type type) {
    switch (type) {
    case Type::MultiMap: return QStringLiteral("multi-map");
    case Type::MultiRegion: return QStringLiteral("multi-region");
    case Type::MapRegion: return QStringLiteral("map-region");
    case Type::Pairwise: return QStringLiteral("pairwise");
    default: return QStringLiteral("single");
    }
}

QString TabSession::typeDisplayName(Type type) {
    switch (type) {
    case Type::MultiMap: return QStringLiteral("Multi-map");
    case Type::MultiRegion: return QStringLiteral("Multi-region");
    case Type::MapRegion: return QStringLiteral("Maps × regions");
    case Type::Pairwise: return QStringLiteral("Pairwise regions");
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

void TabSession::initialize(const QString& value) {
    m_initializing = true;
    const Type nextType = parseType(value);
    for (CellSpec& cell : m_cells) if (cell.controller) cell.controller->deleteLater();
    m_cells.clear();
    m_cellModel.clear();
    m_regionSet->clear();
    m_maps.clear();
    m_type = nextType;
    ensureMapCount(nextType == Type::MultiMap || nextType == Type::MapRegion ? 2 : 1);
    m_layoutColumns = nextType == Type::Pairwise ? 10 : 2;
    m_transposed = false;
    m_diagonalMode = QStringLiteral("split");
    m_layerScope = QStringLiteral("default");
    m_linkNavigation = nextType == Type::MultiMap || nextType == Type::MapRegion;
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
    if (m_type != Type::MultiMap && m_type != Type::MapRegion) return;
    ensureMapCount(m_maps.size() + 1);
    rebuildCells();
}

void TabSession::removeMap(int mapIndex) {
    if ((m_type != Type::MultiMap && m_type != Type::MapRegion) || m_maps.size() <= 1 ||
        mapIndex < 0 || mapIndex >= m_maps.size()) return;
    m_maps.removeAt(mapIndex);
    for (int i = 0; i < m_maps.size(); ++i) if (m_maps[i].label.startsWith(QStringLiteral("Map ")))
        m_maps[i].label = QStringLiteral("Map %1").arg(i + 1);
    rebuildCells();
}

HicDataController* TabSession::createController(const QString& key) {
    auto* controller = new HicDataController(this);
    controller->setMinimapEnabled(m_type == Type::Single);
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
    } else if (m_type == Type::MultiMap) {
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
        if (m_type == Type::MultiMap) return QStringLiteral("cell");
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
        if (target.controller && (m_type == Type::MultiMap || cellsShareNavigation(*sourceIt, target)))
            target.controller->syncColorFrom(source);
    }
}

QVariantMap TabSession::state() const {
    QVariantMap result;
    result[QStringLiteral("schemaVersion")] = 2;
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
    m_linkNavigation = value.value(QStringLiteral("linkNavigation"), m_type == Type::MultiMap || m_type == Type::MapRegion).toBool();
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
            m_maps.push_back(map);
        }
    } else if (value.contains(QStringLiteral("filePath"))) {
        m_maps[0].primaryPath = value.value(QStringLiteral("filePath")).toString();
        m_maps[0].controlPath = value.value(QStringLiteral("controlFilePath")).toString();
    }
    const QVariantMap regionState = value.value(QStringLiteral("regions")).toMap();
    if (!regionState.isEmpty() && !m_regionSet->restoreState(regionState)) return false;
    m_activeCellIndex = value.value(QStringLiteral("activeCellIndex"), 0).toInt();
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
