#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>

#include <limits>

#include "HicDataController.h"
#include "HicTileCache.h"

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
    if (!require(controller.trackSummaries().front().toMap().value("height").toInt() == 400 &&
                 controller.visibleTrackHeight() == 400,
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
    return 0;
}
