#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <cmath>
#include <functional>

#include "HicDataController.h"
#include "HeatmapPainting.h"
#include "MatrixAnalysis.h"
#include "TabSession.h"

namespace {
bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return predicate();
}
void drainLoads() {
    // Deliver watcher notifications only after replacement/clear actions.
    for (int i = 0; i < 3; ++i) {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}
bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("CARTON tests"));
    app.setApplicationName(QStringLiteral("CARTON regressions"));
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    bool passed = true;
    auto check = [&passed](bool condition, const char* message) {
        if (!condition) { qCritical("Regression failed: %s", message); passed = false; }
        return condition;
    };
    const QString track = temporary.filePath(QStringLiteral("signal.bedgraph"));
    const QString loops = temporary.filePath(QStringLiteral("loops.bedpe"));
    const QString bands = temporary.filePath(QStringLiteral("cytoband.txt"));
    if (!writeFile(track, "chr22\t0\t1000\t4\n") ||
        !writeFile(loops, "chr22\t0\t1000\tchr22\t2000\t3000\tloop\n") ||
        !writeFile(bands, "chr22\t0\t1000\tp11\tgneg\n")) return 1;

    HicDataController cleared;
    cleared.loadTrackFromPath(track);
    cleared.loadTrackFromPath(track);
    cleared.loadAnnotationsFromPath(loops);
    cleared.loadAnnotationsFromPath(loops);
    cleared.loadCytobands(QUrl::fromLocalFile(bands));
    cleared.clearTracks();
    cleared.clearAnnotations();
    cleared.clearCytobands();
    drainLoads();
    check(cleared.trackCount() == 0, "clearing tracks cancels queued and active results");
    check(cleared.annotationCount() == 0, "clearing annotations cancels queued and active results");
    check(cleared.cytobandCount() == 0, "clearing cytobands cancels an active result");
    cleared.loadTrackFromPath(track);
    check(waitFor([&]() { return cleared.trackCount() == 1; }), "track loading recovers after clearing");
    cleared.clearLayersWithUndo(QStringLiteral("tracks"));
    check(cleared.trackCount() == 0 && cleared.canUndoLayerClear(), "track clear retains an undo snapshot");
    cleared.loadAnnotationsFromPath(loops);
    check(waitFor([&]() { return cleared.annotationCount() == 1; }), "unrelated annotations load after clearing tracks");
    check(cleared.undoLayerClear() && cleared.trackCount() == 1 && cleared.annotationCount() == 1,
          "undo track clear preserves annotations added after the clear");
    cleared.clearLayersWithUndo(QStringLiteral("tracks"));
    cleared.loadTrackFromPath(track);
    check(waitFor([&]() { return cleared.trackCount() == 1; }), "new tracks load after an undoable clear");
    check(!cleared.canUndoLayerClear() && !cleared.undoLayerClear() && cleared.trackCount() == 1,
          "new track changes invalidate stale clear undo");

    PooledAnnotation feature;
    feature.id = QStringLiteral("shared-feature");
    feature.chr1 = feature.chr2 = QStringLiteral("chr22");
    feature.end1 = feature.end2 = 1000;
    const auto pooled = DatasetRegistry::instance()->createCustomAnnotations(QStringLiteral("Shared layer"), {feature});
    HicDataController annotationUndo;
    annotationUndo.loadAnnotationResource(pooled.id);
    annotationUndo.clearLayersWithUndo(QStringLiteral("annotations"));
    check(annotationUndo.annotationCount() == 0 && pooled.data->annotations.size() == 1,
          "undoable clear detaches annotations from their shared source");
    check(annotationUndo.undoLayerClear() && annotationUndo.annotationCount() == 1 && pooled.data->annotations.size() == 1,
          "annotation clear undo restores the original layers without changing other consumers");
    const auto unchanged = DatasetRegistry::instance()->restoreCustomAnnotations(pooled.id, QStringLiteral("Shared layer"), {feature});
    check(unchanged.data == pooled.data, "unchanged restored annotations retain pooling");
    feature.end1 = 2000;
    const auto restoredAnnotations = DatasetRegistry::instance()->restoreCustomAnnotations(pooled.id, QStringLiteral("Restored snapshot"), {feature});
    check(restoredAnnotations.data && restoredAnnotations.id != pooled.id && pooled.data->annotations.front().end1 == 1000 &&
          restoredAnnotations.data->annotations.front().end1 == 2000,
          "restoring an older annotation snapshot cannot overwrite another tab's shared annotations");

    TabSession selection;
    selection.initialize(QStringLiteral("multi-map"));
    selection.addMap();
    int layoutChanges = 0;
    QObject::connect(&selection, &TabSession::cellLayoutChanged, &selection, [&]() { ++layoutChanges; });
    selection.setActiveCellIndex(0);
    selection.setActiveCellIndex(1);
    check(layoutChanges == 0 && selection.cells()[1].toMap().value(QStringLiteral("selected")).toBool(),
          "selecting a map updates selection without rebuilding viewport delegates");
    HicDataController replaced;
    replaced.loadTrackFromPath(track);
    replaced.loadAnnotationsFromPath(loops);
    replaced.restoreSessionState({}, false);
    drainLoads();
    check(replaced.trackCount() == 0 && replaced.annotationCount() == 0,
          "session restore discards results from the old session");

    // A padded record just before the matrix origin must not truncate into bin 0.
    const auto dense = MatrixAnalysis::makeDense({{99, 100, 9.0f}}, 100,
                                                 100, 300, 100, 300, false);
    check(dense.valid() && !dense.present[0] && dense.values[0] == 0.0f,
          "dense analysis excludes padded records before its origin");

    const std::vector<contactRecord> sparsePrimary = {{0, 100, 3.0f}, {100, 100, 5.0f}};
    const std::vector<contactRecord> sparseControl = {{100, 100, 2.0f}, {100, 200, 7.0f}};
    const auto difference = MatrixAnalysis::compareRecords(sparsePrimary, sparseControl, "diff");
    const auto logRatio = MatrixAnalysis::compareRecords(sparsePrimary, sparseControl, "logratio");
    const auto ratio1 = MatrixAnalysis::compareRecords(sparsePrimary, sparseControl, "ratio1");
    check(difference.size() == 3 && difference[0].counts == 3.0f &&
          difference[1].counts == 3.0f && difference[2].counts == -7.0f,
          "sparse differences retain primary-only and control-only contacts");
    check(logRatio.size() == 3 && logRatio[0].counts == 2.0f && logRatio[2].counts == -3.0f &&
          ratio1.size() == 2 && ratio1[0].counts == 3.0f,
          "pseudocount ratios treat omitted sparse contacts as zero");

    HeatmapColorSettings colors;
    colors.maximum = 10.0;
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        paintHeatmapRecords(painter, image.rect(), {{0, 100, 8.0f}}, {},
                            "22", "22", "observed", 100, 200, 0, 100, 100, colors);
    }
    check(image.pixelColor(50, 50).green() < 200,
          "figure exports paint reflected contacts in asymmetric views");
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        paintHeatmapRecords(painter, image.rect(), {{0, 100, 8.0f}}, {},
                            "22", "22", "vs", 0, 200, 0, 200, 100, colors);
    }
    check(image.pixelColor(75, 25).green() < 200 && image.pixelColor(25, 75) == QColor(Qt::white),
          "split exports keep an empty control triangle blank");
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        paintHeatmapRecords(painter, QRect(20, 20, 60, 60), {{0, 0, 8.0f}}, {},
                            "22", "other", "observed", 50, 150, 50, 150, 100, colors);
    }
    check(image.pixelColor(25, 25).green() < 200 && image.pixelColor(10, 10) == QColor(Qt::white),
          "figure exports include partial edge bins and clip them to the plot");

    TabSession blank;
    blank.loadTrackFromPath(track);
    check(waitFor([&]() { return blank.activeController()->trackCount() == 1; }), "load blank-tab track");
    TabSession restoredBlank;
    restoredBlank.restoreState(blank.state());
    drainLoads();
    check(restoredBlank.activeController()->trackCount() == 1,
          "workspace restores layers on tabs without a primary map");

    const QString transPath = QStringLiteral(CARTON_TEST_TRANS_PATH);
    HicDataController trans;
    trans.openFile(QUrl::fromLocalFile(transPath));
    if (!check(waitFor([&]() { return !trans.resolutions().isEmpty() && !trans.busy(); }),
               "load v10 trans fixture")) return 1;
    check(trans.chromosomeNames().contains(QStringLiteral("chrA")) &&
          trans.chromosomeBoundaries().size() == 2,
          "v10 files without All retain the chromosome at index zero");
    check(trans.chrX() == QStringLiteral("chrA") && trans.chrY() == QStringLiteral("chrA"),
          "files without All default to the first chromosome");
    const QString allLastPath = QStringLiteral(CARTON_TEST_ALL_LAST_PATH);
    HicDataController allLast;
    allLast.openFile(QUrl::fromLocalFile(allLastPath));
    check(waitFor([&]() { return !allLast.resolutions().isEmpty() && !allLast.busy(); }) &&
          allLast.chrX() == QStringLiteral("All") && allLast.chrY() == QStringLiteral("All") &&
          allLast.xChromosomeLength() == 18 && allLast.chromosomeBoundaries().front().toMap().value("end").toLongLong() == 10 && allLast.chromosomeBoundaries().size() == 2 &&
          allLast.recordCount() > 0,
          "default selection finds an out-of-order All without excluding the first chromosome");
    QFile allFixture(allLastPath);
    if (!allFixture.open(QIODevice::ReadOnly)) return 1;
    QByteArray uppercaseAll = allFixture.readAll();
    uppercaseAll.replace(QByteArray("All\0", 4), QByteArray("ALL\0", 4));
    const QString uppercasePath = temporary.filePath("uppercase-all.hic");
    if (!writeFile(uppercasePath, uppercaseAll)) return 1;
    HicDataController uppercase;
    uppercase.openFile(QUrl::fromLocalFile(uppercasePath));
    check(waitFor([&]() { return !uppercase.resolutions().isEmpty() && !uppercase.busy(); }) &&
          uppercase.chrX() == QStringLiteral("ALL") && uppercase.chromosomeBoundaries().size() == 2 &&
          uppercase.recordCount() == allLast.recordCount() && uppercase.chromosomeNames().contains(QStringLiteral("ALL")),
          "default All lookup is case insensitive");
    uppercase.openControlFile(QUrl::fromLocalFile(allLastPath));
    check(waitFor([&]() { return uppercase.controlReady() && !uppercase.busy(); }), "load differently cased All control");
    uppercase.setMatrixType("control");
    drainLoads();
    check(uppercase.recordCount() == allLast.recordCount() && uppercase.recordCount() > 0,
          "control requests use the control file's exact chromosome spelling");
    trans.setResolution(10);
    trans.setResolutionLocked(true);
    trans.setViewRegion("chrB", 0, 70, "chrA", 0, 80);
    drainLoads();
    const auto expectedTrans = straw("observed", "NONE", transPath.toStdString(),
                                     "chrB:0:70", "chrA:0:80", "BP", 10);
    const auto actualTrans = trans.recordsSnapshot();
    bool oriented = !expectedTrans.empty() && expectedTrans.size() == actualTrans.size();
    for (std::size_t i = 0; oriented && i < expectedTrans.size(); ++i)
        oriented = expectedTrans[i].binX == actualTrans[i].binX &&
                   expectedTrans[i].binY == actualTrans[i].binY &&
                   expectedTrans[i].counts == actualTrans[i].counts;
    check(oriented, "interchromosomal tiles retain Straw's requested-axis orientation");

    const QUrl hic = QUrl::fromLocalFile(QStringLiteral(CARTON_TEST_HIC_PATH));
    HicDataController navigation;
    navigation.openFile(hic);
    if (!check(waitFor([&]() { return !navigation.resolutions().isEmpty() && !navigation.busy(); }),
               "load navigation fixture")) return 1;
    navigation.setResolution(10000);
    navigation.setResolutionLocked(true);
    navigation.setViewRegion(QStringLiteral("22"), 10000000, 14000000,
                             QStringLiteral("22"), 10000000, 14000000);
    const qint64 anchorX = navigation.x0() + (navigation.x1() - navigation.x0()) / 4;
    const qint64 anchorY = navigation.y0() + (navigation.y1() - navigation.y0()) * 3 / 4;
    navigation.zoom(2.0, 0.25, 0.75);
    check(std::abs(navigation.x0() + (navigation.x1() - navigation.x0()) / 4 - anchorX) <= 1 &&
          std::abs(navigation.y0() + (navigation.y1() - navigation.y0()) * 3 / 4 - anchorY) <= 1,
          "wheel zoom preserves the genomic coordinate under the cursor");

    HicDataController cacheController;
    cacheController.openFile(hic);
    if (!check(waitFor([&]() { return !cacheController.resolutions().isEmpty() && !cacheController.busy(); }),
               "load cache fixture")) return 1;
    cacheController.setViewRegion("22", 0, 51304566, "22", 0, 51304566);
    if (!check(waitFor([&]() { return !cacheController.busy() && cacheController.recordCount() > 0; }),
               "load primary cache fixture")) return 1;
    cacheController.openControlFile(hic);
    if (!check(waitFor([&]() { return cacheController.controlReady() && !cacheController.busy(); }),
               "load cache control fixture")) return 1;
    DatasetRegistry::instance()->tileCache()->clear();
    cacheController.setMatrixType("control");
    if (!check(waitFor([&]() { return !cacheController.busy(); }), "control-only load settles")) return 1;
    cacheController.setMatrixType("observed");
    drainLoads();
    check(cacheController.recordCount() > 0, "control-only loads cannot poison the primary tile cache");
    cacheController.setMatrixType("logeovs");
    drainLoads();
    const auto logPrimary = cacheController.recordsSnapshot();
    const auto logControl = cacheController.controlRecordsSnapshot();
    bool equalLogHalves = !logPrimary.empty() && logPrimary.size() == logControl.size();
    for (std::size_t i = 0; equalLogHalves && i < logPrimary.size(); ++i)
        equalLogHalves = std::abs(logPrimary[i].counts - logControl[i].counts) < 0.00001f;
    check(equalLogHalves, "log O/E comparison transforms both halves identically");
    cacheController.setMatrixType("oe");
    drainLoads();
    const auto oe = cacheController.recordsSnapshot();
    cacheController.setMatrixType("explogoe");
    drainLoads();
    const auto expLog = cacheController.recordsSnapshot();
    bool expLogIdentity = !oe.empty() && oe.size() == expLog.size();
    for (std::size_t i = 0; expLogIdentity && i < oe.size(); ++i)
        expLogIdentity = std::abs(oe[i].counts - expLog[i].counts) < 0.00001f;
    check(expLogIdentity, "exp(log(O/E)) retains O/E values");

    TabSession source;
    source.setPrimaryFile(0, hic);
    if (!check(waitFor([&]() { return !source.activeController()->resolutions().isEmpty() &&
                                     !source.activeController()->busy(); }), "load workspace fixture")) return 1;
    source.activeController()->setViewRegion(QStringLiteral("22"), 10000000, 14000000,
                                             QStringLiteral("22"), 10000000, 14000000);
    source.setControlFile(0, hic);
    drainLoads();
    check(waitFor([&]() { return source.activeController()->controlReady(); }),
          "control maps selected during tile loads are eventually opened");
    source.activeController()->setMatrixType(QStringLiteral("diff"));
    source.activeController()->setColorMax(17.0);
    const QVariantMap workspace = source.state();
    TabSession restored;
    restored.restoreState(workspace);
    drainLoads();
    check(waitFor([&]() { return !restored.activeController()->busy() &&
                                  !restored.activeController()->resolutions().isEmpty(); }),
          "restored workspace finishes loading");
    drainLoads();
    check(restored.activeController()->x0() == source.activeController()->x0() &&
          restored.activeController()->x1() == source.activeController()->x1(),
          "workspace restores its view after primary metadata arrives");
    check(restored.activeController()->controlReady() &&
          restored.activeController()->matrixType() == QStringLiteral("diff"),
          "workspace restores comparison mode after control metadata arrives");
    check(std::abs(restored.activeController()->colorMax() - 17.0) < 0.001,
          "workspace retains its manual color range");
    return passed ? 0 : 1;
}
