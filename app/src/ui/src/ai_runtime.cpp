#include "ai_runtime.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_worker_schedule.h"
#include "railhmi/platform/log.h"
#include "video_runtime.h"

#ifdef RAILHMI_HAS_MEDIA
#include "railhmi/media/vision_ai_worker.h"
#include "railhmi/media/vision_rknn_detector.h"
#endif

namespace {

using railhmi::core::AiProfile;

bool is_seg_name(const std::string& name) {
    return name.find("seg") != std::string::npos || name.find("Seg") != std::string::npos;
}

std::string version_of(const railhmi::core::AiConfig& ai, const std::string& name) {
    for (const auto& m : ai.models)
        if (m.name == name && !m.version.empty()) return m.version;
    for (const auto& c : ai.catalog)
        if (c.name == name && !c.version.empty()) return c.version;
    return "unlisted";
}

std::string profile_token(AiProfile p) {
    return std::string(railhmi::core::to_string(p));
}

}  // namespace

struct AiRuntime::Impl {
    railhmi::core::AppSettings settings;
    QString config_dir;
    VideoRuntime* video = nullptr;
    QTimer timer;
    railhmi::core::AiAlarmTracker tracker;

    std::vector<std::string> camera_ids;
    std::string det_model;
    std::string seg_model;

#ifdef RAILHMI_HAS_MEDIA
    std::shared_ptr<railhmi::media::RknnVisionDetector> detector;
    std::shared_ptr<railhmi::media::VisionAiWorker> worker;
    std::map<std::string, unsigned long long> last_seq;
#endif

    QString models_dir;
    bool reuse = true;
};

AiRuntime::AiRuntime(const railhmi::core::AppSettings& settings, const QString& configDir,
                     VideoRuntime* video, QObject* parent)
    : QObject(parent), impl_(new Impl) {
    impl_->settings = settings;
    impl_->config_dir = configDir;
    impl_->video = video;

    const auto& ai = settings.app.ai;
    impl_->reuse = ai.reuse_detect_for_duties;

    // 检测模型：默认取 front 的第一条非 seg 声明；分割模型取第一条 seg 声明。
    // 现场临时让三路共用检测模型；将来按职责换专用模型只需改配置。
    for (const auto& m : ai.models) {
        if (m.profile != AiProfile::Front && impl_->det_model.empty() && !is_seg_name(m.name))
            impl_->det_model = m.name;
        if (m.profile == AiProfile::Front) {
            if (is_seg_name(m.name)) {
                if (impl_->seg_model.empty()) impl_->seg_model = m.name;
            } else if (impl_->det_model.empty()) {
                impl_->det_model = m.name;
            }
        }
    }
    if (impl_->det_model.empty() && !ai.catalog.empty()) impl_->det_model = ai.catalog.front().name;
    if (impl_->seg_model.empty())
        for (const auto& c : ai.catalog)
            if (impl_->seg_model.empty() && is_seg_name(c.name)) impl_->seg_model = c.name;

    // 只接入当前具备模型证据的三类职责：车前方 / 受电弓 / 轮轨。
    // cab_door 的门态由 BLE 门磁为主判断（PRD DOOR-002），本轮不把检测模型套到车门上，
    // 因此该路即使 ai_enabled=true 也不入推理队列（注册表与徽标保持不变）。
    for (const auto& cam : settings.cameras) {
        const bool modelled = cam.ai_profile == AiProfile::Front ||
                              cam.ai_profile == AiProfile::Pantograph ||
                              cam.ai_profile == AiProfile::WheelRail;
        if (cam.ai_enabled && modelled) impl_->camera_ids.push_back(cam.id);
    }

    // 模型目录：相对路径按 --config 目录解析（板端通常指向 install/.../model）。
    QString dir = QString::fromStdString(ai.models_dir);
    if (QFileInfo(dir).isRelative()) dir = QDir(configDir).absoluteFilePath(dir);
    impl_->models_dir = dir;

    railhmi::core::AiAlarmConfig acfg;
    acfg.confirm_frames = std::max(1, settings.thresholds.confirm_frames);
    acfg.confidence_min = settings.thresholds.confidence_min;
    acfg.clear_hold_ms = 1500;
    impl_->tracker = railhmi::core::AiAlarmTracker(acfg);

    impl_->timer.setInterval(250);
    connect(&impl_->timer, &QTimer::timeout, this, &AiRuntime::tick);
}

AiRuntime::~AiRuntime() { stop(); }

