// Verifier-owned ext fixture v3 for ITEM-024 re-verification round (software_verification).
// Subject: implementer uifix-r1 (commit fd9dde4) — UI review D-01..D-04 repair.
// Scope of THIS fixture: the demo recovery path added for D-02/D-04 — the bridge
// RailhmiHealthModel::advanceRecovery()/resetRecovery() algorithm is transcribed
// verbatim below (DemoDriver) and driven against the delivered RecoveryTracker;
// every tick's observable state (progress/text/alive/degraded/status tokens) is
// asserted — this is the model-level oracle behind "界面显示恢复步骤与完成状态"
// and the overlay visibility gate (visible: st.completed === false).
// Regression base: verify_health24_ext.cpp (v2, 103 checks) is re-run unchanged.
// Pure logic, no Qt/RKNN — like the delivered probes.
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/recovery.h"

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(bool ok, const std::string& name) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

void check_eq(const std::string& name, const std::string& got, const std::string& want) {
    check(got == want, name + " [" + got + "] want [" + want + "]");
}

// Transcription of RailhmiHealthModel::resetRecovery()/advanceRecovery()
// (app/src/ui/src/health_model.cpp @ fd9dde4) — demo state machine the QML
// overlay Timer and P6 "演示恢复" chip drive. Keep field-for-field faithful:
// one step transition per advance(), all-terminal -> finish(), changed() emit
// is UI-only and not modeled here.
class DemoDriver {
public:
    void reset() {
        t_.start(clock_);
        active_ = true;
    }
    void advance() {
        if (!active_) { t_.start(clock_); active_ = true; }
        clock_ += 500;
        for (int i = 0; i < static_cast<int>(RecoveryStep::Count); ++i) {
            const RecoveryStep s = static_cast<RecoveryStep>(i);
            const RecoveryState cur = t_.state();
            if (cur.steps[i].status != StepStatus::Pending &&
                cur.steps[i].status != StepStatus::Running)
                continue;
            if (s == RecoveryStep::StartStreaming)
                t_.camera_result(false, false, "CAM-02 启动失败（降级）");
            else
                t_.complete_step(s, true);
            break;
        }
        const RecoveryState now = t_.state();
        bool all_terminal = true;
        for (const auto& s : now.steps)
            if (s.status == StepStatus::Pending || s.status == StepStatus::Running)
                all_terminal = false;
        if (all_terminal && !now.completed) t_.finish(clock_);
    }
    RecoveryState state() const { return t_.state(); }

private:
    RecoveryTracker t_;
    bool active_ = false;
    long long clock_ = 1000;
};

