// railhmi Qt/QML 应用入口（骨架，ITEM-003）。
// 真正的页面/状态/取流由后续工作项接入；此处验证工程骨架可构建运行。

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QUrl>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

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
    view.setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (view.status() != QQuickView::Ready) {
        railhmi::platform::Logger::instance().error("QML load failed (qrc:/qml/main.qml)");
        return 3;
    }
    view.resize(1920, 1080);
    view.show();
    return app.exec();
}
