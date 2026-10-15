// railhmi Qt/QML 应用入口（骨架，ITEM-003）。
// 真正的页面/状态/取流由后续工作项接入；此处验证工程骨架可构建运行。

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QUrl>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/config.h"
#include "railhmi/core/navigation.h"
#include "railhmi/core/version.h"
#include "railhmi/platform/log.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("railhmi");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(railhmi::core::version_string()));

    const QString build_desc = QString::fromStdString(railhmi::core::build_description());
    railhmi::platform::Logger::instance().info(build_desc.toStdString());

    // 启动即校验配置；非法配置报出具体字段并非零退出（绝不静默使用默认值）。
    railhmi::core::ConfigLoadResult cfg = railhmi::core::load_config_dir(RAILHMI_CONFIG_DIR);
    if (!cfg.ok) {
        railhmi::platform::Logger::instance().error("configuration invalid; refusing to start");
        for (const auto& e : cfg.errors) {
            railhmi::platform::Logger::instance().error("  " + e.str());
        }
        return 4;
    }
    railhmi::platform::Logger::instance().info(
        "configuration OK: " + std::to_string(cfg.settings.cameras.size()) + " cameras");

    // 角色化导航（ITEM-011）：按角色过滤可见页入口，注入 QML 外壳（受限页由 QML 显示无权限+返回）。
    const railhmi::core::Role role = cfg.settings.app.default_role;
    QVariantList navList;
    QVariantList allList;
    for (railhmi::core::Page p : railhmi::core::all_pages()) {
        QVariantMap m;
        m["id"] = QString::fromLatin1(railhmi::core::to_string(p));
        m["title"] = QString::fromUtf8(railhmi::core::page_title(p));
        allList.append(m);
        if (railhmi::core::role_can_access(role, p)) navList.append(m);
    }
    const railhmi::core::ModeDisplay mode = railhmi::core::mode_display();
    const QString roleId = QString::fromLatin1(railhmi::core::to_string(role));
    const QString defaultPage = QString::fromLatin1(railhmi::core::to_string(railhmi::core::default_page(role)));

    // 配置驱动摄像头注册表与 NVR 集成项（ITEM-012）：槽位保持 cameras.yaml 顺序；徽标由 ai_profile 决定。
    QVariantList camList;
    for (const auto& c : railhmi::core::build_registry(cfg.settings)) {
        QVariantMap m;
        m["id"] = QString::fromStdString(c.id);
        m["name"] = QString::fromUtf8(c.name.c_str());
        m["position"] = QString::fromLatin1(railhmi::core::to_string(c.position));
        m["badge"] = QString::fromUtf8(c.detection_badge.c_str());
        m["aiEnabled"] = c.ai_enabled;
        m["state"] = QString::fromStdString(c.stream_state);
        camList.append(m);
    }
    QVariantList intList;
    for (const auto& in : railhmi::core::build_integrations(cfg.settings)) {
        QVariantMap m;
        m["id"] = QString::fromStdString(in.id);
        m["name"] = QString::fromUtf8(in.name.c_str());
        m["address"] = QString::fromStdString(in.address);
        m["registered"] = in.registered;
        m["state"] = QString::fromStdString(in.state);
        m["protocol"] = QString::fromUtf8(in.protocol.c_str());
        m["channelMapping"] = QString::fromUtf8(in.channel_mapping.c_str());
        intList.append(m);
    }

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty("railhmiBuildDescription", build_desc);
    view.rootContext()->setContextProperty("railhmiSimulation",
                                           railhmi::core::simulation_enabled());
    view.rootContext()->setContextProperty("railhmiNav", navList);
    view.rootContext()->setContextProperty("railhmiAllPages", allList);
    view.rootContext()->setContextProperty("railhmiRole", roleId);
    view.rootContext()->setContextProperty("railhmiDefaultPage", defaultPage);
    view.rootContext()->setContextProperty("railhmiModePrimary", QString::fromUtf8(mode.primary.c_str()));
    view.rootContext()->setContextProperty("railhmiModeSecondary", QString::fromUtf8(mode.secondary.c_str()));
    view.rootContext()->setContextProperty("railhmiModeInteractive", mode.interactive);
    view.rootContext()->setContextProperty("railhmiCameras", camList);
    view.rootContext()->setContextProperty("railhmiIntegrations", intList);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (view.status() != QQuickView::Ready) {
        railhmi::platform::Logger::instance().error("QML load failed (qrc:/qml/main.qml)");
        return 3;
    }
    view.resize(1920, 1080);
    view.show();
    return app.exec();
}
