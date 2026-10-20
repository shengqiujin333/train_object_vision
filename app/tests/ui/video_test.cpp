#include "video_runtime.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQmlContext>
#include <QQuickView>
#include <QThread>
#include <qqml.h>
#include <cstdio>
#include <functional>
#include "railhmi/core/navigation.h"

static bool waitFor(const std::function<bool()>& condition, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(20);
    }
    return false;
}

static QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = findItem(child, name)) return found;
    return nullptr;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc < 2) return 2;
    auto config = railhmi::core::load_config_dir(argv[1]);
    if (!config.ok) return 2;
    config.settings.app.simulation = true;
    config.settings.thresholds.freeze_timeout_ms = 800;
    qmlRegisterUncreatableType<VideoChannel>("Railhmi.Video", 1, 0, "VideoChannel", "Runtime");
    qmlRegisterType<VideoFrameItem>("Railhmi.Video", 1, 0, "VideoFrame");
    VideoRuntime runtime(config.settings);
    const auto list = runtime.channels();
    auto channel = [&](int i) { return qvariant_cast<VideoChannel*>(list[i]); };
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty("railhmiChannels", list);
    view.rootContext()->setContextProperty("railhmiKeyStatus", QVariantList());
    view.setSource(QUrl("qrc:/qml/pages/P1Realtime.qml"));
    view.resize(1280, 720);
    view.show();
    runtime.start();
    check(waitFor([&]() {
        for (int i = 0; i < list.size(); ++i)
            if (!channel(i)->info()["presentable"].toBool() || channel(i)->image().isNull()) return false;
        return true;
    }), "six_workers_publish_owned_images");
    auto* left = findItem(view.rootObject(), "tileLeft");
    auto* front = findItem(view.rootObject(), "tileFront");
    auto* right = findItem(view.rootObject(), "tileRight");
    check(left && front && right && left->property("live").toBool() &&
          front->property("live").toBool() && right->property("live").toBool(), "P1_live_bindings");
    check(front && qvariant_cast<VideoChannel*>(front->property("channel")) == channel(0), "P1_position_mapping");
    QThread::msleep(100);
    QCoreApplication::processEvents();
    QImage screenshot = view.grabWindow();
    int colorful = 0;
    for (int y = 60; y < screenshot.height() - 200; y += 4)
        for (int x = 0; x < screenshot.width(); x += 4) {
            const QColor pixel = screenshot.pixelColor(x, y);
            if (pixel.saturation() > 180 && pixel.value() > 100) ++colorful;
        }
    check(colorful > 3000, "P1_actual_video_pixels_rendered");
    if (argc > 2) screenshot.save(QString::fromLocal8Bit(argv[2]) + "/video-P1.png");
    const auto otherBefore = channel(1)->image().cacheKey();
    runtime.freezeForTest(0, true);
    check(waitFor([&]() { return !channel(0)->info()["presentable"].toBool() && channel(0)->image().isNull(); }),
          "freeze_clears_old_frame");
    check(front && !front->property("live").toBool(), "P1_freeze_placeholder");
    check(channel(1)->info()["presentable"].toBool() && channel(1)->image().cacheKey() != otherBefore,
          "single_channel_failure_isolated");
    runtime.freezeForTest(0, false);
    check(waitFor([&]() { return channel(0)->info()["presentable"].toBool(); }), "resume_restores_video");
    view.setSource(QUrl("qrc:/qml/pages/P4AllCameras.qml"));
    check(waitFor([&]() { return findItem(view.rootObject(), "gridCell-CAM-06") != nullptr; }), "P4_six_stable_slots");
    auto* cell = findItem(view.rootObject(), "gridCell-CAM-01");
    const auto before = channel(0)->image().cacheKey();
    check(waitFor([&]() { return channel(0)->image().cacheKey() != before; }), "frames_keep_advancing_after_page_switch");
    check(cell && cell == findItem(view.rootObject(), "gridCell-CAM-01") && cell->property("live").toBool(),
          "P4_delegate_retained_during_updates");
    if (argc > 2) view.grabWindow().save(QString::fromLocal8Bit(argv[2]) + "/video-P4.png");

    // Exercise the actual application shell and navigation, including the old driver restriction.
    QVariantList navigation;
    for (auto page : railhmi::core::visible_pages(railhmi::core::Role::Driver)) {
        QVariantMap entry;
        entry["id"] = QString::fromLatin1(railhmi::core::to_string(page));
        entry["title"] = QString::fromUtf8(railhmi::core::page_title(page));
        navigation.append(entry);
    }
    auto* context = view.rootContext();
    context->setContextProperty("railhmiNav", navigation);
    context->setContextProperty("railhmiAllPages", navigation);
    context->setContextProperty("railhmiRole", "driver");
    context->setContextProperty("railhmiDefaultPage", QString::fromLatin1(
        railhmi::core::to_string(railhmi::core::default_page(railhmi::core::Role::Driver))));
    context->setContextProperty("railhmiModePrimary", QString::fromUtf8("可见光"));
    context->setContextProperty("railhmiModeSecondary", QString::fromUtf8("红外未接入"));
    context->setContextProperty("railhmiTopBar", QVariantList());
    context->setContextProperty("railhmiAiDebug", QVariantList());
    context->setContextProperty("railhmiMaintenanceCameras", QVariantList());
    context->setContextProperty("railhmiIntegrations", QVariantList());
    context->setContextProperty("railhmiFaults", QVariantList());
    QVariantList cameras;
    for (int i = 0; i < list.size(); ++i) cameras.append(channel(i)->info());
    context->setContextProperty("railhmiCameras", cameras);
    for (const char* name : {"railhmiAlarms", "railhmiReplay", "railhmiMaintenance",
                              "railhmiPermissions", "railhmiHealth"})
        context->setContextProperty(name, static_cast<QObject*>(nullptr));
    view.setSource(QUrl("qrc:/qml/main.qml"));
    check(waitFor([&]() { return findItem(view.rootObject(), "gridCell-CAM-06") != nullptr; }),
          "application_defaults_to_six_camera_view");
    auto* nav = findItem(view.rootObject(), "navigation");
    bool sixEntries = true;
    for (const auto& entry : navigation)
        sixEntries = sixEntries && findItem(view.rootObject(), "nav-" + entry.toMap()["id"].toString());
    check(navigation.size() == 6 && sixEntries, "application_shows_all_six_navigation_entries");
    for (const auto& entry : navigation) {
        const QString pageId = entry.toMap()["id"].toString();
        const bool invoked = nav && QMetaObject::invokeMethod(nav, "navigate", Q_ARG(QString, pageId));
        const bool loaded = waitFor([&]() {
            auto* loader = findItem(view.rootObject(), "pageLoader");
            return loader && loader->property("status").toInt() == 1 &&
                   view.rootObject()->property("currentPageId").toString() == pageId;
        });
        check(invoked && loaded && !view.rootObject()->property("denied").toBool(),
              ("application_opens_" + pageId).toUtf8().constData());
    }
    if (nav) QMetaObject::invokeMethod(nav, "navigate", Q_ARG(QString, QString("all_cameras")));
    QCoreApplication::processEvents();
    if (argc > 2) view.grabWindow().save(QString::fromLocal8Bit(argv[2]) + "/all-functions.png");
    runtime.stop();
    check(channel(0)->image().isNull() && !channel(0)->info()["presentable"].toBool(), "stop_clears_video");

    config.settings.app.simulation = false;
    for (auto& camera : config.settings.cameras) camera.credential_env = "RAILHMI_TEST_MISSING_CREDENTIAL";
    qunsetenv("RAILHMI_TEST_MISSING_CREDENTIAL");
    VideoRuntime missing(config.settings);
    missing.start();
    auto* failed = qvariant_cast<VideoChannel*>(missing.channels()[0]);
    check(failed->info()["state"].toString() == "offline" &&
          failed->info()["detail"].toString().contains("RAILHMI_TEST_MISSING_CREDENTIAL") &&
          !failed->info()["presentable"].toBool(), "missing_credentials_explicit_offline");
    missing.stop();
    std::printf("VIDEO_TEST failures=%d\n", failures);
    return failures ? 1 : 0;
}
