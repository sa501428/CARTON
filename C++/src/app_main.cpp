#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>

#include "HicDataController.h"
#include "HicHeatmapItem.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("CARTON"));
    QGuiApplication::setOrganizationName(QStringLiteral("CARTON"));
    QGuiApplication::setFont(QFont(QStringLiteral("Helvetica Neue")));
#ifdef Q_OS_MACOS
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Unknown);
#endif
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    qmlRegisterType<HicDataController>("Carton", 1, 0, "HicDataController");
    qmlRegisterType<HicHeatmapItem>("Carton", 1, 0, "HicHeatmapItem");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    engine.loadFromModule("Carton", "Main");

    return app.exec();
}