void AiRuntime::start() {
    railhmi::platform::Logger::instance().info(
        "ai: start models_dir=" + impl_->models_dir.toStdString() +
        " detect=" + impl_->det_model + " segment=" + impl_->seg_model +
        " reuse_detect_for_duties=" + std::string(impl_->reuse ? "true" : "false") +
        " lanes=" + std::to_string(impl_->camera_ids.size()));
    rebuild();
    impl_->timer.start();
    tick();
}

void AiRuntime::stop() {
    impl_->timer.stop();
#ifdef RAILHMI_HAS_MEDIA
    if (impl_->worker) {
        impl_->worker->stop();
        impl_->worker.reset();
    }
    impl_->detector.reset();
#endif
    impl_->tracker.clear();
    ready_ = false;
    status_text_ = QStringLiteral("未接入");
    QVariantMap cleared;
    for (const auto& id : impl_->camera_ids)
        cleared[QString::fromStdString(id)] = QVariantMap{{"active", false}};
    publish(QVariantList(), cleared);
    emit statusChanged();
}

void AiRuntime::rebuild() {
#ifdef RAILHMI_HAS_MEDIA
    using namespace railhmi;
    if (impl_->worker) {
        impl_->worker->stop();
        impl_->worker.reset();
    }
    if (impl_->camera_ids.empty()) {
        railhmi::platform::Logger::instance().info("ai: no ai_enabled camera; inference stays 未接入");
        ready_ = false;
        status_text_ = QStringLiteral("未接入");
        emit statusChanged();
        return;
    }

    const auto& th = impl_->settings.thresholds;
    const auto& ai = impl_->settings.app.ai;

    media::RknnDetectorConfig dcfg;
    dcfg.models_dir = impl_->models_dir.toStdString();
    dcfg.reuse_detect_for_duties = impl_->reuse;
    // 三路共用检测模型；front 额外需要分割模型做两阶段判定。
    if (!impl_->det_model.empty()) {
        const std::string ver = version_of(ai, impl_->det_model);
        for (AiProfile p : {AiProfile::Front, AiProfile::Pantograph, AiProfile::WheelRail}) {
            core::DeclaredModel d;
            d.profile = p;
            d.name = impl_->det_model;
            d.version = ver;
            dcfg.declared.push_back(d);
        }
    }
    if (!impl_->seg_model.empty()) {
        core::DeclaredModel d;
        d.profile = AiProfile::Front;
        d.name = impl_->seg_model;
        d.version = version_of(ai, impl_->seg_model);
        dcfg.declared.push_back(d);
    }
    dcfg.rois = impl_->settings.roi;
    dcfg.detect.score_threshold = static_cast<float>(th.confidence_min);
    dcfg.detect.score_threshold_source = "app/config/thresholds.yaml:confidence_min";
    dcfg.detect.nms_iou = 0.45f;
    dcfg.detect.num_classes = 80;
    dcfg.detect.class_whitelist = {0};   // person
    // 类别白名单来自配置（app.ai.detect_classes，缺省 {0}=person）。
    // 现场想同时看到车辆等目标时，把 2(car)/5(bus)/7(truck) 加进去即可；代码不写死。
    if (!ai.detect_classes.empty()) dcfg.detect.class_whitelist = ai.detect_classes;
    dcfg.seg.score_threshold = static_cast<float>(th.confidence_min);
    dcfg.seg.nms_iou = 0.45f;
    dcfg.seg.mask_threshold = 0.0f;
    dcfg.seg.crop_to_box = true;
    dcfg.two_stage_cache.enabled = false;

    impl_->detector = std::make_shared<media::RknnVisionDetector>(dcfg);
    std::string load_err;
    impl_->detector->load(&load_err);

    media::VisionWorkerConfig wc;
    wc.sched.inference_interval = ai.inference_interval;
    wc.sched.queue_depth = ai.queue_depth;
    wc.sched.npu_budget = ai.npu_budget;
    wc.sched.temp_limit_c = ai.temp_limit_c;
    for (const auto& cam : impl_->settings.cameras) {
        if (std::find(impl_->camera_ids.begin(), impl_->camera_ids.end(), cam.id) ==
            impl_->camera_ids.end())
            continue;
        media::AiCameraSpec spec;
        spec.camera_id = cam.id;
        spec.ai_enabled = true;
        spec.ai_profile = profile_token(cam.ai_profile);
        wc.cameras.push_back(spec);
    }
    wc.semantic.allowed_labels = {"person", "obstacle"};
    wc.semantic.known_labels = {"person", "obstacle"};
    wc.semantic.confidence_min = th.confidence_min;
    wc.semantic.require_track = true;
    wc.semantic.use_envelope_in_roi = true;
    wc.semantic.track_policy = core::TrackPolicy::EnvelopeOnly;
    wc.rules.confirm_frames = th.confirm_frames;
    wc.rules.confirm_window_ms = th.confirm_window_ms;
    wc.rules.confidence_min = th.confidence_min;
    wc.rules.suppress_window_ms = th.suppress_window_ms;
    wc.loop_sleep_ms = 5;
    wc.envelope_queue_depth = 2;
    wc.capture_latest_mask = false;

    impl_->worker = std::make_shared<media::VisionAiWorker>(wc, impl_->detector, nullptr, nullptr);
    impl_->worker->start();
    impl_->last_seq.clear();

    // 启动日志把「检测什么类别 / 阈值多少」写清楚：本应用白名单固定 person(0)，
    // 街上的车辆等非白名单类别一律丢弃（这不是模型能力问题，是产品配置）。
    std::string classes;
    for (std::size_t i = 0; i < dcfg.detect.class_whitelist.size(); ++i)
        classes += (i == 0 ? "" : ",") + std::to_string(dcfg.detect.class_whitelist[i]);

    railhmi::platform::Logger::instance().info(
        "ai: worker started lanes=" + std::to_string(impl_->camera_ids.size()) +
        " load_error=" + (load_err.empty() ? std::string("none") : load_err) +
        " detect_ready=" + std::string(impl_->detector->ready(AiProfile::Front) ? "true" : "false") +
        " pantograph_ready=" +
        std::string(impl_->detector->ready(AiProfile::Pantograph) ? "true" : "false") +
        " wheel_rail_ready=" +
        std::string(impl_->detector->ready(AiProfile::WheelRail) ? "true" : "false") +
        " detect_classes=" + classes +
        " conf_threshold=" + std::to_string(th.confidence_min));
    emit modelSelectionChanged();
#endif
}

