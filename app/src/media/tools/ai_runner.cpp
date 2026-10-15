// railhmi 推理工作线程运行器（ITEM-017）
// 启动真实推理工作线程（有界丢旧帧队列 + 帧间隔降频 + SimDetector 替代输入），
// 运行若干秒后打印每路遥测，再 stop() 线程验证“推理线程停止 -> AI 不可用”。
// target 专用；无相机/模型时用替代检测器（如实标记，非 RKNN）。
//
// 用法: railhmi-ai-runner [--config DIR] [--duration S] [--out log]
// 退出码: 0 验收 PASS；1 失败/配置非法；2 参数错误。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/inference.h"
#include "railhmi/media/ai_worker.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config = RAILHMI_CONFIG_DIR;
    int duration_s = 4;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-ai-runner [--config DIR] [--duration S] [--out log]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    core::ConfigLoadResult cfg = core::load_config_dir(config);
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }

    std::map<core::AiProfile, std::string> models;
    for (const auto& m : cfg.settings.app.ai.models) models[m.profile] = m.version;

    media::AiWorkerConfig wc;
    wc.sched.inference_interval = cfg.settings.app.ai.inference_interval;
    wc.sched.queue_depth = cfg.settings.app.ai.queue_depth;
    wc.sched.npu_budget = cfg.settings.app.ai.npu_budget;
    wc.sched.temp_limit_c = cfg.settings.app.ai.temp_limit_c;
    wc.cameras = core::build_registry(cfg.settings);

    auto det = std::make_shared<media::SimDetector>(models);
    media::AiWorker worker(wc, det);
    emit("type=worker start interval=" + std::to_string(wc.sched.inference_interval) +
         " depth=" + std::to_string(wc.sched.queue_depth) +
         " cameras=" + std::to_string(wc.cameras.size()) +
         " detector=sim(substitute,not-rknn)");

    worker.start();
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() <
           duration_s) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    const core::AiRuntime online = worker.snapshot();
    int eligible = 0, with_result = 0;
    for (const auto& c : online.channels) {
        if (!core::ai_eligible(c.profile)) continue;
        ++eligible;
        if (c.last_result_ms >= 0) ++with_result;
        emit("type=channel camera=" + c.camera_id + " profile=" + core::to_string(c.profile) +
             " fps=" + std::to_string(static_cast<int>(c.fps + 0.5)) +
             " latency=" + std::to_string(c.latency_ms) +
             " dropped=" + std::to_string(c.dropped) +
             " model=" + c.model_version);
    }
    emit("type=ai-status running=" + core::ai_status_text(online, online.channels.empty() ? 0 : 0) +
         " eligible=" + std::to_string(eligible) + " with_result=" + std::to_string(with_result));

    worker.stop();
    const core::AiRuntime stopped = worker.snapshot();
    const std::string stopped_text = core::ai_status_text(stopped, 0);
    emit("type=ai-status stopped=" + stopped_text);
    const bool ok = (stopped_text == "AI 不可用") && with_result == eligible && eligible > 0 &&
                    !stopped.worker_alive;
    emit(std::string("type=acceptance worker_stopped=") + (stopped.worker_alive ? "false" : "true") +
         " ai_unavailable=" + (stopped_text == "AI 不可用" ? "true" : "false") +
         " channels_ran=" + std::to_string(with_result) + "/" + std::to_string(eligible) +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
