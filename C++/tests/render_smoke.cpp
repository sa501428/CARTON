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
