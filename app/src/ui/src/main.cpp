// railhmi Qt/QML 应用入口（骨架，ITEM-003）。
// 真正的页面/状态/取流由后续工作项接入；此处验证工程骨架可构建运行。

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QUrl>
#include <QString>

#include "railhmi/core/config.h"
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

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty("railhmiBuildDescription", build_desc);
    view.rootContext()->setContextProperty("railhmiSimulation",
                                           railhmi::core::simulation_enabled());
    view.setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (view.status() != QQuickView::Ready) {
        railhmi::platform::Logger::instance().error("QML load failed (qrc:/qml/main.qml)");
        return 3;
    }
    view.resize(1920, 1080);
    view.show();
    return app.exec();
}