void AiRuntime::tick() {
    QVariantList channels;
    QVariantMap alarms;
    bool worker_alive = false;
    int ready_lanes = 0;

#ifdef RAILHMI_HAS_MEDIA
    if (impl_->worker && impl_->video) {
        // 诊断开关（RAILHMI_AI_DEBUG=1，默认关闭）：每 8 个 tick（约 2s）逐路打印框数与可用性，
        // 用来区分「模型没检出」与「链路没跑」。生产默认不输出。
        static long long tick_seq = 0;
        ++tick_seq;
        const bool dbg_log =
            qEnvironmentVariableIsSet("RAILHMI_AI_DEBUG") && (tick_seq % 8 == 0);
        // 只提交真实新帧（seq 变化），同一帧不重复入队；租约失效时跳过。
        for (const auto& id : impl_->camera_ids) {
            railhmi::media::FrameLease lease = impl_->video->latestLease(id);
            if (!lease.valid()) continue;
            const unsigned long long seq = lease.descriptor().seq;
            auto it = impl_->last_seq.find(id);
            if (it != impl_->last_seq.end() && it->second == seq) continue;
            impl_->last_seq[id] = seq;
            impl_->worker->submit(lease);
        }

        const railhmi::media::VisionWorkerSnapshot snap = impl_->worker->snapshot();
        worker_alive = snap.worker_alive && snap.running;
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();

        std::map<std::string, railhmi::core::AiAlarmState> states;
        for (const auto& lane : snap.lanes) {
            const railhmi::core::CameraConfig* slot = nullptr;
            for (const auto& cam : impl_->settings.cameras)
                if (cam.id == lane.camera_id) slot = &cam;

            QVariantMap m;
            m["cameraId"] = QString::fromStdString(lane.camera_id);
            m["name"] = slot ? QString::fromUtf8(slot->name.c_str())
                             : QString::fromStdString(lane.camera_id);
            m["profile"] = QString::fromStdString(profile_token(lane.profile));
            m["profileLabel"] =
                QString::fromUtf8(railhmi::core::ai_profile_label(lane.profile).c_str());
            bool reused = impl_->reuse && (lane.profile == AiProfile::Pantograph ||
                                           lane.profile == AiProfile::WheelRail);
            QString model = QString::fromStdString(lane.model_version);
            if (model.isEmpty() && slot) model = QString::fromStdString(impl_->det_model);
            m["modelVersion"] = model;
            m["modelReused"] = reused;
            m["modelText"] = model.isEmpty()
                                 ? QStringLiteral("—")
                                 : model + (reused ? QStringLiteral("（临时复用检测模型）")
                                                   : QString());
            const bool lane_ready = lane.ready && lane.unavailable_reason.empty();
            if (lane_ready) ++ready_lanes;
            m["available"] = lane_ready && worker_alive;
            m["unavailableReason"] =
                !worker_alive ? QStringLiteral("AI 未接入")
                              : (lane_ready ? QStringLiteral("")
                                            : QString::fromStdString(lane.unavailable_reason));
            m["fpsText"] = QString::number(lane.fps, 'f', 1);
            m["latencyMs"] = lane.latency_ms;
            m["inferMs"] = lane.last_infer_us / 1000;
            m["fps"] = lane.fps;
            m["lastResultText"] = lane.published > 0 ? QStringLiteral("刚刚") : QStringLiteral("—");
            m["boxCount"] = static_cast<int>(lane.boxes.size());
            // 验证用注入：RAILHMI_AI_SIM_ALARM 指定的路既参与报警判定，也在 P1/P5 画出框，
            // 便于在没有真实目标时验证「框 -> 报警 -> 红框」整条链路（日志不称其为真实检测）。
            static const QString sim_ids = qEnvironmentVariable("RAILHMI_AI_SIM_ALARM");
            const bool sim_active =
                !sim_ids.isEmpty() &&
                sim_ids.split(QLatin1Char(',')).contains(QString::fromStdString(lane.camera_id));
            double max_conf = -1.0;
            QVariantList boxes;
            for (const auto& b : lane.boxes) {
                QVariantMap bm;
                bm["x"] = b.x;
                bm["y"] = b.y;
                bm["w"] = b.w;
                bm["h"] = b.h;
                bm["confidence"] = b.confidence;
                bm["label"] = QString::fromStdString(b.label);
                bm["distanceM"] = b.distance_m;
                bm["inRoi"] = b.in_roi;
                boxes.append(bm);
                max_conf = std::max(max_conf, static_cast<double>(b.confidence));
            }
            if (sim_active) {
                QVariantMap bm;
                bm["x"] = 0.38;
                bm["y"] = 0.42;
                bm["w"] = 0.22;
                bm["h"] = 0.26;
                bm["confidence"] = 0.92;
                bm["label"] = QStringLiteral("person(模拟)");
                bm["distanceM"] = -1.0;
                bm["inRoi"] = true;
                boxes.append(bm);
                max_conf = std::max(max_conf, 0.92);
            }
            m["boxes"] = boxes;
            if (dbg_log) {
                const auto& dc = impl_->detector->counters();
                railhmi::platform::Logger::instance().info(
                    "ai-debug: " + lane.camera_id +
                    " profile=" + profile_token(lane.profile) +
                    " boxes=" + std::to_string(lane.boxes.size()) +
                    " published=" + std::to_string(lane.published) +
                    " infer_us=" + std::to_string(lane.last_infer_us) +
                    " det_cand=" + std::to_string(dc.last_det_candidates) +
                    " det_low=" + std::to_string(dc.last_det_dropped_score) +
                    " det_cls=" + std::to_string(dc.last_det_dropped_class) +
                    " det_nms=" + std::to_string(dc.last_det_nms) +
                    " det_ok_total=" + std::to_string(dc.det_boxes) +
                    " ready=" + std::string(lane_ready ? "true" : "false") +
                    " reason=" + lane.unavailable_reason);
            }
            m["confidenceText"] = max_conf >= 0.0
                                      ? QString::number(max_conf * 100, 'f', 0) + "%"
                                      : QStringLiteral("—");
            m["distanceText"] = QString::fromUtf8(lane.distance_text.c_str());
            QVariantList roi;
            for (const auto& r : impl_->settings.roi) {
                if (r.camera != lane.camera_id) continue;
                QVariantMap rm;
                rm["name"] = QString::fromUtf8(r.name.c_str());
                rm["x"] = r.x;
                rm["y"] = r.y;
                rm["w"] = r.w;
                rm["h"] = r.h;
                roi.append(rm);
            }
            m["roi"] = roi;

            // 报警判定（多帧确认 + 解除保持），只在跃迁时上报，避免刷屏。
            // 验证用注入：RAILHMI_AI_SIM_ALARM=CAM-01,CAM-04（显式环境变量，默认关闭；
            // 用于在没有真实目标的现场/离屏环境验证「红框 + 报警条 + 报警中心」闭环）。
            // 注入的框带 simulation 语义，日志与界面都不声称是真实检测。
            std::vector<railhmi::core::EnvelopeBox> alarm_boxes = lane.boxes;
            if (sim_active) {
                railhmi::core::EnvelopeBox b;
                b.x = 0.4f;
                b.y = 0.4f;
                b.w = 0.2f;
                b.h = 0.2f;
                b.confidence = 0.92f;
                b.class_id = 0;
                b.label = "person";
                b.in_roi = true;
                alarm_boxes.push_back(b);
            }
            railhmi::core::AiAlarmState st;
            const bool changed = impl_->tracker.update(lane.camera_id, lane.profile, alarm_boxes,
                                                       static_cast<long long>(now_ms), &st);
            states[lane.camera_id] = st;
            if (changed) {
                if (st.active) {
                    emit alarmRaised(QString::fromStdString(lane.camera_id),
                                     static_cast<int>(st.level),
                                     QString::fromLatin1(railhmi::core::ai_alarm_type_token(st.type)),
                                     QString::fromUtf8(st.description.c_str()));
                } else {
                    emit alarmCleared(QString::fromStdString(lane.camera_id));
                }
            }
            m["alarming"] = st.active;
            channels.append(m);
        }

        // 报警真值表：所有受检相机都有一条（未报警也给出 false），QML 直接查表。
        for (const auto& id : impl_->camera_ids) {
            const auto& st = states[id];
            QVariantMap a;
            a["active"] = st.active;
            a["level"] = static_cast<int>(st.level);
            a["text"] = QString::fromUtf8(st.description.c_str());
            alarms[QString::fromStdString(id)] = a;
        }
    }
#endif

    // 可用性只在 GUI 线程算：线程活着 + 至少一路模型就绪才算「接入」。
    const bool now_ready = worker_alive && ready_lanes > 0;
    const QString now_text = now_ready
                                 ? QStringLiteral("接入 %1 路").arg(ready_lanes)
                                 : QStringLiteral("未接入");
    if (now_ready != ready_ || now_text != status_text_) {
        ready_ = now_ready;
        status_text_ = now_text;
        emit statusChanged();
    }
    publish(channels, alarms);
}

