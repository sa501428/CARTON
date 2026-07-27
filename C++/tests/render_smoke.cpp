#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
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

    return 0;
}
