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
#include "railhmi/core/channel_view.h"
#include "railhmi/core/config.h"
#include "railhmi/core/grid.h"
#include "railhmi/core/inference.h"
#include "railhmi/core/layout.h"
#include "railhmi/core/navigation.h"
#include "railhmi/core/status.h"
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

    // 三联布局比例与高度来自 core compute_layout（单一来源，ITEM-014）。
    // QML 只消费比例/高度，窗口尺寸变化时比例保持。
    const railhmi::core::LayoutMetrics lm = railhmi::core::compute_layout(1920, 1080);
    QVariantMap layoutMap;
    layoutMap["leftRatio"] = lm.left_ratio;
    layoutMap["frontRatio"] = lm.front_ratio;
    layoutMap["rightRatio"] = lm.right_ratio;
    layoutMap["frontMinAreaRatio"] = 0.34;
    layoutMap["topBarHeight"] = lm.top_bar_h;
    layoutMap["statusStripHeight"] = lm.status_strip_h;
    layoutMap["alarmBarHeight"] = lm.alarm_bar_h;
    layoutMap["marginNarrow"] = 16;
    layoutMap["marginWide"] = 24;

    // 渲染路径由配置选定（默认 cpu；gl 可开关），接入 QML 渲染面。
    const QString renderPath = QString::fromLatin1(
        railhmi::core::to_string(cfg.settings.app.render_path));
    railhmi::platform::Logger::instance().info("render path: " + renderPath.toStdString());
    const QString roleId = QString::fromLatin1(railhmi::core::to_string(role));
    const QString defaultPage = QString::fromLatin1(railhmi::core::to_string(railhmi::core::default_page(role)));

    // 配置驱动摄像头注册表与 NVR 集成项（ITEM-012）：槽位保持 cameras.yaml 顺序；徽标由 ai_profile 决定。
    const std::vector<railhmi::core::CameraSlot> registry = railhmi::core::build_registry(cfg.settings);
    QVariantList camList;
    for (const auto& c : registry) {
        QVariantMap m;
        m["id"] = QString::fromStdString(c.id);
        m["name"] = QString::fromUtf8(c.name.c_str());
        m["position"] = QString::fromLatin1(railhmi::core::to_string(c.position));
        m["badge"] = QString::fromUtf8(c.detection_badge.c_str());
        m["aiEnabled"] = c.ai_enabled;
        m["state"] = QString::fromStdString(c.stream_state);
        camList.append(m);
    }

    // 全摄像头页每格展示视图（ITEM-015）：注册表顺序 + 运行时快照。应用尚未接入媒体
    // 取流时，各路按“未启动”占位（仍占格，不丢格、不重排）。
    QVariantList chanList;
    for (const auto& v : railhmi::core::build_channel_views(registry, {}, 0)) {
        QVariantMap m;
        m["id"] = QString::fromStdString(v.id);
        m["name"] = QString::fromUtf8(v.name.c_str());
        m["position"] = QString::fromStdString(v.position);
        m["badge"] = QString::fromUtf8(v.badge.c_str());
        m["aiEnabled"] = v.ai_enabled;
        m["slot"] = v.slot;
        m["state"] = QString::fromStdString(v.state);
        m["stateLabel"] = QString::fromUtf8(v.state_label.c_str());
        m["fpsText"] = QString::fromUtf8(v.fps_text.c_str());
        m["lastFrameText"] = QString::fromUtf8(v.last_frame_text.c_str());
        m["reconnectText"] = QString::fromUtf8(v.reconnect_text.c_str());
        m["presentable"] = v.presentable;
        m["placeholder"] = v.placeholder;
        chanList.append(m);
    }

    // 网格形状（ITEM-015）：宽屏 3x2，竖屏 2x3，随窗口尺寸切换。
    QVariantMap gridMap;
    gridMap["wideColumns"] = railhmi::core::grid_columns(railhmi::core::GridShape::ThreeByTwo);
    gridMap["wideRows"] = railhmi::core::grid_rows(railhmi::core::GridShape::ThreeByTwo);
    gridMap["tallColumns"] = railhmi::core::grid_columns(railhmi::core::GridShape::TwoByThree);
    gridMap["tallRows"] = railhmi::core::grid_rows(railhmi::core::GridShape::TwoByThree);

    // 关键状态区七项（ITEM-016）：颜色+形状字形+文字三重编码；应用未接入 AI/录像/存储采样时
    // 按“未接入”呈现，异常时本色突出；点击异常项按角色解析 allowed。
    QVariantList keyList;
    for (const auto& it : railhmi::core::build_key_status(railhmi::core::KeyStatusSnapshot{}, role)) {
        const railhmi::core::StatusClickResult r = railhmi::core::resolve_status_click(it, role);
        QVariantMap m;
        m["key"] = QString::fromStdString(it.key);
        m["label"] = QString::fromUtf8(it.label.c_str());
        m["level"] = QString::fromLatin1(railhmi::core::to_string(it.level));
        m["shape"] = QString::fromStdString(it.shape);
        m["glyph"] = QString::fromUtf8(it.glyph.c_str());
        m["color"] = QString::fromStdString(it.color);
        m["value"] = QString::fromUtf8(it.value.c_str());
        m["abnormal"] = it.abnormal;
        m["clickable"] = it.clickable;
        m["allowed"] = r.allowed;
        m["target"] = QString::fromLatin1(railhmi::core::to_string(it.target_page));
        keyList.append(m);
    }

    // 顶部模式信息条（ITEM-016）：时间（QML 实时时钟）/模式(可见光·红外未接入)/录像/AI/系统健康/未接入项。
    QVariantList topList;
    railhmi::core::TopBarSnapshot topSnap;
    topSnap.time_text = "--:--:--";
    for (const auto& it : railhmi::core::build_top_bar(topSnap, role)) {
        const railhmi::core::StatusClickResult r = railhmi::core::resolve_top_click(it, role);
        QVariantMap m;
        m["key"] = QString::fromStdString(it.key);
        m["label"] = QString::fromUtf8(it.label.c_str());
        m["level"] = QString::fromLatin1(railhmi::core::to_string(it.level));
        m["shape"] = QString::fromStdString(it.shape);
        m["glyph"] = QString::fromUtf8(it.glyph.c_str());
        m["color"] = QString::fromStdString(it.color);
        m["value"] = QString::fromUtf8(it.value.c_str());
        m["secondary"] = QString::fromUtf8(it.secondary.c_str());
        m["unavailable"] = it.unavailable;
        m["abnormal"] = railhmi::core::status_abnormal(it.level);
        m["clickable"] = it.clickable;
        m["allowed"] = r.allowed;
        m["target"] = QString::fromLatin1(railhmi::core::to_string(it.target_page));
        topList.append(m);
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
    view.rootContext()->setContextProperty("railhmiChannels", chanList);
    view.rootContext()->setContextProperty("railhmiGrid", gridMap);
    view.rootContext()->setContextProperty("railhmiKeyStatus", keyList);
    view.rootContext()->setContextProperty("railhmiTopBar", topList);

    // AI 调试页（ITEM-017）：仅 ai_enabled 相机；应用尚未启动推理线程时每路显式“AI 不可用”。
    railhmi::core::AiRuntime aiRt;
    aiRt.worker_alive = false;
    for (const auto& c : registry) {
        railhmi::core::AiChannelRuntime ch;
        ch.camera_id = c.id;
        ch.profile = c.ai_profile;
        for (const auto& m : cfg.settings.app.ai.models)
            if (m.profile == c.ai_profile) ch.model_version = m.version;
        aiRt.channels.push_back(ch);
    }
    QVariantList aiList;
    for (const auto& d : railhmi::core::build_ai_debug(registry, aiRt, {}, 0)) {
        QVariantMap m;
        m["cameraId"] = QString::fromStdString(d.camera_id);
        m["name"] = QString::fromUtf8(d.name.c_str());
        m["profile"] = QString::fromLatin1(railhmi::core::to_string(d.profile));
        m["profileLabel"] = QString::fromUtf8(d.profile_label.c_str());
        m["modelVersion"] = QString::fromStdString(d.model_version);
        m["available"] = d.available;
        m["unavailableReason"] = QString::fromUtf8(d.unavailable_reason.c_str());
        char fpsbuf[32];
        std::snprintf(fpsbuf, sizeof(fpsbuf), "%.1f", d.fps);
        m["fpsText"] = QString::fromLatin1(fpsbuf);
        m["latencyMs"] = d.latency_ms;
        m["lastResultText"] = QString::fromUtf8(d.last_result_text.c_str());
        m["confidenceText"] = d.boxes.empty() ? QString::fromUtf8("—")
                                               : QString::number(d.max_confidence * 100, 'f', 0) + "%";
        QVariantList boxes;
        for (const auto& b : d.boxes) {
            QVariantMap bm;
            bm["x"] = b.x; bm["y"] = b.y; bm["w"] = b.w; bm["h"] = b.h;
            bm["confidence"] = b.confidence; bm["label"] = QString::fromStdString(b.label);
            boxes.append(bm);
        }
        m["boxes"] = boxes;
        QVariantList roi;
        for (const auto& r : cfg.settings.roi) {
            if (r.camera != d.camera_id) continue;
            QVariantMap rm;
            rm["name"] = QString::fromUtf8(r.name.c_str());
            rm["x"] = r.x; rm["y"] = r.y; rm["w"] = r.w; rm["h"] = r.h;
            roi.append(rm);
        }
        m["roi"] = roi;
        aiList.append(m);
    }
    view.rootContext()->setContextProperty("railhmiAiDebug", aiList);
    view.rootContext()->setContextProperty("railhmiIntegrations", intList);
    view.rootContext()->setContextProperty("railhmiLayout", layoutMap);
    view.rootContext()->setContextProperty("railhmiRenderPath", renderPath);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (view.status() != QQuickView::Ready) {
        railhmi::platform::Logger::instance().error("QML load failed (qrc:/qml/main.qml)");
        return 3;
    }
    view.resize(1920, 1080);
    view.show();
    return app.exec();
}
