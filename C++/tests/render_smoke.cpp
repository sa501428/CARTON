#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJSValue>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>

#include "AnalysisItems.h"
#include "HicDataController.h"

namespace {
bool spinUntil(QGuiApplication& application, const std::function<bool()>& condition,
               int timeoutMilliseconds = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMilliseconds) {
        application.processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    application.processEvents(QEventLoop::AllEvents, 10);
    return condition();
}

bool require(bool condition, const char* message) {
    if (!condition) qCritical("Render smoke test failed: %s", message);
    return condition;
}
}

int main(int argc, char** argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication application(argc, argv);

    HicDataController controller;
    QQuickWindow window;
    window.resize(320, 240);
    auto* item = new ProcessedHeatmapItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setWidth(320);
    item->setHeight(240);

    int resultCount = 0;
    int renderedFrames = 0;
    QObject::connect(item, &ProcessedHeatmapItem::resultChanged,
                     &application, [&resultCount]() { ++resultCount; });
    QObject::connect(&window, &QQuickWindow::frameSwapped,
                     &application, [&renderedFrames]() { ++renderedFrames; });

    window.show();
    item->setController(&controller);
    if (!require(spinUntil(application, [&]() {
                     return resultCount >= 1 && renderedFrames >= 1;
                 }),
                 "initial processed texture rendered")) {
        return 1;
    }

    const int firstFrame = renderedFrames;
    item->setOperation(QStringLiteral("laplacian"));
    if (!require(spinUntil(application, [&]() {
                     return resultCount >= 2 && renderedFrames > firstFrame;
                 }),
                 "processed texture can be replaced")) {
        return 1;
    }

    item->setOperation(QStringLiteral("gabor"));
    item->setParameter(100.0);
    if (!require(spinUntil(application, [&]() {
                     return resultCount >= 3 && !item->errorString().isEmpty();
                 }),
                 "an invalid operation result safely clears the texture")) {
        return 1;
    }

    item->setOperation(QStringLiteral("gradient-magnitude"));
    item->setParameter(1.0);
    if (!require(spinUntil(application, [&]() {
                     return resultCount >= 4 && item->errorString().isEmpty();
                 }),
                 "rendering recovers after an invalid result")) {
        return 1;
    }

    delete item;

    HicDataController rotatedController;
    auto* rotatedItem = new RotatedHeatmapItem(window.contentItem());
    rotatedItem->setParentItem(window.contentItem());
    rotatedItem->setWidth(320);
    rotatedItem->setHeight(240);
    rotatedItem->setMaxDistance(2000000);
    rotatedItem->setController(&rotatedController);
    QObject::connect(&rotatedController, &HicDataController::metadataChanged,
                     &application, [&rotatedController]() {
        rotatedController.setAnalysisPaddingBins(200);
        if (rotatedController.resolutions().contains(10000))
            rotatedController.setResolution(10000);
        rotatedController.setViewRegion(QStringLiteral("22"), 0, 51304566,
                                        QStringLiteral("22"), 0, 51304566);
    });
    const int frameBeforeRotatedLoad = renderedFrames;
    rotatedController.openFile(QUrl::fromLocalFile(QStringLiteral(CARTON_TEST_HIC_PATH)));
    if (!require(spinUntil(application, [&]() {
                     return rotatedController.recordCount() > 0 &&
                            renderedFrames > frameBeforeRotatedLoad;
                 }, 10000),
                 "45-degree renderer loads and paints a real Hi-C strip")) {
        return 1;
    }

    delete rotatedItem;

    // The strip used to fill its whole pane with one saturated colour at the
    // view a freshly opened map lands on. The vertical axis was pinned to a
    // fixed 2 Mb while the horizontal one spanned a chromosome, so every bin
    // became a diamond many times taller than the pane. Rotating a square map
    // by 45 degrees puts the midpoint on one axis and half the separation on
    // the other at the same base pairs per pixel, so the reach a strip can show
    // follows from its aspect ratio alone. The software renderer used here
    // skips custom geometry nodes, so check that reach rather than the pixels.
    HicDataController defaultController;
    auto* defaultItem = new RotatedHeatmapItem(window.contentItem());
    defaultItem->setParentItem(window.contentItem());
    defaultItem->setWidth(1200);
    defaultItem->setHeight(200);
    // Mirrors what TabSession applies for a 45-degree tab.
    QObject::connect(defaultItem, &RotatedHeatmapItem::effectiveMaxDistanceChanged,
                     &application, [&defaultController, defaultItem]() {
        const qint64 distance = defaultItem->effectiveMaxDistance();
        const qint64 resolution = std::max(1, defaultController.resolution());
        defaultController.setAnalysisPaddingBins(
            static_cast<int>(std::clamp<qint64>((distance / 2 + resolution - 1) / resolution + 2, 1, 2000)));
        defaultController.setAutoColorDistanceLimit(distance);
    });
    // This map only carries chromosome 22, so stand in for the whole-chromosome
    // view a fresh open lands on. The resolution is left to adapt to the span,
    // exactly as resetView() would pick it.
    QObject::connect(&defaultController, &HicDataController::metadataChanged,
                     &application, [&defaultController]() {
        defaultController.setViewRegion(QStringLiteral("22"), 0, 51304566,
                                        QStringLiteral("22"), 0, 51304566);
    });
    defaultItem->setController(&defaultController);
    const int frameBeforeDefaultLoad = renderedFrames;
    defaultController.openFile(QUrl::fromLocalFile(QStringLiteral(CARTON_TEST_HIC_PATH)));
    if (!require(spinUntil(application, [&]() {
                     return defaultController.recordCount() > 0 &&
                            renderedFrames > frameBeforeDefaultLoad;
                 }, 10000),
                 "45-degree renderer paints the view a freshly opened map lands on")) {
        return 1;
    }

    auto reachMatchesAspect = [defaultItem, &defaultController](const char* message) {
        const qint64 span = std::max(defaultController.x1(), defaultController.y1()) -
                            std::min(defaultController.x0(), defaultController.y0());
        const double expected = 2.0 * static_cast<double>(span) *
                                defaultItem->height() / defaultItem->width();
        const double slack = std::max(1.0, static_cast<double>(defaultController.resolution()));
        return require(std::abs(static_cast<double>(defaultItem->effectiveMaxDistance()) - expected) <= slack,
                       message);
    };
    if (!reachMatchesAspect("the automatic vertical reach follows the strip's aspect ratio")) return 1;

    const qint64 wideReach = defaultItem->effectiveMaxDistance();
    defaultController.setViewRegion(QStringLiteral("22"), 20000000, 22000000,
                                    QStringLiteral("22"), 20000000, 22000000);
    if (!reachMatchesAspect("zooming in shortens the vertical reach with the view")) return 1;
    if (!require(defaultItem->effectiveMaxDistance() < wideReach / 4,
                 "a twenty-fold zoom is not drawn at the same separation as the whole chromosome")) {
        return 1;
    }

    defaultItem->setAutoDistance(false);
    defaultItem->setMaxDistance(500000);
    if (!require(defaultItem->effectiveMaxDistance() == 500000,
                 "an explicit maximum distance overrides the aspect ratio")) return 1;

    delete defaultItem;

    // The track painters read trackRenderBatches from QML, so the parallel
    // numeric arrays have to survive the C++ to JavaScript boundary as
    // indexable sequences. Exercise that through a real engine rather than
    // trusting the QVariant round trip.
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory for the track batch check")) return 1;
    const QString trackPath = temporary.filePath(QStringLiteral("batch.bedgraph"));
    {
        QFile file(trackPath);
        if (!require(file.open(QIODevice::WriteOnly), "write bedGraph for the track batch check")) return 1;
        file.write("chr22\t0\t1000\t4\nchr22\t1000\t2000\t-2\nchr22\t2000\t3000\t7\n");
    }

    HicDataController trackController;
    trackController.setResolution(1000);
    trackController.setChrX(QStringLiteral("22"));
    trackController.setChrY(QStringLiteral("22"));
    trackController.setX0(0);
    trackController.setX1(3000);
    trackController.setY0(0);
    trackController.setY1(3000);
    trackController.loadTrackFromPath(trackPath);
    if (!require(spinUntil(application, [&]() { return trackController.trackCount() == 1; }),
                 "track loaded for the batch check")) {
        return 1;
    }

    QQmlEngine engine;
    QJSValue evaluate = engine.evaluate(QStringLiteral(R"JS(
        (function (controller) {
            var batches = controller.trackRenderBatches(true, 300)
            if (batches.length !== 1) return "expected one batch, got " + batches.length
            var batch = batches[0]
            if (batch.count <= 0) return "batch carries no bins"
            if (batch.starts.length !== batch.count) return "starts is not indexable"
            var total = 0
            for (var i = 0; i < batch.count; ++i) {
                if (!(batch.ends[i] > batch.starts[i])) return "bin " + i + " has no extent"
                total += batch.values[i]
            }
            if (!(total > 0)) return "values did not survive as numbers"
            if (!(batch.max > batch.min)) return "display range is empty"
            return "ok"
        })
    )JS"));
    if (!require(!evaluate.isError(), "track batch script compiles")) return 1;
    const QJSValue outcome = evaluate.call({engine.toScriptValue(&trackController)});
    if (!require(!outcome.isError() && outcome.toString() == QStringLiteral("ok"),
                 qPrintable(QStringLiteral("trackRenderBatches reads correctly from QML: %1")
                                .arg(outcome.toString())))) {
        return 1;
    }

    return 0;
}