void AiRuntime::publish(const QVariantList& channels, const QVariantMap& alarms) {
    if (channels != channels_) {
        channels_ = channels;
        emit channelsChanged();
    }
    if (alarms != alarms_) {
        alarms_ = alarms;
        emit alarmsChanged();
    }
}

QVariantList AiRuntime::channels() const { return channels_; }

QVariantMap AiRuntime::alarms() const { return alarms_; }

QVariantList AiRuntime::catalog() const {
    QVariantList out;
    std::vector<std::string> seen;
    auto push = [&](const std::string& name, const std::string& version) {
        if (name.empty()) return;
        for (const auto& s : seen)
            if (s == name) return;
        seen.push_back(name);
        QVariantMap m;
        m["name"] = QString::fromStdString(name);
        m["version"] = QString::fromStdString(version);
        m["kind"] = is_seg_name(name) ? QStringLiteral("segment") : QStringLiteral("detect");
        out.append(m);
    };
    const auto& ai = impl_->settings.app.ai;
    for (const auto& c : ai.catalog) push(c.name, c.version);
    for (const auto& m : ai.models) push(m.name, m.version);
    // 当前选中的模型即使来自配置以外也要出现在候选里（否则下拉框会显示空）。
    push(impl_->det_model, version_of(ai, impl_->det_model));
    push(impl_->seg_model, version_of(ai, impl_->seg_model));
    return out;
}

