#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#ifdef CARTON_ENABLE_UI_TESTS
#include "tests/ui_smoke.h"
#endif

#include "HicDataController.h"
#include "HicHeatmapItem.h"
#include "DatasetRegistry.h"
#include "RegionSetModel.h"
#include "TabSession.h"
#include "AnalysisItems.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("CARTON"));
    QGuiApplication::setOrganizationName(QStringLiteral("CARTON"));
    const bool interfaceTest = QCoreApplication::arguments().contains(QStringLiteral("--ui-smoke-test"));
    QTemporaryDir testSettings;
    if (interfaceTest) {
        if (!testSettings.isValid()) return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testSettings.path());
    }
    QGuiApplication::setFont(QFont(QStringLiteral("Helvetica Neue")));
#ifdef Q_OS_MACOS
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Unknown);
#endif
    if (interfaceTest) QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<HicDataController>("Carton", 1, 0, "HicDataController");
    qmlRegisterType<HicHeatmapItem>("Carton", 1, 0, "HicHeatmapItem");
    qmlRegisterType<RotatedHeatmapItem>("Carton", 1, 0, "RotatedHeatmapItem");
    qmlRegisterType<BullseyeItem>("Carton", 1, 0, "BullseyeItem");
    qmlRegisterType<Virtual4CItem>("Carton", 1, 0, "Virtual4CItem");
    qmlRegisterType<ProcessedHeatmapItem>("Carton", 1, 0, "ProcessedHeatmapItem");
    qmlRegisterType<RegionSetModel>("Carton", 1, 0, "RegionSetModel");
    qmlRegisterType<TabSession>("Carton", 1, 0, "TabSession");
    qmlRegisterSingletonInstance("Carton", 1, 0, "DatasetRegistry", DatasetRegistry::instance());

    bool initialQmlWarnings = false;
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
#ifdef CARTON_ENABLE_UI_TESTS
    if (interfaceTest) QObject::connect(&engine, &QQmlEngine::warnings, &app,
        [&initialQmlWarnings](const QList<QQmlError>& warnings) { initialQmlWarnings |= !warnings.isEmpty(); });
#endif
    engine.loadFromModule("Carton", "Main");

#ifdef CARTON_ENABLE_UI_TESTS
    if (interfaceTest) {
        QTimer::singleShot(0, &app, [&engine, &initialQmlWarnings]() {
            const int result = runUiSmokeTests(engine);
            QCoreApplication::exit(initialQmlWarnings ? 1 : result);
        });
    }
#endif
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, [&engine]() {
            QCoreApplication::exit(engine.rootObjects().isEmpty() ? 1 : 0);
        });
    }

    return app.exec();
}
