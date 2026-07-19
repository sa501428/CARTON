#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>

#include "HicDataController.h"
#include "HicHeatmapItem.h"
#include "DatasetRegistry.h"
#include "RegionSetModel.h"
#include "TabSession.h"

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
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<HicDataController>("Carton", 1, 0, "HicDataController");
    qmlRegisterType<HicHeatmapItem>("Carton", 1, 0, "HicHeatmapItem");
    qmlRegisterType<RegionSetModel>("Carton", 1, 0, "RegionSetModel");
    qmlRegisterType<TabSession>("Carton", 1, 0, "TabSession");
    qmlRegisterSingletonInstance("Carton", 1, 0, "DatasetRegistry", DatasetRegistry::instance());

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    engine.loadFromModule("Carton", "Main");

    return app.exec();
}
