// railhmi Qt/QML 应用入口（骨架，ITEM-003）。
// 真正的页面/状态/取流由后续工作项接入；此处验证工程骨架可构建运行。

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QUrl>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <map>
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

#include "alarm_model.h"
#include "replay_model.h"
#include "maintenance_model.h"
#include "permissions_model.h"
#include "health_model.h"

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
        m["distanceText"] = QString::fromUtf8(d.distance_text.c_str());
        QVariantList boxes;
        for (const auto& b : d.boxes) {
            QVariantMap bm;
            bm["x"] = b.x; bm["y"] = b.y; bm["w"] = b.w; bm["h"] = b.h;
            bm["confidence"] = b.confidence; bm["label"] = QString::fromStdString(b.label);
            bm["distanceM"] = static_cast<double>(b.distance_m);
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

    // 报警中心（ITEM-018）：桥接对象（过滤/排序/确认委托 core）。持久化事件接入前，
    // 仅模拟开启时灌入样例用于页面自检；否则为空 -> 页面显示空态。
    RailhmiAlarmModel alarmModel;
    if (cfg.settings.app.simulation) {
        std::map<std::string, std::string> names;
        std::map<std::string, railhmi::core::RecordAvailability> rec;
        for (const auto& c : registry) names[c.id] = c.name;
        const long long t0 = 1760000000000LL;   // 固定基准（模拟）
        auto mk = [&](long long id, int level, const char* type, const char* cam, long long off,
                      const char* ack, const char* desc, const char* snap) {
            railhmi::core::EventRow e;
            e.id = id; e.level = level; e.type = type; e.source_cam = cam; e.ts_wall_ms = t0 + off;
            e.ack_state = ack; e.description = desc; e.snapshot_ref = snap;
            return e;
        };
        std::vector<railhmi::core::EventRow> evs;
        evs.push_back(mk(1, 1, "person_on_rail", "CAM-01", 5000, "unacked", "轨道人员", "s1"));
        evs.push_back(mk(2, 3, "side_camera_device", "CAM-02", 3000, "unacked", "侧视离线", ""));
        evs.push_back(mk(3, 2, "pantograph_abnormal", "CAM-04", 9000, "unacked", "受电弓异常", "s3"));
        evs.push_back(mk(4, 1, "wheel_derail", "CAM-05", 1000, "acked", "轮轨异常", "s4"));
        rec["CAM-01"] = railhmi::core::RecordAvailability::Available;
        rec["CAM-04"] = railhmi::core::RecordAvailability::Incomplete;
        alarmModel.seedEvents(evs, names, rec);
    }
    view.rootContext()->setContextProperty("railhmiAlarms", &alarmModel);

    // 历史回放（ITEM-020）：桥接对象（时间轴/跳转/同步/来源/选择委托 core）。模拟开启时灌入样例。
    // 修复 D-01：按选中报警动态生成详情；修复 D-02：同步状态由各路真实时钟偏移构建。
    RailhmiReplayModel replayModel;
    replayModel.setSyncThreshold(cfg.settings.thresholds.replay_sync_threshold_ms);
    if (cfg.settings.app.simulation) {
        const long long t0 = 1760000000000LL;
        auto seg = [&](long long id, const char* cam, long long a, long long b, bool crit) {
            railhmi::core::RecordingSegmentRow r;
            r.id = id; r.cam = cam; r.t0_ms = t0 + a; r.t1_ms = t0 + b;
            r.kind = crit ? railhmi::core::SegmentKind::Critical : railhmi::core::SegmentKind::Continuous;
            r.complete = true;
            return r;
        };
        std::vector<railhmi::core::RecordingSegmentRow> segs = {
            seg(1, "CAM-01", 0, 4000, false), seg(2, "CAM-01", 4000, 120000, true),
            seg(3, "CAM-01", 130000, 300000, false), seg(11, "CAM-02", 0, 300000, false),
            seg(12, "CAM-03", 0, 300000, false)};
        std::vector<railhmi::core::RecordingGapRow> gaps;
        railhmi::core::RecordingGapRow gp;
        gp.cam = "CAM-01"; gp.t0_ms = t0 + 200000; gp.t1_ms = t0 + 250000; gp.reason = "recording_interrupted";
        gaps.push_back(gp);
        std::vector<long long> alarms = {t0 + 5000};
        replayModel.seed(segs, gaps, alarms, false /*nvr 未核验/不可用*/, true /*本机片段可用*/);

        // 与报警中心样例一致的事件行（选择哪个报警就动态生成哪个详情）
        auto mkrow = [&](long long id, int lvl, const char* type, const char* cam, const char* name,
                         long long off, const char* desc, const char* ack) {
            railhmi::core::EventRow e;
            e.id = id; e.level = lvl; e.type = type; e.source_cam = cam; e.ts_wall_ms = t0 + off;
            e.description = desc; e.ack_state = ack;
            return railhmi::core::make_alarm_row(e, name, railhmi::core::RecordAvailability::Available);
        };
        std::vector<railhmi::core::AlarmRow> rows;
        rows.push_back(mkrow(1, 1, "person_on_rail", "CAM-01", "前方", 5000, "轨道人员", "acked"));
        rows.push_back(mkrow(3, 2, "pantograph_abnormal", "CAM-04", "车顶", 9000, "受电弓异常", "unacked"));
        rows.push_back(mkrow(4, 1, "wheel_derail", "CAM-05", "车底", 1000, "轮轨异常", "unacked"));
        replayModel.seedAlarms(rows, {12.34, -1.0, 3.2}, {0.87, -1.0, 0.91});

        // 各路回放时钟偏移（前方报警三联）；真实媒体源接入前由桥接注入，无时钟时页面显示“同步状态不可用”
        std::map<std::string, long long> offs;
        offs["CAM-02"] = 0;
        offs["CAM-01"] = 120;
        offs["CAM-03"] = 900;   // 超阈值 300 -> 同步偏差提示可达
        replayModel.setCameraOffsets(offs);
        replayModel.selectAlarm("CAM-01", t0 + 5000);
    } else {
        replayModel.seed({}, {}, {}, false, false);
    }
    view.rootContext()->setContextProperty("railhmiReplay", &replayModel);

    // 设备维护（ITEM-022）：云台容差/归位/预置位（二次确认）/日志/录像/健康委托 core。
    RailhmiMaintenanceModel maintModel;
    QVariantList maintCams;
    if (cfg.settings.app.simulation) {
        const long long tnow = 1760000000000LL;
        std::vector<railhmi::core::CamRuntime> rt(registry.size());
        for (std::size_t i = 0; i < rt.size(); ++i) {
            rt[i].has_stream = true;
            rt[i].state = railhmi::core::StreamState::Live;
            rt[i].fps = 25.0;
            rt[i].last_frame_ms = tnow - 400;
        }
        if (rt.size() > 1) {
            rt[1].state = railhmi::core::StreamState::Frozen;
            rt[1].fps = 0.0;
            rt[1].last_frame_ms = tnow - 10000;
            rt[1].quality.too_dark = true;
        }
        if (rt.size() > 3) rt[3].reconnect_count = 2;
        for (const auto& r : railhmi::core::build_maintenance_cameras(registry, rt, tnow)) {
            QVariantMap m;
            m["id"] = QString::fromStdString(r.id);
            m["name"] = QString::fromUtf8(r.name.c_str());
            m["position"] = QString::fromStdString(r.position);
            m["online"] = r.online;
            m["stateLabel"] = QString::fromUtf8(r.state_label.c_str());
            m["fpsText"] = QString::fromStdString(r.fps_text);
            m["lastFrameText"] = QString::fromUtf8(r.last_frame_text.c_str());
            m["qualityLabel"] = QString::fromUtf8(r.quality_label.c_str());
            m["reconnectText"] = QString::fromUtf8(r.reconnect_text.c_str());
            maintCams.append(m);
        }
        railhmi::core::PtzState ptz;
        ptz.pan_target = 0; ptz.pan_actual = 15;   // 初始超差 -> 前方 AI NOT_READY
        ptz.tilt_target = 0; ptz.tilt_actual = 8;
        ptz.zoom_target = 1; ptz.zoom_actual = 2;
        railhmi::core::HealthSnapshot hs;
        hs.npu_load = 42; hs.cpu = 33; hs.temp_c = 58; hs.net_errors = 3;
        railhmi::core::RecordingStatus rs = railhmi::core::recording_status(
            true, 5LL * 1024 * 1024 * 1024, 20LL * 1024 * 1024 * 1024);
        maintModel.seed(ptz, railhmi::core::PtzTolerance{}, true, hs, rs, "maintainer");
    }
    view.rootContext()->setContextProperty("railhmiMaintenance", &maintModel);
    view.rootContext()->setContextProperty("railhmiMaintenanceCameras", maintCams);

    // 权限与二次确认（ITEM-023）：权限矩阵/门控/导出·配置修改写维护日志委托 core。
    RailhmiPermissionsModel permsModel;
    permsModel.seed(role, cfg.settings.app.permissions, QString::fromLatin1(railhmi::core::to_string(role)));
    view.rootContext()->setContextProperty("railhmiPermissions", &permsModel);

    // 健康/结构化日志/自恢复（ITEM-024）：采样评估/日志查询/恢复进度委托 core。
    RailhmiHealthModel healthModel;
    {
        railhmi::core::HealthSample hs;
        hs.ts_wall_ms = 1760000000000LL;
        hs.npu_load = 42; hs.cpu = 33; hs.temp_c = 58; hs.net_errors = 3;
        hs.disk_free = 15LL * 1024 * 1024 * 1024; hs.disk_total = 20LL * 1024 * 1024 * 1024;
        std::vector<railhmi::core::LogRecord> logs;
        const long long day = hs.ts_wall_ms;
        logs.push_back(railhmi::core::LogRecord{day, railhmi::core::LogSeverity::Info, "app", "启动完成", {{"role", "driver"}}});
        logs.push_back(railhmi::core::LogRecord{day + 1000, railhmi::core::LogSeverity::Error, "stream", "CAM-04 断流", {{"cam", "CAM-04"}}});
        logs.push_back(railhmi::core::LogRecord{day - 2 * 86400000LL, railhmi::core::LogSeverity::Warn, "ptz", "云台超差", {{"delta", "4.0"}}});
        railhmi::core::RecoveryTracker rec;
        rec.start(day);
        const railhmi::core::RecoveryStep seq[8] = {
            railhmi::core::RecoveryStep::LoadConfig, railhmi::core::RecoveryStep::OpenDatabase,
            railhmi::core::RecoveryStep::StartRecording, railhmi::core::RecoveryStep::StartStreaming,
            railhmi::core::RecoveryStep::StartAi, railhmi::core::RecoveryStep::PtzHome,
            railhmi::core::RecoveryStep::FrontPreset, railhmi::core::RecoveryStep::FrontAiReady};
        for (int i = 0; i < 8; ++i) {
            if (seq[i] == railhmi::core::RecoveryStep::StartStreaming) rec.camera_result(true);
            else rec.complete_step(seq[i], true);
        }
        rec.finish(day + 1000);
        healthModel.seed(hs, railhmi::core::HealthThresholds{}, logs, rec.state());
    }
    view.rootContext()->setContextProperty("railhmiHealth", &healthModel);
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