bool AiRuntime::isAlarming(const QString& cameraId) const {
    return alarms_.value(cameraId).toMap().value("active").toBool();
}

QString AiRuntime::detectModel() const { return QString::fromStdString(impl_->det_model); }

QString AiRuntime::segmentModel() const { return QString::fromStdString(impl_->seg_model); }

QString AiRuntime::modelsDir() const { return impl_->models_dir; }

QString AiRuntime::modelFor(const QString& cameraId) const {
    for (const auto& value : channels_) {
        const QVariantMap m = value.toMap();
        if (m.value("cameraId").toString() == cameraId) return m.value("modelText").toString();
    }
    return QStringLiteral("—");
}

bool AiRuntime::selectModel(const QString& kind, const QString& modelName) {
    const std::string name = modelName.toStdString();
    if (name.empty()) {
        emit modelChanged(kind, modelName, false, QStringLiteral("模型名为空"));
        return false;
    }
    const bool segment = (kind == QLatin1String("segment"));
    if (segment) impl_->seg_model = name;
    else impl_->det_model = name;
    railhmi::platform::Logger::instance().info(
        "ai: model switch kind=" + kind.toStdString() + " model=" + name);
    rebuild();
    tick();
    emit modelSelectionChanged();
    emit modelChanged(kind, modelName, ready_, status_text_);
    return ready_;
}
