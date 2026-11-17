// railhmi Qt/QML 应用入口：配置驱动六路取流与实时视频显示。

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QQmlEngine>
#include <qqml.h>
#include <QQuickItem>
#include <QRect>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/channel_view.h"
#include "railhmi/core/config.h"
#include "railhmi/core/fault_catalog.h"
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
#include "maintenance_ops.h"
#include "permissions_model.h"
#include "health_model.h"
#include "video_runtime.h"
#include "ai_runtime.h"
#include "door_sensor_runtime.h"
#include "vehicle_state.h"
#include "nvr_status.h"
#include "credential_env.h"
#include "system_metrics.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

// 解析部署参数 `--config <dir>`（或 `--config=<dir>`）；未提供时回退编译期缺省。
// systemd 单元以 `--config /opt/railhmi/config` 启动，故此路径必须真正被消费（ITEM-025 D-02）。
int main(int argc, char** argv) {
    const std::string configDir =
        railhmi::core::parse_config_dir(argc, argv, RAILHMI_CONFIG_DIR);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("railhmi");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(railhmi::core::version_string()));

    const QString build_desc = QString::fromStdString(railhmi::core::build_description());
    railhmi::platform::Logger::instance().info(build_desc.toStdString());
    railhmi::platform::Logger::instance().info("config dir: " + configDir);

    // 启动即校验配置；非法配置报出具体字段并非零退出（绝不静默使用默认值）。
    railhmi::core::ConfigLoadResult cfg = railhmi::core::load_config_dir(configDir);
    if (!cfg.ok) {
        railhmi::platform::Logger::instance().error("configuration invalid; refusing to start");
        for (const auto& e : cfg.errors) {
            railhmi::platform::Logger::instance().error("  " + e.str());
        }
        return 4;
    }
    railhmi::platform::Logger::instance().info(
        "configuration OK: " + std::to_string(cfg.settings.cameras.size()) + " cameras");

    // 固定凭据在启动阶段从部署私有 0600 文件补齐（systemd EnvironmentFile 之外也生效）。
    // rtc/NVR 的密码不写入源码，也不再由用户在页面输入；日志只报补齐数量，绝不打印值。
    const int credentialVars =
        loadPrivateCredentialFiles(QString::fromStdString(configDir));
    railhmi::platform::Logger::instance().info(
        "credentials: " + std::to_string(credentialVars) + " private env var(s) loaded from config dir");

    // 显示尺寸（分辨率配置驱动 + 全屏自适应）：
    //   - 默认跟随物理屏幕实际尺寸全屏显示，换屏不需要改代码；
    //   - app.resolution 作为设计/回退分辨率，在无窗口系统、或显式 --windowed /
    //     RAILHMI_WINDOWED=1（离屏验证、桌面预览）时生效；
    //   - compute_layout、QML 根尺寸均与运行时尺寸同源，不再固定 1920x1080。
    bool windowed = qEnvironmentVariableIsSet("RAILHMI_WINDOWED");
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::string(argv[i]) == "--windowed") windowed = true;
    }
    int displayW = cfg.settings.app.resolution_w;
    int displayH = cfg.settings.app.resolution_h;
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!windowed && screen) {
        const QRect geo = screen->geometry();
        if (geo.width() >= 640 && geo.height() >= 480) {
            displayW = geo.width();
            displayH = geo.height();
        }
    }
    railhmi::platform::Logger::instance().info(
        "display: config=" + std::to_string(cfg.settings.app.resolution_w) + "x" +
        std::to_string(cfg.settings.app.resolution_h) +
        " runtime=" + std::to_string(displayW) + "x" + std::to_string(displayH) +
        (windowed ? " (windowed)" : " (fullscreen)"));

    // 全部六个功能页均注入导航，不按角色隐藏。
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
    // 使用运行时显示尺寸，配置/屏幕改变时布局随之改变（ITEM-014 ratios_preserved 仍成立）。
    const railhmi::core::LayoutMetrics lm = railhmi::core::compute_layout(displayW, displayH);
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
    QString defaultPage = QString::fromLatin1(railhmi::core::to_string(railhmi::core::default_page(role)));
    // 启动页可被环境变量覆盖（RAILHMI_START_PAGE=realtime|alarm_center|replay|all_cameras|ai_debug|maintenance）。
    // 仅接受导航里真实存在的页 id；用于现场/离屏验收与定制开机页，非法值忽略并保持默认。
    {
        const QString want = qEnvironmentVariable("RAILHMI_START_PAGE");
        if (!want.isEmpty()) {
            for (const QVariant& v : navList) {
                if (v.toMap().value("id").toString() == want) {
                    defaultPage = want;
                    railhmi::platform::Logger::instance().info(
                        "start page overridden: " + want.toStdString());
                    break;
                }
            }
        }
    }

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
        // 维护页「摄像头检测」按 RTSP 地址取主机名做 ping，故一并下发（只读、无凭据）。
        m["url"] = QString::fromStdString(c.rtsp_url);
        camList.append(m);
    }

    // 生命周期覆盖 QQuickView；所有页面共享同一路取流，不随页面切换重建。
    qmlRegisterUncreatableType<VideoChannel>("Railhmi.Video", 1, 0, "VideoChannel", "Provided by runtime");
    qmlRegisterType<VideoFrameItem>("Railhmi.Video", 1, 0, "VideoFrame");
    VideoRuntime videoRuntime(cfg.settings);

    // 网格形状（ITEM-015）：宽屏 3x2，竖屏 2x3，随窗口尺寸切换。
    QVariantMap gridMap;
    gridMap["wideColumns"] = railhmi::core::grid_columns(railhmi::core::GridShape::ThreeByTwo);
    gridMap["wideRows"] = railhmi::core::grid_rows(railhmi::core::GridShape::ThreeByTwo);
    gridMap["tallColumns"] = railhmi::core::grid_columns(railhmi::core::GridShape::TwoByThree);
    gridMap["tallRows"] = railhmi::core::grid_rows(railhmi::core::GridShape::TwoByThree);

    // 顶栏真值来源（现场需求）：全部由运行时真实数据注入，缺省一律未接入/异常，绝不臆测。
    //   * doorStatus       : 关键状态区「车门」项沿用 BLE 汇总原文（全关/1 路未关严…）
    //   * doorSummaryLevel : 顶栏「门磁」三态（开门/关门/传感器异常）的映射输入
    //   * nvrConnected     : 顶栏「录像」位，来自 NvrStatusRuntime 的真实 TCP 连接探测
    //   * vehicleState     : 顶栏「车辆状态」人工置位（无车速/GPS 数据源，不伪装成传感器真值）
    railhmi::core::CategoryStatus doorStatus;
    std::string doorSummaryLevel;
    bool nvrConnected = false;
    // AI 模型接入真值（推理线程存活 + 至少一路模型就绪）；由 AiRuntime::statusChanged 刷新。
    bool aiReadyFlag = false;
    VehicleStateRuntime vehicleState;
    NvrStatusRuntime nvrStatus(QString::fromStdString(configDir) + QStringLiteral("/nvr.yaml"));

    // 摄像头实时路数：presentable = core may_present_as_live（LIVE 且质量可用），
    // 顶栏「摄像头」与关键状态区共用同一口径。
    auto liveCount = [&]() {
        int live = 0;
        for (const auto& value : videoRuntime.channels())
            if (qvariant_cast<VideoChannel*>(value)->info().value("presentable").toBool()) ++live;
        return live;
    };

    // 关键状态区七项（ITEM-016）：颜色+形状字形+文字三重编码；应用未接入 AI/录像/存储采样时
    // 按“未接入”呈现，异常时本色突出；点击异常项按角色解析 allowed。
    auto buildKeyList = [&]() {
        railhmi::core::KeyStatusSnapshot snapshot;
        snapshot.door = doorStatus;      // 车门项由 BLE 门磁广播真值驱动
        int live = 0, started = 0, offline = 0;
        const auto channels = videoRuntime.channels();
        for (const auto& value : channels) {
            const auto info = qvariant_cast<VideoChannel*>(value)->info();
            if (info.value("presentable").toBool()) ++live;
            const auto state = info.value("state").toString();
            if (state != "unstarted") ++started;
            if (state == "offline") ++offline;
        }
        if (started > 0) {
            snapshot.camera.level = live == channels.size() ? railhmi::core::StatusLevel::Ok
                : offline > 0 ? railhmi::core::StatusLevel::Alarm
                              : railhmi::core::StatusLevel::Warning;
            snapshot.camera.value = std::to_string(live) + "/" + std::to_string(channels.size()) + " 路实时";
        }
        QVariantList result;
        for (const auto& it : railhmi::core::build_key_status(snapshot, role)) {
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
            result.append(m);
        }
        return result;
    };
    QVariantList keyList = buildKeyList();

    // 顶部状态条（现场需求精简）：时间 / 摄像头 / 录像 / 系统健康 / 车辆状态 / 门磁。
    // 删除「模式（可见光·红外未接入）」与「AI」；「车速」改为人工可置位的「车辆状态」。
    // 全部真值由下面的映射函数在 core 侧统一生成，UI 不散落文案；构建逻辑做成可重入 lambda，
    // 门磁/摄像头/NVR/车辆状态任一变化时重建。
    railhmi::core::TopBarSnapshot topSnap;
    topSnap.time_text = "--:--:--";
    auto buildTopList = [&]() {
        topSnap.camera = railhmi::core::camera_top_status(
            liveCount(), static_cast<int>(videoRuntime.channels().size()));
        topSnap.recording = railhmi::core::nvr_top_status(nvrConnected);
        // AI 模型接入位（2026-11-17）：只报「接入/未接入」真实结论，模型名在 P5/P6 展示。
        topSnap.ai_model =
            railhmi::core::ai_model_top_status(aiReadyFlag, aiReadyFlag ? 1 : 0);
        // 系统健康仍无真实采样源接入，保持缺省「未接入」，不伪造正常。
        topSnap.vehicle_state = railhmi::core::vehicle_state_top_status(vehicleState.mode());
        topSnap.door_sensor = railhmi::core::door_top_status(doorSummaryLevel);
        QVariantList out;
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
            out.append(m);
        }
        return out;
    };
    QVariantList topList = buildTopList();
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
    view.rootContext()->setContextProperty("railhmiScreenWidth", displayW);
    view.rootContext()->setContextProperty("railhmiScreenHeight", displayH);
    view.rootContext()->setContextProperty("railhmiWindowed", windowed);
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
    view.rootContext()->setContextProperty("railhmiChannels", videoRuntime.channels());
    view.rootContext()->setContextProperty("railhmiGrid", gridMap);
    view.rootContext()->setContextProperty("railhmiKeyStatus", keyList);
    view.rootContext()->setContextProperty("railhmiTopBar", topList);
    view.rootContext()->setContextProperty("railhmiVehicleState", &vehicleState);

    // 顶栏资源读数：内存 / 每个 CPU 核 / 每个 NPU 核，全部来自 /proc、/sys 真实采样。
    // 每 2s 更新一次；取不到的维度显示「—」或「聚合」，绝不用 0 冒充正常。
    SystemMetricsRuntime systemMetrics;
    view.rootContext()->setContextProperty("railhmiSystemMetrics", &systemMetrics);

    // 任一真值源变化时重建顶栏；内容相同则不重设上下文属性，避免无谓的 QML 重绑定。
    auto republishTop = [&]() {
        const QVariantList updatedTop = buildTopList();
        if (updatedTop == topList) return;
        topList = updatedTop;
        view.rootContext()->setContextProperty("railhmiTopBar", topList);
    };

    for (const auto& value : videoRuntime.channels()) {
        QObject::connect(qvariant_cast<VideoChannel*>(value), &VideoChannel::infoChanged, &view, [&]() {
            const auto updated = buildKeyList();
            if (updated != keyList) {
                keyList = updated;
                view.rootContext()->setContextProperty("railhmiKeyStatus", keyList);
            }
            // 摄像头接入路数变化同步刷新顶栏「x/N 路实时」。
            republishTop();
        });
    }

    // 车辆状态：顶栏点击 -> cycle() -> 重建顶栏（人工置位，不臆测传感器真值）。
    QObject::connect(&vehicleState, &VehicleStateRuntime::changed, &view, republishTop);
    // 录像位：只认 NvrStatusRuntime 的真实 TCP 连接探测结果，探测不到一律 NVR异常。
    QObject::connect(&nvrStatus, &NvrStatusRuntime::changed, &view, [&](bool connected) {
        nvrConnected = connected;
        republishTop();
    });

    // AI 推理接入（2026-11-17）：真实 RKNN 工作线程（media::VisionAiWorker）+ 逐路报警判定。
    //   * 车前方/受电弓/轮轨三路当前**共用同一个检测模型**（app.ai.reuse_detect_for_duties）；
    //     车前方另用分割模型做「轨道掩码 ∩ 人/物」两阶段判定，判定结果即 in_roi 标志；
    //   * 模型名/路径全部来自配置，P5 可现场替换（railhmiAi.selectModel）；
    //   * 未起线程 / 模型未就绪 / 无帧时如实显示「未接入/不可用」，不伪造检测框与报警。
    AiRuntime aiRuntime(cfg.settings, QString::fromStdString(configDir), &videoRuntime);
    view.rootContext()->setContextProperty("railhmiAi", &aiRuntime);
    view.rootContext()->setContextProperty("railhmiAiDebug", aiRuntime.channels());
    view.rootContext()->setContextProperty("railhmiAiCatalog", aiRuntime.catalog());
    view.rootContext()->setContextProperty("railhmiAiAlarms", aiRuntime.alarms());
    QObject::connect(&aiRuntime, &AiRuntime::channelsChanged, &view, [&]() {
        view.rootContext()->setContextProperty("railhmiAiDebug", aiRuntime.channels());
        view.rootContext()->setContextProperty("railhmiAiCatalog", aiRuntime.catalog());
    });
    // P1 三画面红框判据：所有受检路都在表里（未报警为 false）。
    QObject::connect(&aiRuntime, &AiRuntime::alarmsChanged, &view, [&]() {
        view.rootContext()->setContextProperty("railhmiAiAlarms", aiRuntime.alarms());
    });
    QObject::connect(&aiRuntime, &AiRuntime::statusChanged, &view, [&]() {
        aiReadyFlag = aiRuntime.aiReady();
        republishTop();
    });

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

    // AI 报警跃迁 -> 报警中心实时事件（只追加，不删除；确认只改状态）。
    // 事件类型 token 与契约一致（person_on_rail / front_obstacle / wheel_derail / pantograph_abnormal）。
    QObject::connect(&aiRuntime, &AiRuntime::alarmRaised, &view,
                     [&](const QString& cameraId, int level, const QString& type,
                         const QString& description) {
        QString sourceName = cameraId;
        for (const auto& c : registry)
            if (QString::fromStdString(c.id) == cameraId) sourceName = QString::fromUtf8(c.name.c_str());
        alarmModel.addLiveEvent(type, level, cameraId, sourceName, description);
        railhmi::platform::Logger::instance().info(
            "ai-alarm: " + type.toStdString() + " cam=" + cameraId.toStdString() +
            " level=" + std::to_string(level));
    });
    QObject::connect(&aiRuntime, &AiRuntime::alarmCleared, &view, [&](const QString& cameraId) {
        railhmi::platform::Logger::instance().info(
            "ai-alarm: cleared cam=" + cameraId.toStdString());
    });

    // ---- BLE 门磁（BlueZ 广播）----
    // core 只做报文解析/汇总；这里把汇总接到顶栏门磁位、关键状态「车门」项、
    // P1 当前报警条、P6 维护设备卡，并在「非未关严 -> 未关严」跃迁时追加 L2 报警事件。
    // 无真实设备/未启用/BlueZ 故障时统一显示不可用，绝不伪装成「门已关」。
    DoorSensorRuntime doorRuntime(cfg.settings.app.door_sensor);
    view.rootContext()->setContextProperty("railhmiDoorDevices", QVariantList());
    view.rootContext()->setContextProperty("railhmiDoorSummary", QVariantMap());
    view.rootContext()->setContextProperty("railhmiDoorAlarmText", QString());

    auto doorLevel = [](const QString& token) {
        if (token == QLatin1String("ok")) return railhmi::core::StatusLevel::Ok;
        if (token == QLatin1String("info")) return railhmi::core::StatusLevel::Info;
        if (token == QLatin1String("warning")) return railhmi::core::StatusLevel::Warning;
        if (token == QLatin1String("alarm")) return railhmi::core::StatusLevel::Alarm;
        return railhmi::core::StatusLevel::Unavailable;
    };
    auto publishDoor = [&]() {
        const QVariantMap sum = doorRuntime.summary();
        const QString levelToken = sum.value(QStringLiteral("level")).toString();
        doorSummaryLevel = levelToken.toStdString();   // 顶栏三态映射输入
        railhmi::core::CategoryStatus d;
        d.level = doorLevel(levelToken);
        d.value = sum.value(QStringLiteral("value")).toString().toStdString();
        if (d.value.empty()) d.value = "未接入";
        doorStatus = d;

        const QVariantList updatedKey = buildKeyList();
        if (updatedKey != keyList) {
            keyList = updatedKey;
            view.rootContext()->setContextProperty("railhmiKeyStatus", keyList);
        }
        republishTop();
        view.rootContext()->setContextProperty("railhmiDoorDevices", doorRuntime.devices());
        view.rootContext()->setContextProperty("railhmiDoorSummary", sum);
        view.rootContext()->setContextProperty("railhmiDoorAlarmText",
                                              sum.value(QStringLiteral("alarmText")).toString());
    };
    QObject::connect(&doorRuntime, &DoorSensorRuntime::changed, &view, publishDoor);
    QObject::connect(&doorRuntime, &DoorSensorRuntime::doorAjar, &view,
                     [&](const QString& id, const QString& description) {
        // map_level(AlarmType::DoorAjar) == L2 -> EventRow.level = 2（test_rules.cpp 已固定）。
        // 不伪造录像/快照关联：RecordAvailability::Unavailable。
        alarmModel.addLiveEvent(QStringLiteral("door_ajar"), 2, id,
                                QString::fromUtf8("门磁"), description);
        railhmi::platform::Logger::instance().info(
            "door-sensor: L2 door_ajar event appended for " + id.toStdString());
    });
    publishDoor();                                  // 启动即给出真值（通常「未接入」）
    QTimer::singleShot(0, &doorRuntime, &DoorSensorRuntime::start);

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
        replayModel.configureNvr(QString::fromStdString(configDir) + "/nvr.yaml");
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

    // 设备维护页「日志显示 + 功能操作」（2026-11-17）：ping 摄像头、NVR 登录检测、
    // 系统信息与日志保存/导出。过程信息统一写进同一份日志（同时进结构化日志）。
    MaintenanceOps maintOps(cfg.settings, QString::fromStdString(configDir));
    view.rootContext()->setContextProperty("railhmiMaintOps", &maintOps);

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

    // 故障自检目录（ITEM-025）：FI-01..10 显式提示 + 恢复路径，供维护页展示。
    QVariantList faultList;
    for (const auto& f : railhmi::core::fault_catalog()) {
        QVariantMap m;
        m["id"] = QString::fromStdString(f.id);
        m["label"] = QString::fromUtf8(f.label.c_str());
        m["prompt"] = QString::fromUtf8(f.prompt.c_str());
        m["recovery"] = QString::fromUtf8(f.recovery.c_str());
        faultList.append(m);
    }
    view.rootContext()->setContextProperty("railhmiFaults", faultList);
    view.rootContext()->setContextProperty("railhmiIntegrations", intList);
    view.rootContext()->setContextProperty("railhmiLayout", layoutMap);
    view.rootContext()->setContextProperty("railhmiRenderPath", renderPath);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (view.status() != QQuickView::Ready) {
        railhmi::platform::Logger::instance().error("QML load failed (qrc:/qml/main.qml)");
        return 3;
    }
    view.resize(displayW, displayH);
    if (windowed) view.show();
    else view.showFullScreen();

    // 可选几何自检（RAILHMI_DUMP_LAYOUT=1）：打印关键元素实际宽高，用于板端/离屏验证
    // 视频格子是否严格 16:9、以及全屏尺寸是否生效。正常运行不产生任何输出。
    if (qEnvironmentVariableIsSet("RAILHMI_DUMP_LAYOUT")) {
        // QML 由 Loader/Repeater 动态创建的项不在 QObject 父子链里，须遍历视觉树。
        auto dump = [&view]() {
            QQuickItem* root = view.rootObject();
            if (!root) return;
            auto& log = railhmi::platform::Logger::instance();
            log.info("LAYOUT root=" + std::to_string(static_cast<int>(root->width())) + "x" +
                     std::to_string(static_cast<int>(root->height())));
            const QStringList watched = {"threePanelRow", "tileFront", "tileLeft", "tileRight",
                                     "bottomRow", "tilePantograph", "tileWheelRail",
                                     "alarmBar", "videoLayer",
                                     "zoomOverlay", "gridCell-CAM-01",
                                     // 现场定稿 P3：画面区域/单画面几何与关键帧条占位自检
                                     "replayVideoArea", "replayPanel-CAM-01",
                                     "keyframeStrip", "detailRecordBar",
                                     "topResourceMeters"};
            std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
                if (!item) return;
                if (watched.contains(item->objectName())) {
                    log.info("LAYOUT " + item->objectName().toStdString() + "=" +
                             std::to_string(static_cast<int>(item->width())) + "x" +
                             std::to_string(static_cast<int>(item->height())));
                }
                for (QQuickItem* child : item->childItems()) walk(child);
            };
            walk(root);
        };
        QTimer::singleShot(1500, &view, dump);   // 默认页（P4）
        QTimer::singleShot(9000, &view, dump);   // 切到 P1 之后
        QTimer::singleShot(14000, &view, dump);  // 验收脚本此时切到 P3，读回放画面几何
    }

    QTimer::singleShot(0, &videoRuntime, &VideoRuntime::start);
    // 推理在取流之后启动：先有租约再入队，避免空跑与首帧误报。
    QTimer::singleShot(800, &aiRuntime, &AiRuntime::start);

    // 维护操作的启动脚本（RAILHMI_MAINT_RUN=camera,nvr,system）：只用于现场/离屏验收，
    // 与 P6 页面上五个按钮调用的是同一批方法；默认不设置 = 什么都不做。
    {
        const QString maintRun = qEnvironmentVariable("RAILHMI_MAINT_RUN");
        if (!maintRun.isEmpty()) {
            QTimer::singleShot(2500, &view, [&maintOps, &registry, maintRun, &videoRuntime,
                                             &aiRuntime]() {
                const QStringList steps = maintRun.split(QLatin1Char(','));
                for (const QString& step : steps) {
                    if (step == QLatin1String("camera")) {
                        QVariantList cams;
                        for (const auto& c : registry)
                            cams.append(QVariantMap{{"id", QString::fromStdString(c.id)},
                                                    {"name", QString::fromStdString(c.name)},
                                                    {"url", QString::fromStdString(c.rtsp_url)}});
                        maintOps.pingCameras(cams);
                    } else if (step == QLatin1String("nvr")) {
                        maintOps.checkNvr();
                    } else if (step == QLatin1String("system")) {
                        const QVariantList info = maintOps.systemInfo();
                        maintOps.appendLog(QStringLiteral("系统信息"), QStringLiteral("===== 系统信息 ====="));
                        for (const QVariant& v : info) {
                            const QVariantMap m = v.toMap();
                            maintOps.appendLog(QStringLiteral("系统信息"),
                                               m.value("group").toString() + QStringLiteral(" · ") +
                                                   m.value("key").toString() + QStringLiteral("：") +
                                                   m.value("value").toString());
                        }
                    } else if (step == QLatin1String("selfcheck")) {
                        maintOps.appendLog(QStringLiteral("故障自检"), QStringLiteral("===== 自检开始 ====="));
                        int live = 0;
                        for (const QVariant& v : videoRuntime.channels()) {
                            const QVariantMap info = qvariant_cast<VideoChannel*>(v)->info();
                            if (info.value("presentable").toBool()) ++live;
                        }
                        maintOps.appendLog(QStringLiteral("故障自检"),
                                           QStringLiteral("视频：%1 路实时 / 共 %2 路")
                                               .arg(live)
                                               .arg(videoRuntime.channels().size()));
                        maintOps.appendLog(QStringLiteral("故障自检"),
                                           QStringLiteral("AI：%1（检测模型 %2）")
                                               .arg(aiRuntime.statusText(), aiRuntime.detectModel()));
                        // WiFi 驱动/链路（与页面「故障自检」同一实现）
                        for (const QString& line : maintOps.wifiCheckSummary().split(QLatin1Char('\n')))
                            if (!line.isEmpty()) maintOps.appendLog(QStringLiteral("故障自检"), line);
                        maintOps.appendLog(QStringLiteral("故障自检"), QStringLiteral("===== 自检结束 ====="));
                    }
                }
            });
        }
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &videoRuntime, &VideoRuntime::stop);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &aiRuntime, &AiRuntime::stop);
    return app.exec();
}
