// vext_recovery.cpp — ITEM-014 验证侧自有夹具（software_verification VERIFY-20261017-v2）
//
// 黑盒驱动交付 MultiStreamManager + RecoveryController（链接交付 librailhmi_media.a），
// 覆盖交付探针未测的边界：
//   g01 正向基线: cut -> 退避 -> 真实重建 -> 真帧确认（attempts=1, gen 前进）
//   g02 恢复后同文本二次断流: 重复 cut（同文本 last_error）必须再次退避重建（D-01 实证）
//   g03 异文本一次错误: freeze 注入被分类并重建（对照）
//   g03b 同文本二次 freeze 对照: 与 g02 同形态的去重复现
//   g04 awaiting 期间交替异文本错误: 分类继续、排程存在，但排程的重建须被执行
//       （设计 §6.2「offline 后仍由控制器有限速率重试」；D-02 实证）
//   g05 鉴权失败不自动重试
//   g06 能力门控 + 变化后恰一次重建并真帧确认
//   g07 ONVIF 三态
//   g08 单路隔离
//
// 预期缺陷判例（若交付无缺陷则全绿）: g02 两条 + g04 一条；
// 其余 24 条为回归判据，任何 FAIL 都提示夹具或交付异常。
//
// 判据依据: 接口契约 D-02、设计 §6.2/§6.4、ITEM-014 任务书。

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/recovery_policy.h"
#include "railhmi/media/multi_stream.h"
#include "railhmi/media/recovery_controller.h"

namespace {

using railhmi::media::ChannelSpec;
using railhmi::media::MultiStreamManager;
using railhmi::media::RecoveryController;
using railhmi::media::RecoveryControllerConfig;
using railhmi::media::RecoveryControllerStatus;
using railhmi::media::SourceKind;
using railhmi::core::RecoveryAction;
using railhmi::core::RecoveryConfig;

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

void observe(const char* scene, const RecoveryControllerStatus& s, long long gen_before,
             long long gen_after) {
    std::printf("vext OBSERVE %s gen=%lld->%lld %s\n", scene, gen_before, gen_after,
                s.describe().c_str());
}

bool wait_for(const std::function<bool()>& cond, int timeout_ms, int step_ms = 50) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < timeout_ms) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
    return cond();
}

struct Scene {
    MultiStreamManager mgr;
    RecoveryController ctrl;
    explicit Scene(const RecoveryConfig& rc)
        : mgr(make_specs()), ctrl(mgr.worker(0), make_cfg(rc)) {}
    static std::vector<ChannelSpec> make_specs() {
        std::vector<ChannelSpec> v;
        for (const char* id : {"CAM-01"}) {
            ChannelSpec sp;
            sp.camera_id = id;
            sp.cfg.camera_id = id;
            sp.cfg.source = SourceKind::Test;
            v.push_back(sp);
        }
        return v;
    }
    static RecoveryControllerConfig make_cfg(const RecoveryConfig& rc) {
        RecoveryControllerConfig c;
        c.policy = rc;
        return c;
    }
};

RecoveryConfig short_cfg() {
    RecoveryConfig rc;
    rc.base_backoff_ms = 200;
    rc.max_backoff_ms = 3200;
    rc.max_attempts = 4;
    return rc;
}

bool warmup(MultiStreamManager& mgr, int channels) {
    return wait_for([&]() {
        const auto st = mgr.stats();
        for (int i = 0; i < channels; ++i)
            if (st[static_cast<std::size_t>(i)].last_seq == 0) return false;
        return true;
    }, 4000);
}

long long gen_of(MultiStreamManager& mgr, std::size_t idx) {
    return static_cast<long long>(mgr.stats()[idx].generation);
}

