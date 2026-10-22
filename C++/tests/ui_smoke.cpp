#include "ui_smoke.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>

#include <cmath>
#include <functional>

namespace {
void settle(int milliseconds = 60) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}
bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 10000) settle(10);
    return predicate();
}
bool flowsFit(QQuickItem* item) {
    if (!item || !item->isVisible()) return true;
    bool ok = true;
    if (QByteArray(item->metaObject()->className()).startsWith("QQuickFlow")) {
        for (QQuickItem* child : item->childItems()) {
            if (child->isVisible() && child->width() > 0 && child->x() + child->width() > item->width() + 2) {
                qCritical() << "Toolbar overflow:" << child->metaObject()->className()
                            << child->property("text") << child->x() << child->width() << item->width();
                ok = false;
            }
        }
    }
    for (QQuickItem* child : item->childItems()) ok = flowsFit(child) && ok;
    return ok;
}
}

int runUiSmokeTests(QQmlApplicationEngine& engine) {
    if (engine.rootObjects().isEmpty()) return 1;
    QObject* root = engine.rootObjects().first();
    auto* window = qobject_cast<QQuickWindow*>(root);
    bool passed = true;
    QObject warningScope;
    const auto warningConnection = QObject::connect(&engine, &QQmlEngine::warnings, &warningScope,
        [&passed](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) qCritical().noquote() << warning.toString();
            if (!warnings.isEmpty()) passed = false;
        });
    auto check = [&passed](bool ok, const char* message) {
        if (!ok) { qCritical("Interface test failed: %s", message); passed = false; }
        return ok;
    };
    QObject* sidebar = root->findChild<QObject*>("workspaceSidebar");
    QObject* navigation = root->findChild<QObject*>("persistentMapNavigation");
    QObject* settings = root->findChild<QObject*>("applicationSettings");
    QObject* chooser = root->findChild<QObject*>("newTabChooser");
    QObject* colors = root->findChild<QObject*>("heatmapColorSettings");
    if (!check(sidebar && navigation && settings && chooser && colors && window, "primary interface components exist")) return 1;
    auto script = [&](const QString& body) {
        QJSValue fn = engine.evaluate("(function(w, sidebar, navigation, settings, chooser, colors) {" + body + "})");
        QJSValue result = fn.call({engine.newQObject(root), engine.newQObject(sidebar), engine.newQObject(navigation),
                                  engine.newQObject(settings), engine.newQObject(chooser), engine.newQObject(colors)});
        if (result.isError()) { qCritical() << result.toString() << result.property("stack").toString(); passed = false; }
        return result;
    };
    for (QObject* object : {root, sidebar, navigation, settings, chooser, colors})
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
    settle();
    for (int page = 0; page < 4; ++page) {
        sidebar->setProperty("currentPage", page);
        settle();
        check(sidebar->property("currentPage").toInt() == page, "sidebar destination changes");
    }
    script("chooser.open();"); settle();
    check(chooser->property("visible").toBool(), "task-oriented tab chooser opens");
    script("chooser.close(); settings.open();"); settle();
    check(settings->property("visible").toBool(), "application settings opens");
    QObject* settingsTabs = settings->findChild<QObject*>("settingsTabs");
    if (check(settingsTabs, "settings categories exist")) for (int i = 0; i < 3; ++i) { settingsTabs->setProperty("currentIndex", i); settle(); }
    script("settings.close(); colors.open();"); settle();
    check(colors->property("visible").toBool(), "canonical color settings opens");
    script("colors.close();");
    for (const QString& type : {"single", "multi-map", "multi-region", "map-region", "pairwise", "rotated-45", "bullseye", "virtual-4c", "processing"}) {
        script(QString("w.addTab('%1');").arg(type));
        for (int width : {760, 1024, 1440}) {
            root->setProperty("width", width);
            root->setProperty("height", 940);
            root->setProperty("fontScale", width == 760 ? 1.4 : 1.0);
            settle();
            check(navigation->property("width").toDouble() > 200, "navigation remains usable with sidebar open");
            check(flowsFit(window->contentItem()), qPrintable("responsive toolbars: " + type));
        }
    }
    root->setProperty("width", 1440);
    root->setProperty("fontScale", 1.0);
    script("w.addTab('single'); w.activeTab.setPrimaryFile(0, '" + QUrl::fromLocalFile(QStringLiteral(CARTON_UI_TEST_HIC_PATH)).toString() + "');");
    auto activeController = [&]() { return root->property("activeController").value<QObject*>(); };
    check(waitFor([&]() { QObject* c = activeController(); return c && !c->property("busy").toBool() && !c->property("chrX").toString().isEmpty(); }), "fixture loads through the active tab");
    script("navigation.syncModels(); navigation.focusLocus();"); settle();
    QObject* locus = root->findChild<QObject*>("navigationLocus");
    check(locus && locus->property("activeFocus").toBool(), "go-to-locus shortcut target receives focus");
    if (locus) locus->setProperty("text", "chrA:0-60");
    script("navigation.go();"); settle();
    check(activeController() && activeController()->property("chrX").toString() == "chrA", "persistent locus control navigates the map");
    script("w.activeTab.loadTrackFromPath('" + QStringLiteral(CARTON_UI_TEST_TRACK_PATH) + "');");
    check(waitFor([&]() { return activeController()->property("trackCount").toInt() > 0; }), "track loads for compact layer inspection");
    sidebar->setProperty("currentPage", 1);
    sidebar->setProperty("selectedTrackIndex", 0);
    settle();
    script("w.clearLayers('tracks');"); settle();
    check(activeController()->property("trackCount").toInt() == 0, "clear action removes tracks");
    script("w.undoClearLayers();"); settle();
    check(activeController()->property("trackCount").toInt() == 1, "undo restores cleared tracks");
    script("var count = w.tabs.length; w.closeTab(0); if (w.tabs.length !== count - 1) throw new Error('per-tab close failed');"); settle();
    check(root->property("activeTab").value<QObject*>() != nullptr, "closing another tab preserves an active tab");
    QObject::disconnect(warningConnection);
    return passed ? 0 : 1;
}
