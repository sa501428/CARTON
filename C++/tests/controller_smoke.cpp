#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>

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
    if (!require(controller.tracksModel()->rowCount() == 1 && controller.annotationsModel()->rowCount() == 1,
                 "workspace list models")) return 1;
    controller.setTrackVisible(0, false);
    controller.setTrackCollapsed(0, true);
    controller.setTrackHeight(0, 96);
    const QVariantMap summary = controller.trackSummaries().front().toMap();
    if (!require(!summary.value("visible").toBool() && summary.value("collapsed").toBool() &&
                 summary.value("height").toInt() == 96, "track display state")) return 1;
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
    return 0;
}