void drive(RecoveryController& c, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < ms) {
        c.tick(railhmi::media::MultiStreamManager::now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

// ---- g01 正向基线 ----
void g01() {
    Scene s(short_cfg());
    std::string err;
    check(s.mgr.start_all(&err) == 1, "g01 start_all");
    if (!warmup(s.mgr, 1)) { check(false, "g01 warmup frames"); return; }
    const long long g0 = gen_of(s.mgr, 0);
    s.mgr.request_cut(0);
    drive(s.ctrl, 3500);
    const auto st = s.ctrl.status();
    const long long g1 = gen_of(s.mgr, 0);
    observe("g01", st, g0, g1);
    check(st.policy.total_attempts == 1, "g01 attempts once");
    check(st.recovered && st.recovered_generation == g1 && g1 == g0 + 1, "g01 recovered by new frame");
    check(s.mgr.stats()[0].last_seq > 0, "g01 frames after recovery");
    check(st.recovery_latency_ms >= 0, "g01 latency recorded");
}

// ---- g02 恢复后同文本二次断流（D-01 实证）----
void g02() {
    Scene s(short_cfg());
    std::string err;
    if (s.mgr.start_all(&err) != 1 || !warmup(s.mgr, 1)) {
        check(false, "g02 setup"); return;
    }
    const long long g0 = gen_of(s.mgr, 0);
    s.mgr.request_cut(0);                    // 第一次断流（文本: simulated stream cut (FI-01)）
    drive(s.ctrl, 3500);
    const auto after1 = s.ctrl.status();
    const long long g1 = gen_of(s.mgr, 0);
    if (!(after1.recovered && after1.policy.total_attempts == 1 && g1 == g0 + 1)) {
        check(false, "g02 first recovery prerequisite");
        observe("g02_first", after1, g0, g1);
        return;
    }
    // 恢复后：完全相同的断流（真实断流常态形态为逐字节相同的错误消息）再次发生
    s.mgr.request_cut(0);
    drive(s.ctrl, 4000);                     // >> 2×backoff 窗：正常应完成第二次重建+确认
    const auto after2 = s.ctrl.status();
    const long long g2 = gen_of(s.mgr, 0);
    observe("g02_second_cut", after2, g1, g2);
    check(after2.policy.total_attempts == 2, "g02 second cut attempts==2");
    check(g2 == g1 + 1 && after2.recovered_generation == g2, "g02 second cut rebuilt+confirmed");
    check(s.mgr.stats()[0].last_seq > 0, "g02 frames after second recovery");
}

// ---- g03 异文本一次错误: freeze 注入被分类并重建（对照）----
void g03() {
    Scene s(short_cfg());
    std::string err;
    if (s.mgr.start_all(&err) != 1 || !warmup(s.mgr, 1)) {
        check(false, "g03 setup"); return;
    }
    const long long g0 = gen_of(s.mgr, 0);
    s.mgr.request_freeze(0);                 // 异文本: simulated stream freeze (FI-02)
    drive(s.ctrl, 3500);
    const auto st = s.ctrl.status();
    const long long g1 = gen_of(s.mgr, 0);
    observe("g03_freeze_inject", st, g0, g1);
    check(st.policy.total_attempts == 1, "g03 distinct-text error classified and rebuilt");
    check(st.recovered && g1 == g0 + 1, "g03 recovered");
}

// ---- g03b 同文本二次 freeze 对照: 与 g02 同形态的去重复现 ----
void g03b() {
    Scene s(short_cfg());
    std::string err;
    if (s.mgr.start_all(&err) != 1 || !warmup(s.mgr, 1)) {
        check(false, "g03b setup"); return;
    }
    const long long g0 = gen_of(s.mgr, 0);
    s.mgr.request_freeze(0);                 // 第一次 freeze（首次出现该文本）
    drive(s.ctrl, 3000);
    if (!s.ctrl.status().recovered) { check(false, "g03b first recovery prerequisite"); return; }
    const long long g1 = gen_of(s.mgr, 0);
    s.mgr.request_freeze(0);                 // 同文本第二次
    drive(s.ctrl, 4000);
    const auto st = s.ctrl.status();
    const long long g2 = gen_of(s.mgr, 0);
    observe("g03b_second_freeze", st, g1, g2);
    // D-01 呈证: 正确行为 = 同文本第二次也必须重建（attempts==2）。缺陷下 attempts 保持 1。
    // 修复轮后本判例随 g02 两条一起转 PASS。
    check(st.policy.total_attempts == 2, "g03b D-01 exhibit same-text repeat not rebuilt");
}

// ---- g04 awaiting 期间交替异文本错误（D-02 实证）----
void g04() {
    Scene s(short_cfg());
    std::string err;
    if (s.mgr.start_all(&err) != 1 || !warmup(s.mgr, 1)) {
        check(false, "g04 setup"); return;
    }
    const long long g0 = gen_of(s.mgr, 0);
    s.mgr.request_cut(0);
    drive(s.ctrl, 1500);
    if (!s.ctrl.status().recovered) {
        check(false, "g04 first rebuild+confirm prerequisite");
        return;
    }
    const int attempts_before = s.ctrl.status().policy.total_attempts;
    // 进入持续故障: 交替 cut/freeze 注入（连串异文本错误，模拟真实持续断流的错误流），
    // 期间不注入 resume；第二次重建后（awaiting=true）恢复确认永不成立。
    const auto t0 = std::chrono::steady_clock::now();
    bool toggle = false;
    int injected = 0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < 6000) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count() >= injected * 600) {
            toggle = !toggle;
            if (toggle) s.mgr.request_cut(0); else s.mgr.request_freeze(0);
            ++injected;
        }
        s.ctrl.tick(railhmi::media::MultiStreamManager::now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    const auto st = s.ctrl.status();
    const long long gN = gen_of(s.mgr, 0);
    observe("g04_continuous_fault", st, g0, gN);
    // 若进入 awaiting 且确认永不成立，设计要求退避后继续有限重建（attempts 增长，
    // 或按 max_attempts 停止并显式 stop_reason）。若 attempts 冻结且未停止 = 排程失效。
    const bool stuck_awaiting_no_retry = st.awaiting_recovery &&
                                         st.policy.total_attempts == attempts_before &&
                                         !st.policy.stopped;
    check(!stuck_awaiting_no_retry, "g04 bounded retry continues during awaiting");
}

// ---- g05 鉴权失败不自动重试 ----
void g05() {
    Scene s(short_cfg());
    std::string err;
    if (s.mgr.start_all(&err) != 1 || !warmup(s.mgr, 1)) {
        check(false, "g05 setup"); return;
    }
    const long long g0 = gen_of(s.mgr, 0);
    s.ctrl.on_source_error("gst-resource-error-quark", "Unauthorized 401", 0);
    drive(s.ctrl, 2500);
    const auto st = s.ctrl.status();
    const long long g1 = gen_of(s.mgr, 0);
    observe("g05_auth", st, g0, g1);
    check(st.policy.stopped && st.policy.total_attempts == 0, "g05 auth no retry");
    check(g1 == g0, "g05 no rebuild on auth");
}

// ---- g06 能力门控 ----
void g06() {
    Scene s(short_cfg());
    std::string err;
    if (s.mgr.start_all(&err) != 1 || !warmup(s.mgr, 1)) {
        check(false, "g06 setup"); return;
    }
    const long long g0 = gen_of(s.mgr, 0);
    s.ctrl.on_source_error("gst-core-error-quark", "no element 'mppvideodec' (unsupported)", 0);
    drive(s.ctrl, 1500);
    const auto mid = s.ctrl.status();
    check(mid.policy.capability_wait && mid.policy.total_attempts == 0, "g06 waits capability");
    check(gen_of(s.mgr, 0) == g0, "g06 no rebuild while waiting");
    s.ctrl.notify_capability_changed(railhmi::media::MultiStreamManager::now_ms());
    drive(s.ctrl, 3500);
    const auto st = s.ctrl.status();
    const long long g1 = gen_of(s.mgr, 0);
    observe("g06_after_change", st, g0, g1);
    check(st.policy.total_attempts == 1 && st.recovered && g1 == g0 + 1,
          "g06 single rebuild after change confirmed");
}

// ---- g07 ONVIF 三态 ----
void g07() {
    RecoveryControllerConfig def;
    RecoveryController c1(nullptr, def);
    check(c1.status().onvif_status == "disabled", "g07 default disabled");
    check(!c1.onvif_tick(0), "g07 default no tick");

    RecoveryControllerConfig unconf;
    unconf.onvif_watchdog_enabled = true;
    RecoveryController c2(nullptr, unconf);
    check(c2.status().onvif_status == "unavailable:UNCONFIGURED", "g07 unconfigured explicit");
    check(!c2.onvif_tick(0), "g07 unconfigured no action");

    RecoveryControllerConfig conf;
    conf.onvif_watchdog_enabled = true;
    conf.onvif_address = "http://192.0.2.9/onvif/device_service";
    RecoveryController c3(nullptr, conf);
    check(c3.status().onvif_status == "unsupported:no_protocol", "g07 configured unsupported explicit");
    check(!c3.onvif_tick(0), "g07 configured no action (protocol not registered)");
}

// ---- g08 单路隔离 ----
void g08() {
    std::vector<ChannelSpec> specs;
    for (const char* id : {"CAM-01", "CAM-02", "CAM-03"}) {
        ChannelSpec sp;
        sp.camera_id = id;
        sp.cfg.camera_id = id;
        sp.cfg.source = SourceKind::Test;
        specs.push_back(sp);
    }
    MultiStreamManager mgr(specs);
    RecoveryControllerConfig cc;
    cc.policy = short_cfg();
    RecoveryController c(mgr.worker(0), cc);
    std::string err;
    if (mgr.start_all(&err) != 3 || !warmup(mgr, 3)) {
        check(false, "g08 setup"); return;
    }
    const long long g0 = gen_of(mgr, 0), g1 = gen_of(mgr, 1), g2 = gen_of(mgr, 2);
    const unsigned long long seq1_before = mgr.stats()[1].last_seq;
    mgr.request_cut(0);
    drive(c, 3500);
    const auto st = c.status();
    const long long g0a = gen_of(mgr, 0), g1a = gen_of(mgr, 1), g2a = gen_of(mgr, 2);
    const unsigned long long seq1_after = mgr.stats()[1].last_seq;
    observe("g08", st, g0, g0a);
    check(st.recovered && g0a == g0 + 1, "g08 ch0 rebuilt+confirmed");
    check(g1a == g1 && g2a == g2, "g08 other channels generation unchanged");
    check(seq1_after > seq1_before, "g08 other channels still frame-advancing");
}

}  // namespace

int main() {
    g01();
    g02();
    g03();
    g03b();
    g04();
    g05();
    g06();
    g07();
    g08();
    std::printf("vext summary %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