// --- A. demo path: reset -> 9 advance ticks -> completion -> idempotence ------
void t_demo_path() {
    DemoDriver d;

    // fresh (pre-reset) tracker: not started, 8 pending steps
    check_eq("demo fresh text", d.state().text(), "未开始");
    check(d.state().progress_pct() == 0, "demo fresh progress 0");
    {
        bool all_pending = d.state().steps.size() == 8;
        for (const auto& s : d.state().steps)
            if (s.status != StepStatus::Pending) all_pending = false;
        check(all_pending, "demo fresh 8 steps all pending");
    }

    // resetRecovery() (P6 演示恢复 chip): started, none terminal
    d.reset();
    check(d.state().started && !d.state().completed, "reset -> started, not completed");
    check_eq("reset text", d.state().text(), "恢复中 0/8");
    check(d.state().progress_pct() == 0, "reset progress 0");
    check(d.state().alive, "reset alive=true");
    check(d.state().degraded_cameras == 0, "reset degraded_cameras=0");

    // tick 1..3: config / db / recording complete ok
    d.advance();
    check(d.state().steps[0].status == StepStatus::Ok, "tick1 load_config Ok");
    check(d.state().progress_pct() == 12, "tick1 progress 12");
    check_eq("tick1 text", d.state().text(), "恢复中 1/8");
    d.advance();
    check(d.state().progress_pct() == 25, "tick2 progress 25");
    check_eq("tick2 text", d.state().text(), "恢复中 2/8");
    d.advance();
    check(d.state().progress_pct() == 37, "tick3 progress 37");
    check_eq("tick3 text", d.state().text(), "恢复中 3/8");

    // tick 4: StartStreaming via camera_result(false,false) -> Degraded, app alive
    d.advance();
    check_eq("tick4 streaming status token",
             to_string(d.state().steps[3].status), "degraded");
    check_eq("tick4 streaming detail",
             d.state().steps[3].detail, "CAM-02 启动失败（降级）");
    check(d.state().degraded_cameras == 1, "tick4 degraded_cameras=1");
    check(d.state().progress_pct() == 50, "tick4 progress 50 (degraded counts)");
    check_eq("tick4 text", d.state().text(), "恢复中 4/8");
    check(d.state().alive, "tick4 alive=true (non-critical camera never fatal)");

    // tick 5..7: ai / ptz home / front preset
    d.advance();
    check(d.state().progress_pct() == 62, "tick5 progress 62");
    check_eq("tick5 text", d.state().text(), "恢复中 5/8");
    d.advance();
    check(d.state().progress_pct() == 75, "tick6 progress 75");
    d.advance();
    check(d.state().progress_pct() == 87, "tick7 progress 87");
    check_eq("tick7 text", d.state().text(), "恢复中 7/8");

    // tick 8: front_ai_ready -> all terminal -> same tick finish() (bridge does
    // the completion inside advanceRecovery, so 9 ticks total reach 100%)
    d.advance();
    check(d.state().steps[7].status == StepStatus::Ok, "tick8 front_ai_ready Ok");
    check(d.state().completed, "tick8 completed=true (bridge finishes in-tick)");
    check(d.state().progress_pct() == 100, "tick8 progress 100");
    check_eq("tick8 text", d.state().text(), "恢复完成（部分降级）");
    check(d.state().degraded_cameras == 1, "tick8 degraded count preserved");
    check(d.state().alive, "tick8 alive=true");

    // overlay Timer keeps firing until visible flips false; extra advance must
    // be a state no-op (no double-finish, no re-degrade)
    d.advance();
    check(d.state().completed, "post-completion advance keeps completed");
    check_eq("post-completion text stable", d.state().text(), "恢复完成（部分降级）");
    check(d.state().degraded_cameras == 1, "post-completion degraded stable");
    check(d.state().progress_pct() == 100, "post-completion progress stable");

    // re-run the demo from completion (chip pressed again): full reset
    d.reset();
    check(!d.state().completed, "re-reset -> not completed");
    check(d.state().progress_pct() == 0, "re-reset progress 0");
    check(d.state().degraded_cameras == 0, "re-reset degraded cleared");
    check_eq("re-reset text", d.state().text(), "恢复中 0/8");
    d.advance();
    check(d.state().progress_pct() == 12, "re-demo tick1 progress 12 (repeatable)");

    // mid-demo reset (chip pressed while walking): state restarts cleanly
    d.advance();
    d.advance();  // 2/8
    d.reset();
    check_eq("mid-demo reset text", d.state().text(), "恢复中 0/8");
    check(d.state().progress_pct() == 0 && !d.state().completed,
          "mid-demo reset restarts");
    d.advance();
    check(d.state().progress_pct() == 12, "advance after mid-reset walks from step 1");
}

// --- B. critical-failure model state behind the overlay notice ----------------
// RecoveryOverlay shows "关键步骤失败" gated on st.alive === false; the bridge
// must be able to surface that state (seed/probe path, not the ok-only demo).
void t_critical_notice_state() {
    RecoveryTracker t;
    t.start(0);
    t.complete_step(RecoveryStep::LoadConfig, false, "配置目录缺失");
    check_eq("critical status token", to_string(t.state().steps[0].status), "failed");
    check(!t.state().alive, "critical failure -> alive=false (overlay notice)");
    check_eq("critical mid text", t.state().text(), "恢复中 1/8");
    check(t.state().progress_pct() == 12, "critical progress still counts");
    check_eq("critical status label", step_status_label(StepStatus::Failed), "失败");
}

// --- C. status tokens/labels the QML color mapping depends on -----------------
void t_status_tokens() {
    check_eq("status token pending", to_string(StepStatus::Pending), "pending");
    check_eq("status token running", to_string(StepStatus::Running), "running");
    check_eq("status token ok", to_string(StepStatus::Ok), "ok");
    check_eq("status token degraded", to_string(StepStatus::Degraded), "degraded");
    check_eq("status token failed", to_string(StepStatus::Failed), "failed");
    check_eq("status label pending", step_status_label(StepStatus::Pending), "待执行");
    check_eq("status label running", step_status_label(StepStatus::Running), "进行中");
    check_eq("status label ok", step_status_label(StepStatus::Ok), "完成");
    check_eq("status label degraded", step_status_label(StepStatus::Degraded), "降级");
    // step labels shown in overlay/P6 rows for the two recovery-relevant steps
    check_eq("step label streaming", recovery_step_label(RecoveryStep::StartStreaming), "启动相机取流");
    check_eq("step label front_ai_ready", recovery_step_label(RecoveryStep::FrontAiReady), "前方 AI READY");
}

}  // namespace

int main() {
    std::printf("== verify ITEM-024 ext v3 (demo recovery path / uifix-r1) ==\n");
    t_demo_path();
    t_critical_notice_state();
    t_status_tokens();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
