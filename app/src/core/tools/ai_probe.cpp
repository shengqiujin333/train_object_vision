// railhmi 推理调度/队列/遥测探针（ITEM-017）
// 用配置的推理调度（帧间隔/队列深度/预算/温度）驱动模拟帧，演示：
// 有界丢旧帧、降频推理、优先级调度、NPU 预算、温度守卫（降载不停）、两侧广角不入队，
// 以及“推理线程停止 -> AI 不可用 + 相关报警标注”的转换。无 Qt/RKNN，纯 core。
//
// 用法: railhmi-ai-probe [--config DIR] [--frames N] [--out log]
// 退出码: 0 验收 PASS；1 验收 FAIL 或配置非法；2 参数错误。

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/inference.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string config = RAILHMI_CONFIG_DIR;
    int frames = 50;
    long long frame_dt_ms = 40;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config);
        else if (a == "--frames") { std::string v; next(&v); frames = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-ai-probe [--config DIR] [--frames N] [--out log]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    ConfigLoadResult cfg = load_config_dir(config);
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }

    const AiConfig& ai = cfg.settings.app.ai;
    std::map<AiProfile, std::string> model_version;
    for (const auto& m : ai.models) model_version[m.profile] = m.version;

    AiSchedulerConfig sc;
    sc.inference_interval = ai.inference_interval;
    sc.queue_depth = ai.queue_depth;
    sc.npu_budget = ai.npu_budget;
    sc.temp_limit_c = ai.temp_limit_c;

    emit("type=config interval=" + std::to_string(sc.inference_interval) +
         " depth=" + std::to_string(sc.queue_depth) +
         " budget=" + std::to_string(sc.npu_budget) +
         " temp_limit=" + std::to_string(static_cast<int>(sc.temp_limit_c)) +
         " models=" + std::to_string(ai.models.size()));

    const std::vector<CameraSlot> registry = build_registry(cfg.settings);

    AiRuntime rt;
    rt.worker_alive = true;
    rt.channels.clear();
    for (const auto& s : registry) {
        AiChannelRuntime c;
        c.camera_id = s.id;
        c.profile = s.ai_profile;
        auto it = model_version.find(s.ai_profile);
        c.model_version = (it != model_version.end()) ? it->second : "";
        rt.channels.push_back(c);
    }

    // 每路一个丢旧帧队列；只有 ai_eligible 的相机推进。
    std::vector<BoundedFrameQueue> queues;
    std::vector<long long> inferred;
    for (const auto& c : rt.channels) {
        queues.emplace_back(sc.queue_depth);
        inferred.push_back(0);
    }

    long long now = 0;
    for (int fr = 0; fr < frames; ++fr) {
        now += frame_dt_ms;
        for (std::size_t i = 0; i < rt.channels.size(); ++i) {
            AiChannelRuntime& c = rt.channels[i];
            if (!ai_eligible(c.profile)) continue;
            c.last_frame_ms = now;
            queues[i].push(FrameRef{c.camera_id, static_cast<uint64_t>(fr), now});
            if (frame_due(static_cast<uint64_t>(fr), sc.inference_interval)) {
                FrameRef got;
                if (queues[i].pop(&got)) {
                    c.last_result_ms = now;
                    c.latency_ms = 30 + ai_priority(c.profile) * 3;
                    c.dropped = queues[i].dropped();
                    c.queue_depth = static_cast<int>(queues[i].size());
                    ++inferred[i];
                }
            }
        }
    }
    const double dur_s = (frames * frame_dt_ms) / 1000.0;
    for (std::size_t i = 0; i < rt.channels.size(); ++i) {
        AiChannelRuntime& c = rt.channels[i];
        if (!ai_eligible(c.profile)) continue;
        c.fps = dur_s > 0 ? inferred[i] / dur_s : 0.0;
        emit("type=queue camera=" + c.camera_id + " profile=" + to_string(c.profile) +
             " depth=" + std::to_string(sc.queue_depth) +
             " dropped=" + std::to_string(c.dropped) +
             " inferred=" + std::to_string(inferred[i]) +
             " requested=" + std::to_string((frames + sc.inference_interval - 1) / sc.inference_interval) +
             " fps=" + std::to_string(static_cast<int>(c.fps + 0.5)) +
             " model=" + c.model_version);
    }

    // 调度：常温 / 超温。
    SchedInput s60;
    s60.channels = rt.channels;
    s60.temp_c = 60;
    s60.worker_alive = true;
    SchedDecision d60 = plan_inference(s60, sc);
    std::string run60, skip60;
    for (const auto& id : d60.run) run60 += (run60.empty() ? "" : ",") + id;
    for (const auto& id : d60.skipped) skip60 += (skip60.empty() ? "" : ",") + id;
    emit("type=schedule temp=60 run=" + run60 + " skipped=" + skip60 +
         " guard=" + (d60.temperature_guard ? "true" : "false"));

    SchedInput s90 = s60;
    s90.temp_c = 90;
    SchedDecision d90 = plan_inference(s90, sc);
    std::string run90, skip90;
    for (const auto& id : d90.run) run90 += (run90.empty() ? "" : ",") + id;
    for (const auto& id : d90.skipped) skip90 += (skip90.empty() ? "" : ",") + id;
    emit("type=schedule temp=90 run=" + run90 + " skipped=" + skip90 +
         " guard=" + (d90.temperature_guard ? "true" : "false"));

    // 在线时的 AI 调试视图与状态。
    std::vector<DetectionResult> latest;
    for (const auto& c : rt.channels) {
        if (!ai_eligible(c.profile)) continue;
        DetectionResult r;
        r.camera_id = c.camera_id;
        r.profile = c.profile;
        r.model_version = c.model_version;
        r.latency_ms = c.latency_ms;
        r.ts_ms = c.last_result_ms;
        r.max_confidence = 0.8f;
        DetectionBox b;
        b.x = 0.4f; b.y = 0.5f; b.w = 0.1f; b.h = 0.2f; b.confidence = 0.8f; b.label = "target"; b.in_roi = true;
        r.boxes.push_back(b);
        latest.push_back(r);
    }
    emit("type=ai-status online=" + std::string(ai_status_text(rt, now)));
    for (const auto& d : build_ai_debug(registry, rt, latest, now)) {
        emit("type=debug camera=" + d.camera_id + " profile=" + to_string(d.profile) +
             " available=" + (d.available ? "true" : "false") +
             " model=" + d.model_version +
             " latency=" + std::to_string(d.latency_ms) +
             " boxes=" + std::to_string(d.boxes.size()) +
             " conf=" + std::to_string(static_cast<int>(d.max_confidence * 100 + 0.5)) +
             " last=" + d.last_result_text);
    }

    // 推理线程停止：AI 不可用，相关报警标注，调试页每路不可用（不静默）。
    rt.worker_alive = false;
    const bool annot_ok = (ai_alarm_annotation(false) == "AI 不可用");
    const std::string stopped = ai_status_text(rt, now);
    bool all_unavail = true;
    for (const auto& d : build_ai_debug(registry, rt, latest, now))
        if (d.available || d.unavailable_reason != "AI 不可用") all_unavail = false;
    emit("type=ai-status stopped=" + stopped);
    emit("type=acceptance worker_down_status=" + stopped +
         " alarm_annotation=" + ai_alarm_annotation(false) +
         " debug_all_unavailable=" + (all_unavail ? "true" : "false") +
         " temp_guard_degrades=" + (d90.temperature_guard && !d90.run.empty() ? "true" : "false") +
         " wide_angle_excluded=" +
         (d60.run.end() == std::find(d60.run.begin(), d60.run.end(), "CAM-02") ? "true" : "false"));

    const bool ok = (stopped == "AI 不可用") && annot_ok && all_unavail &&
                    d90.temperature_guard && !d90.run.empty() && d60.run.size() == 4;
    emit(std::string("type=result ") + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
