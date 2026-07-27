#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>

#include <limits>
#include <cmath>

#include "HicDataController.h"
#include "HicTileCache.h"
#include "DatasetRegistry.h"
#include "RegionSetModel.h"
#include "TabSession.h"
#include "MatrixAnalysis.h"

namespace {
bool require(bool condition, const char* message) {
    if (!condition) qCritical("Smoke test failed: %s", message);
    return condition;
}

bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("CARTON smoke test"));
    QGuiApplication::setOrganizationName(QStringLiteral("CARTON tests"));

    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory")) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());

    HicTileCache cache(3, 2);
    for (int i = 0; i < 3; ++i) {
        HicTile tile;
        tile.key.filePath = "test";
        tile.key.chrX = std::to_string(i);
        tile.records.resize(2);
        cache.put(std::move(tile));
    }
    if (!require(cache.tileCount() <= 2 && cache.recordCount() <= 3, "cache bounds")) return 1;
    cache.setLimits(1, 1);
    if (!require(cache.tileCount() <= 1 && cache.recordCount() <= 1, "cache trims after limit change")) return 1;

    HicDataController controller;
    const QVariantList matrixOptions = controller.matrixTypeOptions();
    bool hasCosineObserved = false;
    bool hasCosineOe = false;
    bool exposedControlModeWithoutControl = false;
    for (const QVariant& value : matrixOptions) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        hasCosineObserved |= id == QStringLiteral("cosineobserved");
        hasCosineOe |= id == QStringLiteral("cosineoe");
        exposedControlModeWithoutControl |= id.contains(QStringLiteral("control")) ||
                                            id.endsWith(QStringLiteral("vs"));
    }
    if (!require(hasCosineObserved && hasCosineOe && !exposedControlModeWithoutControl,
                 "matrix options organize cosine modes and hide A/B modes until control is ready")) return 1;

    std::vector<contactRecord> similarityInput;
    auto addSimilarityValue = [&similarityInput](int x, int y, float value) {
        contactRecord record;
        record.binX = x * 100;
        record.binY = y * 100;
        record.counts = value;
        similarityInput.push_back(record);
    };
    addSimilarityValue(0, 0, 1.0f);
    addSimilarityValue(1, 0, 2.0f);
    addSimilarityValue(2, 0, 3.0f);
    addSimilarityValue(1, 1, 4.0f);
    addSimilarityValue(2, 2, 1.0f);
    const std::vector<contactRecord> pearson = MatrixAnalysis::similarity(
        similarityInput, MatrixAnalysis::SimilarityMetric::Pearson, 100,
        0, 300, 0, 300, 0, 300);
    const std::vector<contactRecord> cosine = MatrixAnalysis::similarity(
        similarityInput, MatrixAnalysis::SimilarityMetric::Cosine, 100,
        0, 300, 0, 300, 0, 300);
    auto similarityAt = [](const std::vector<contactRecord>& records, int x, int y) {
        for (const contactRecord& record : records)
            if (record.binX == x && record.binY == y) return record.counts;
        return std::numeric_limits<float>::quiet_NaN();
    };
    if (!require(pearson.size() == 6 && cosine.size() == 6,
                 "similarity computes one symmetric triangle without redundant cells")) return 1;
    if (!require(std::abs(similarityAt(pearson, 100, 0) + 0.5f) < 0.0001f,
                 "Pearson mean-centers O/E-style row vectors")) return 1;
    if (!require(std::abs(similarityAt(cosine, 100, 0) -
                          static_cast<float>(10.0 / std::sqrt(280.0))) < 0.0001f,
                 "cosine uses uncentered row vectors")) return 1;

    const QString cytobands = temporary.filePath(QStringLiteral("cytoBand.txt"));
    if (!require(writeFile(cytobands,
            "chr1\t0\t100\tp11\tgneg\nchr1\t100\t180\tp12\tgpos50\nchr1\t180\t220\tacen\tacen\n"),
            "write cytobands")) return 1;
    controller.loadCytobands(QUrl::fromLocalFile(cytobands));
    if (!require(controller.cytobandCount() == 3, "cytoband parsing")) return 1;
    controller.clearCytobands();
    if (!require(controller.cytobandCount() == 0, "cytoband clearing")) return 1;

    const QString track = temporary.filePath(QStringLiteral("signal.bedgraph"));
    if (!require(writeFile(track, "chr1\t0\t100\t2.5\nchr1\t100\t200\t-1.5\n"), "write track")) return 1;
    controller.loadTrackFromPath(track);
    if (!require(controller.trackCount() == 1, "track parsing")) return 1;
    if (!require(controller.trackSummaries().front().toMap().value("height").toInt() == 100 &&
                 controller.visibleTrackHeight() == 100,
                 "default track height")) return 1;
    if (!require(controller.tracksModel()->rowCount() == 1 && controller.annotationsModel()->rowCount() == 1,
                 "workspace list models")) return 1;
    controller.setTrackVisible(0, false);
    controller.setTrackCollapsed(0, true);
    controller.setTrackHeight(0, 1200);
    if (!require(controller.trackSummaries().front().toMap().value("height").toInt() == 1200 &&
                 controller.visibleTrackHeight() == 0,
                 "uncapped track height")) return 1;
    controller.setTrackHeight(0, 96);
    const QVariantMap summary = controller.trackSummaries().front().toMap();
    if (!require(!summary.value("visible").toBool() && summary.value("collapsed").toBool() &&
                 summary.value("height").toInt() == 96 && summary.value("format").toString() == QStringLiteral("bedGraph") &&
                 summary.value("renderMode").toString() == QStringLiteral("signal") &&
                 summary.value("binSize").toLongLong() == 0, "track display state")) return 1;
    controller.setWorkspaceSearch(QStringLiteral("signal"));
    if (!require(controller.searchResultsModel()->rowCount() >= 1, "workspace search model")) return 1;

    controller.setCacheLimitMB(64);
    controller.setColorPercentile(97.5);
    controller.setZeroTransparent(true);
    if (!require(controller.cacheLimitMB() == 64 && controller.colorPercentile() == 97.5 &&
                 controller.zeroTransparent(), "display and cache settings")) return 1;

    const QString matrix = temporary.filePath(QStringLiteral("visible.tsv"));
    if (!require(controller.exportVisibleMatrix(QUrl::fromLocalFile(matrix)), "matrix export")) return 1;
    QFile matrixFile(matrix);
    if (!require(matrixFile.open(QIODevice::ReadOnly) && matrixFile.readAll().startsWith("chrX\tstartX"),
                 "matrix export contents")) return 1;

    const QString png = temporary.filePath(QStringLiteral("figure.png"));
    if (!require(controller.exportFigurePng(QUrl::fromLocalFile(png), 640, 480), "PNG export")) return 1;
    if (!require(!QImage(png).isNull(), "PNG is readable")) return 1;

    // Regression test: many .hic files (e.g. classic Juicer output) name
    // chromosomes without a "chr" prefix ("1") while bedGraph/BED/wig/bigWig
    // tracks almost always use UCSC-style names ("chr1"). Track features must
    // still be considered visible in that case instead of silently matching
    // nothing.
    HicDataController prefixController;
    prefixController.setResolution(100);
    prefixController.setChrX(QStringLiteral("1"));
    prefixController.setChrY(QStringLiteral("1"));
    prefixController.setX0(0);
    prefixController.setX1(1000);
    prefixController.setY0(0);
    prefixController.setY1(1000);
    const QString prefixTrack = temporary.filePath(QStringLiteral("signal2.bedgraph"));
    if (!require(writeFile(prefixTrack, "chr1\t0\t100\t2.5\nchr1\t100\t200\t-1.5\n"), "write chr-prefixed track"))
        return 1;
    prefixController.loadTrackFromPath(prefixTrack);
    if (!require(prefixController.trackCount() == 1, "chr-prefixed track parsing")) return 1;
    const QVariantList prefixSegments = prefixController.visibleTrackSegments(true);
    if (!require(prefixSegments.size() == 2, "chr-prefix-insensitive track visibility")) return 1;
    if (!require(prefixSegments.front().toMap().value("kind").toString() == QStringLiteral("signal"),
                 "bedGraph uses signal rendering")) return 1;

    QByteArray denseContents;
    for (int i = 0; i < 1000; ++i) {
        denseContents += QByteArray("chr1\t") + QByteArray::number(i) + '\t' + QByteArray::number(i + 1) + '\t' +
                         QByteArray::number(i % 10) + '\n';
    }
    const QString denseTrack = temporary.filePath(QStringLiteral("dense.bedgraph"));
    if (!require(writeFile(denseTrack, denseContents), "write dense bedGraph")) return 1;
    prefixController.loadTrackFromPath(denseTrack);
    const QVariantList pixelSegments = prefixController.visibleTrackSegmentsForPixels(true, 100);
    int denseSegmentCount = 0;
    for (const QVariant& value : pixelSegments) {
        if (value.toMap().value("trackIndex").toInt() == 1) ++denseSegmentCount;
    }
    if (!require(denseSegmentCount > 0 && denseSegmentCount <= 100, "dense signal is bounded by display pixels")) return 1;
    const QVariantMap mapBinnedSummary = prefixController.trackSummaries()[1].toMap();
    if (!require(mapBinnedSummary.value("binSize").toLongLong() == 0 &&
                 mapBinnedSummary.value("effectiveBinSize").toLongLong() == 100,
                 "quantitative tracks default to the Hi-C resolution")) return 1;

    prefixController.setTrackBinSize(1, 50);
    prefixController.setTrackReduction(1, QStringLiteral("min"));
    const QVariantList minimumSegments = prefixController.visibleTrackSegmentsForPixels(true, 1000);
    double firstMinimum = std::numeric_limits<double>::quiet_NaN();
    qint64 renderedBinSize = 0;
    for (const QVariant& value : minimumSegments) {
        const QVariantMap segment = value.toMap();
        if (segment.value("trackIndex").toInt() == 1) {
            firstMinimum = segment.value("value").toDouble();
            renderedBinSize = segment.value("renderedBinSize").toLongLong();
            break;
        }
    }
    prefixController.setTrackReduction(1, QStringLiteral("max"));
    const QVariantList maximumSegments = prefixController.visibleTrackSegmentsForPixels(true, 1000);
    double firstMaximum = std::numeric_limits<double>::quiet_NaN();
    for (const QVariant& value : maximumSegments) {
        const QVariantMap segment = value.toMap();
        if (segment.value("trackIndex").toInt() == 1) {
            firstMaximum = segment.value("value").toDouble();
            break;
        }
    }
    if (!require(renderedBinSize == 50 && firstMinimum == 0.0 && firstMaximum == 9.0,
                 "fixed genomic bins honor min/max windowing")) return 1;
    const QVariantMap denseSummary = prefixController.trackSummaries()[1].toMap();
    if (!require(denseSummary.value("binSize").toLongLong() == 50 &&
                 denseSummary.value("effectiveBinSize").toLongLong() == 50,
                 "fixed bin size is exposed in track summary")) return 1;

    const QString bedTrack = temporary.filePath(QStringLiteral("features.bed"));
    if (!require(writeFile(bedTrack, "chr1\t10\t30\tfeature-a\t500\n"), "write BED")) return 1;
    prefixController.loadTrackFromPath(bedTrack);
    const QVariantList mixedSegments = prefixController.visibleTrackSegmentsForPixels(true, 100);
    bool foundBedFeature = false;
    for (const QVariant& value : mixedSegments) {
        const QVariantMap segment = value.toMap();
        if (segment.value("trackIndex").toInt() == 2 && segment.value("kind").toString() == QStringLiteral("feature")) {
            foundBedFeature = true;
            break;
        }
    }
    if (!require(foundBedFeature, "BED uses interval feature rendering")) return 1;

    const QString inferredBedTrack = temporary.filePath(QStringLiteral("features.tsv"));
    if (!require(writeFile(inferredBedTrack, "chr1\t40\t60\n"), "write extensionless-style BED")) return 1;
    prefixController.loadTrackFromPath(inferredBedTrack);
    const QVariantMap inferredBedSummary = prefixController.trackSummaries().back().toMap();
    if (!require(inferredBedSummary.value("format").toString() == QStringLiteral("bed") &&
                 inferredBedSummary.value("renderMode").toString() == QStringLiteral("feature"),
                 "three-column text tracks infer BED rendering")) return 1;

    const PooledTrackResult pooledTrackA = DatasetRegistry::instance()->loadTrack(track);
    const PooledTrackResult pooledTrackB = DatasetRegistry::instance()->loadTrack(track);
    if (!require(pooledTrackA.data && pooledTrackA.data == pooledTrackB.data,
                 "track resources are pooled by canonical source")) return 1;

    QVector<GenomicTrackFeature> derivedFeatures;
    derivedFeatures.push_back({QStringLiteral("chr1"), 0, 100, QStringLiteral("v4c"), 3.5, QColor("#3478f6")});
    const PooledTrackResult derivedTrack = DatasetRegistry::instance()->createDerivedTrack(
        QStringLiteral("Synthetic virtual 4C"), derivedFeatures,
        {{QStringLiteral("kind"), QStringLiteral("virtual-4c")}});
    if (!require(derivedTrack.data && derivedTrack.data->derived &&
                 DatasetRegistry::instance()->trackById(derivedTrack.id).data == derivedTrack.data,
                 "derived tracks are pooled in memory")) return 1;
    HicDataController derivedConsumer;
    derivedConsumer.loadTrackResource(derivedTrack.id);
    if (!require(derivedConsumer.trackCount() == 1 &&
                 derivedConsumer.trackSummaries().front().toMap().value("resourceId").toString() == derivedTrack.id,
                 "derived pooled tracks load without a backing file")) return 1;

    prefixController.addAnnotationFromFractions(0.1, 0.1, 0.2, 0.2);
    const QVariantMap customLayer = prefixController.annotationLayerSummaries().front().toMap();
    const QString customResource = customLayer.value("resourceId").toString();
    if (!require(!customResource.isEmpty() && customResource.startsWith("annotation:custom:"),
                 "hand annotations create a custom registry resource")) return 1;
    HicDataController restoredAnnotationController;
    restoredAnnotationController.restoreSessionState(prefixController.sessionState(), false);
    if (!require(restoredAnnotationController.annotationLayerSummaries().front().toMap().value("resourceId").toString() == customResource,
                 "restored custom annotations reuse their pooled resource ID")) return 1;
    HicDataController annotationConsumer;
    annotationConsumer.loadAnnotationResource(customResource);
    const QString sharedResource = annotationConsumer.annotationLayerSummaries().back().toMap().value("resourceId").toString();
    annotationConsumer.setAnnotationLayerColor(1, QColor("#ff00ff"));
    const QVariantMap overriddenLayer = annotationConsumer.annotationLayerSummaries().back().toMap();
    if (!require(overriddenLayer.value("resourceId").toString() == customResource &&
                 overriddenLayer.value("colorOverride").toBool(),
                 "layer recoloring is a non-destructive display override")) return 1;
    annotationConsumer.clearAnnotationLayerColorOverride(1);
    annotationConsumer.clearAnnotationLayer(1);
    const QString forkedResource = annotationConsumer.annotationLayerSummaries().back().toMap().value("resourceId").toString();
    if (!require(sharedResource == customResource && forkedResource != customResource,
                 "editing a shared annotation resource uses copy-on-write")) return 1;

    const QString placementAnnotations = temporary.filePath(QStringLiteral("placement.bedpe"));
    if (!require(writeFile(placementAnnotations,
            "chr1\t100\t200\tchr1\t700\t800\tloop\t0\t#00ff00\n"),
            "write placement annotations")) return 1;
    HicDataController placementController;
    placementController.setResolution(100);
    placementController.setChrX(QStringLiteral("1"));
    placementController.setChrY(QStringLiteral("1"));
    placementController.setX0(0);
    placementController.setX1(1000);
    placementController.setY0(0);
    placementController.setY1(1000);
    placementController.loadAnnotationsFromPath(placementAnnotations);
    const int placementLayer = placementController.annotationLayerSummaries().size() - 1;
    if (!require(placementController.visibleAnnotations().size() == 2,
                 "intrachromosomal annotations render on both sides by default")) return 1;
    placementController.setAnnotationLayerPlacement(placementLayer, QStringLiteral("above"));
    const QVariantList aboveAnnotations = placementController.visibleAnnotations();
    if (!require(aboveAnnotations.size() == 1 &&
                 aboveAnnotations.front().toMap().value("x0").toLongLong() >
                     aboveAnnotations.front().toMap().value("y0").toLongLong(),
                 "annotation layers can render above the diagonal only")) return 1;
    placementController.setAnnotationLayerPlacement(placementLayer, QStringLiteral("below"));
    const QVariantList belowAnnotations = placementController.visibleAnnotations();
    if (!require(belowAnnotations.size() == 1 &&
                 belowAnnotations.front().toMap().value("x0").toLongLong() <
                     belowAnnotations.front().toMap().value("y0").toLongLong(),
                 "annotation layers can render below the diagonal only")) return 1;
    placementController.setAnnotationLayerColor(placementLayer, QColor("#123456"));
    if (!require(placementController.visibleAnnotations().front().toMap().value("color").value<QColor>() == QColor("#123456"),
                 "annotation color override replaces BEDPE colors")) return 1;
    HicDataController restoredPlacementController;
    restoredPlacementController.restoreSessionState(placementController.sessionState(), false);
    const QVariantMap restoredPlacementLayer = restoredPlacementController.annotationLayerSummaries().back().toMap();
    if (!require(restoredPlacementLayer.value("placement").toString() == QStringLiteral("below") &&
                 restoredPlacementLayer.value("colorOverride").toBool() &&
                 restoredPlacementLayer.value("color").value<QColor>() == QColor("#123456"),
                 "annotation placement and color override round-trip")) return 1;

    const QString regionsBed = temporary.filePath(QStringLiteral("regions.bed"));
    if (!require(writeFile(regionsBed,
            "chr1\t100\t200\tA\nchr1\t180\t300\tB\nchr2\t500\t600\tC\n"), "write region BED")) return 1;
    const QString regionsBedpe = temporary.filePath(QStringLiteral("regions.bedpe"));
    if (!require(writeFile(regionsBedpe,
            "chr1\t100\t200\tchr1\t900\t1000\tloop-a\n"
            "chr2\t500\t600\tchr3\t700\t800\tloop-b\n"), "write region BEDPE")) return 1;

    RegionSetModel regionSet;
    regionSet.setWindowSize(2000);
    if (!require(regionSet.loadBed(QUrl::fromLocalFile(regionsBed)) && regionSet.rowCount() == 2,
                 "BED regions merge overlaps")) return 1;
    const QVariantMap firstRegion = regionSet.entries().front().toMap();
    if (!require(firstRegion.value("end").toLongLong() - firstRegion.value("start").toLongLong() == 2000,
                 "single regions use a fixed recentered window")) return 1;
    if (!require(regionSet.loadBedpe(QUrl::fromLocalFile(regionsBedpe)) && regionSet.rowCount() == 2 &&
                 regionSet.entries().front().toMap().value("chrY").toString() == QStringLiteral("chr1"),
                 "BEDPE produces paired regions")) return 1;
    if (!require(regionSet.loadBedpeAsBed(QUrl::fromLocalFile(regionsBedpe)) && regionSet.rowCount() == 4 &&
                 regionSet.kind() == QStringLiteral("bedpe-as-bed"),
                 "BEDPE endpoints project to a merged BED region set")) return 1;
    RegionSetModel restoredProjection;
    if (!require(restoredProjection.restoreState(regionSet.state()) && restoredProjection.rowCount() == 4 &&
                 restoredProjection.kind() == QStringLiteral("bedpe-as-bed"),
                 "BEDPE projection mode round-trips")) return 1;

    TabSession multiMap;
    multiMap.initialize(QStringLiteral("multi-map"));
    multiMap.addMap();
    if (!require(multiMap.mapCount() == 3 && multiMap.cellCount() == 3,
                 "multi-map tabs create independent map cells")) return 1;
    multiMap.loadTrackFromPath(track);
    int multiMapTrackCells = 0;
    for (const QVariant& value : multiMap.cells()) {
        if (auto* cellController = value.toMap().value("controller").value<HicDataController*>();
            cellController && cellController->trackCount() == 1) ++multiMapTrackCells;
    }
    if (!require(multiMapTrackCells == 1, "multi-map layers default to the active cell")) return 1;

    TabSession multiRegion;
    multiRegion.initialize(QStringLiteral("multi-region"));
    if (!require(multiRegion.loadRegions(QUrl::fromLocalFile(regionsBedpe), QStringLiteral("bedpe")) &&
                 multiRegion.regionCount() == 2 && multiRegion.cellCount() == 2,
                 "multi-region tabs build one cell per BEDPE row")) return 1;
    multiRegion.loadTrackFromPath(track);
    int multiRegionTrackCells = 0;
    for (const QVariant& value : multiRegion.cells()) {
        if (auto* cellController = value.toMap().value("controller").value<HicDataController*>();
            cellController && cellController->trackCount() == 1) ++multiRegionTrackCells;
    }
    if (!require(multiRegionTrackCells == 2, "multi-region layers default to the whole tab")) return 1;

    TabSession mapRegion;
    mapRegion.initialize(QStringLiteral("map-region"));
    if (!require(mapRegion.loadRegions(QUrl::fromLocalFile(regionsBedpe), QStringLiteral("bedpe")) &&
                 mapRegion.cellCount() == 4 && mapRegion.rowCount() == 2 && mapRegion.columnCount() == 2,
                 "map-by-region tabs build the Cartesian product")) return 1;
    mapRegion.loadTrackFromPath(track);
    int mapScopedTrackCells = 0;
    for (const QVariant& value : mapRegion.cells()) {
        if (auto* cellController = value.toMap().value("controller").value<HicDataController*>();
            cellController && cellController->trackCount() == 1) ++mapScopedTrackCells;
    }
    if (!require(mapScopedTrackCells == 2, "map-by-region layers default to the active map")) return 1;
    mapRegion.addMap();
    if (!require(mapRegion.cellCount() == 6 && mapRegion.rowCount() == 2 && mapRegion.columnCount() == 3,
                 "map-by-region tabs grow by map columns")) return 1;
    mapRegion.setTransposed(true);
    if (!require(mapRegion.rowCount() == 3 && mapRegion.columnCount() == 2,
                 "map-by-region axes can be transposed")) return 1;

    TabSession pairwise;
    pairwise.initialize(QStringLiteral("pairwise"));
    if (!require(pairwise.loadRegions(QUrl::fromLocalFile(regionsBed), QStringLiteral("bed")) &&
                 pairwise.regionCount() == 2 && pairwise.cellCount() == 4,
                 "pairwise tabs build an N by N grid")) return 1;
    pairwise.setDiagonalMode(QStringLiteral("blank"));
    int blankCells = 0;
    for (const QVariant& value : pairwise.cells()) if (value.toMap().value("blank").toBool()) ++blankCells;
    if (!require(blankCells == 2, "pairwise diagonal cells can be blanked")) return 1;
    const QVariantMap pairwiseState = pairwise.state();
    TabSession restored;
    if (!require(restored.restoreState(pairwiseState) && restored.type() == QStringLiteral("pairwise") &&
                 restored.cellCount() == 4 && restored.diagonalMode() == QStringLiteral("blank"),
                 "versioned tab state round-trips")) return 1;

    const std::vector<contactRecord> syntheticRecords = {
        {0, 0, 1.0f}, {0, 100, 2.0f}, {100, 100, 4.0f}, {100, 200, 6.0f}
    };
    const MatrixAnalysis::DenseMatrix dense = MatrixAnalysis::makeDense(
        syntheticRecords, 100, 0, 300, 0, 300, true, 512);
    if (!require(dense.valid() && dense.width == 3 && dense.height == 3 &&
                 dense.values[1] == 2.0f && dense.values[3] == 2.0f,
                 "dense matrix construction reflects intrachromosomal contacts")) return 1;
    const std::vector<MatrixAnalysis::PolarPixel> polar = MatrixAnalysis::bullseye(
        syntheticRecords, 100, 0, 0, 1, true);
    bool foundIndependentPolarPixel = false;
    for (const auto& pixel : polar) {
        if (pixel.dx == 1 && pixel.dy == 0 && pixel.present && pixel.value == 2.0f)
            foundIndependentPolarPixel = true;
    }
    if (!require(polar.size() == 5 && foundIndependentPolarPixel,
                 "bullseye preserves one source pixel per polar glyph without aggregation")) return 1;
    const std::vector<float> virtualValues = MatrixAnalysis::virtual4C(
        syntheticRecords, 100, 0, 0, 300, true);
    const std::vector<float> virtualRowValues = MatrixAnalysis::virtual4C(
        syntheticRecords, 100, 100, 0, 300, false, false);
    if (!require(virtualValues.size() == 3 && virtualValues[0] == 1.0f &&
                 virtualValues[1] == 2.0f && virtualValues[2] == 0.0f &&
                 virtualRowValues[0] == 2.0f && virtualRowValues[1] == 4.0f,
                 "virtual 4C extracts exact row and column anchor bins")) return 1;

    MatrixAnalysis::DenseMatrix ramp;
    ramp.width = 3;
    ramp.height = 3;
    ramp.resolution = 1;
    ramp.values = {0, 1, 2, 0, 1, 2, 0, 1, 2};
    ramp.present.assign(9, true);
    ramp.minimum = 0;
    ramp.maximum = 2;
    const MatrixAnalysis::DenseMatrix gradient = MatrixAnalysis::process(ramp, QStringLiteral("gradient-x"));
    if (!require(gradient.valid() && gradient.values[4] == 1.0f,
                 "dependency-free gradient operator")) return 1;
    MatrixAnalysis::DenseMatrix twoByTwo;
    twoByTwo.width = twoByTwo.height = 2;
    twoByTwo.resolution = 1;
    twoByTwo.values = {1, 2, 3, 4};
    twoByTwo.present.assign(4, true);
    twoByTwo.minimum = 1;
    twoByTwo.maximum = 4;
    const MatrixAnalysis::DenseMatrix squared = MatrixAnalysis::process(twoByTwo, QStringLiteral("matrix-square"));
    const MatrixAnalysis::DenseMatrix diffused = MatrixAnalysis::process(
        twoByTwo, QStringLiteral("graph-diffusion"), 2, 0.25);
    const MatrixAnalysis::DenseMatrix polarTransform = MatrixAnalysis::process(ramp, QStringLiteral("polar"));
    const MatrixAnalysis::DenseMatrix gabor = MatrixAnalysis::process(ramp, QStringLiteral("gabor"), 2.0, 45.0);
    const MatrixAnalysis::DenseMatrix rejectedGabor =
        MatrixAnalysis::process(ramp, QStringLiteral("gabor"), 100.0, 0.0);
    if (!require(squared.valid() && squared.values == std::vector<float>({7, 10, 15, 22}) &&
                 diffused.valid() && polarTransform.valid() && gabor.valid() &&
                 !rejectedGabor.valid() && rejectedGabor.error.contains(QStringLiteral("limited")),
                 "bounded matrix square, graph diffusion, polar, and Gabor operators")) return 1;

    std::vector<contactRecord> denseDiagonal;
    for (int x = 0; x < 100; ++x) {
        for (int distance = 0; distance < 100; ++distance) {
            denseDiagonal.push_back(
                {x * 100, (x + distance) * 100, static_cast<float>(x + distance + 1)});
        }
    }
    const std::vector<contactRecord> boundedDiagonal = MatrixAnalysis::boundedRotatedRecords(
        denseDiagonal, 100, 0, 20000, 10000, 20, 20, 100);
    if (!require(!boundedDiagonal.empty() && boundedDiagonal.size() <= 100,
                 "rotated heatmaps aggregate dense strips to their rendering budget")) return 1;
    if (!require(!MatrixAnalysis::supportedOperations().join(QLatin1Char(' ')).contains(QStringLiteral("FFT"), Qt::CaseInsensitive) &&
                 !MatrixAnalysis::supportedOperations().join(QLatin1Char(' ')).contains(QStringLiteral("wavelet"), Qt::CaseInsensitive),
                 "deferred FFT and wavelet operators are not exposed")) return 1;

    for (const QString& type : {QStringLiteral("rotated-45"), QStringLiteral("bullseye"),
                                QStringLiteral("virtual-4c"), QStringLiteral("processing")}) {
        TabSession analysisTab;
        analysisTab.initialize(type);
        if (!require(analysisTab.type() == type && analysisTab.mapCount() == 2 && analysisTab.cellCount() == 2,
                     "analysis tabs create synchronized multi-map cells")) return 1;
        analysisTab.addMap();
        if (!require(analysisTab.mapCount() == 3 && analysisTab.cellCount() == 3,
                     "analysis tabs can add maps")) return 1;
        const QVariantMap analysisState = analysisTab.state();
        TabSession restoredAnalysis;
        if (!require(restoredAnalysis.restoreState(analysisState) && restoredAnalysis.type() == type &&
                     restoredAnalysis.mapCount() == 3,
                     "analysis tab state round-trips")) return 1;
    }
    TabSession boundedProcessing;
    boundedProcessing.initialize(QStringLiteral("processing"));
    boundedProcessing.setProcessingParameter(100.0);
    boundedProcessing.setProcessingOperator(QStringLiteral("gabor"));
    if (!require(boundedProcessing.processingParameter() == 12.0,
                 "processing tabs clamp Gabor sigma before scheduling work")) return 1;
    return 0;
}
