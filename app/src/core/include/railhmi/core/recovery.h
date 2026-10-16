#pragma once

// railhmi-core: 启动自恢复序列（ITEM-024 / DESKTOP §15.4、交互 §13 恢复）。
// 顺序：加载配置 -> 打开数据库(WAL) -> 启动录像/存储 -> 启动各相机取流 -> 启动 AI ->
//       云台归位 -> 调前方预置位 -> 前方 AI READY -> 发布 recovery.completed。
// 任一步失败：记录 + 显式告警，不阻塞其余步骤；单路非关键相机异常不导致整体退出（Degraded）。

#include <cstddef>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

enum class RecoveryStep {
    LoadConfig,
    OpenDatabase,
    StartRecording,
    StartStreaming,
    StartAi,
    PtzHome,
    FrontPreset,
    FrontAiReady,
    Count,
};
const char* to_string(RecoveryStep s);
const char* recovery_step_label(RecoveryStep s);

enum class StepStatus { Pending, Running, Ok, Degraded, Failed };
const char* to_string(StepStatus s);
const char* step_status_label(StepStatus s);

struct StepState {
    RecoveryStep step = RecoveryStep::LoadConfig;
    StepStatus status = StepStatus::Pending;
    std::string detail;
};

struct RecoveryState {
    std::vector<StepState> steps;
    bool started = false;
    bool completed = false;
    bool alive = true;              // 关键步未失败 -> 应用存活
    int degraded_cameras = 0;
    long long started_ms = 0;
    long long finished_ms = 0;
    int progress_pct() const;       // 0..100（按步覆盖率）
    std::string text() const;       // “恢复中 3/8 …” / “恢复完成”
};

// 该步是否为关键步（失败则应用不可用）。
bool recovery_step_critical(RecoveryStep s);

class RecoveryTracker {
public:
    RecoveryTracker();

    void start(long long now_ms);
    // 完成某一步；非关键步失败记为 Degraded（不阻塞其余步骤），关键步失败记为 Failed。
    void complete_step(RecoveryStep s, bool ok, const std::string& detail = "");
    // 记录单路相机启动结果：非关键异常 -> StartStreaming 降级（不导致整体退出）。
    void camera_result(bool ok, bool critical = false, const std::string& detail = "");
    void finish(long long now_ms);

    RecoveryState state() const { return st_; }
    bool application_alive() const { return st_.alive; }

private:
    StepState* find(RecoveryStep s);
    void recompute();

    RecoveryState st_;
};

}  // namespace core
}  // namespace railhmi
