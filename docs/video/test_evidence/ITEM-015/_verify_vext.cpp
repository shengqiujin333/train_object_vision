// vext_timebase.cpp — ITEM-015 验证侧自有夹具（software_verification VERIFY-20261017-v1）
//
// 链接交付 librailhmi_core.a，黑盒驱动 TimeBase / compute_replay_sync / SeekController，
// 覆盖交付探针与单测未测的边界（判据独立重推导，不采信 acceptance 行）：
//   v01 阈值边界: dev==threshold 同步（<=），dev==threshold+1 不同步且提示（>）
//   v02 未校准守卫强: try_to_wall_ms 不写 *out（保持调用方原值），can_write_utc=false
//   v03 重复校准: 第二次 calibrate 覆盖参照点（换算按新参照点，不叠加两次偏移）
//   v04 换算只做一次: to_wall_ms 幂等（同输入重复调用同输出），to_mono_us 往返稳定
//   v05 负/大输入: 负 mono（校准前时区）、大跨度换算不溢出（/1000 与 *1000 对称）
//   v06 同步: 空 locked 列表 available=true（无从偏差但不谎报不可用）
//   v07 同步: 未知相机键不影响已锁定路判定；missing 列表按锁定序（多缺）
//   v08 同步: 负阈值（极端配置）等价 dev<=neg 不可满足 → in_sync=false 有提示不崩溃
//   v09 seek: 容差边界 |actual-target|==tol 恰 reached（<= 语义）
//   v10 seek: 连续陈旧回调（多代旧）位置保持最后接受值；reset 后 generation 归 0
//   v11 seek: 同代多次 on_position 以最后接受值为准（新回调覆盖旧位置）
//   v12 seek: 未发起任何 seek 时回调一律陈旧（generation 0 → 任何 0 回调 accepted? 检查语义）
//   v13 同步: 单路 + 计算路径（positions.size()<2 分支）available=true dev=0
//   v14 守卫与同步联合: 未校准时同步 unavailable UNCALIBRATED（即便各路时钟齐备）
//
// 判据依据: 任务书 ITEM-015 + 接口契约 §4/§8 + 设计 §5.2/§5.3/§8.3。

#include <climits>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/replay_sync.h"
#include "railhmi/core/timebase.h"

namespace {

using railhmi::core::SeekController;
using railhmi::core::TimeBase;
using railhmi::core::compute_replay_sync;

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

// ---- v01 阈值边界: <= 同步 / > 不同步（半开语义验证）----
void v01() {
    std::map<std::string, long long> edge{{"CAM-01", 1000}, {"CAM-02", 1300}};
    auto eq = compute_replay_sync({"CAM-01", "CAM-02"}, edge, true, 300);
    check(eq.available && eq.in_sync && eq.max_deviation_ms == 300 && eq.prompt.empty(),
          "v01 dev==threshold in_sync (<=)");
    std::map<std::string, long long> over{{"CAM-01", 1000}, {"CAM-02", 1301}};
    auto gt = compute_replay_sync({"CAM-01", "CAM-02"}, over, true, 300);
    check(gt.available && !gt.in_sync && gt.max_deviation_ms == 301 && !gt.prompt.empty(),
          "v01 dev==threshold+1 not in_sync (>)");
}

// ---- v02 未校准守卫: 不写输出参数 ----
void v02() {
    TimeBase tb;
    long long out_sentinel = 987654321LL;
    const bool wrote = tb.try_to_wall_ms(123450987LL, &out_sentinel);
    check(!tb.calibrated() && !tb.can_write_utc(), "v02 uncalibrated flags false");
    check(!wrote, "v02 try_to_wall_ms false");
    check(out_sentinel == 987654321LL, "v02 output not written (caller value preserved)");
    // nullptr 输出参数也要安全
    check(!tb.try_to_wall_ms(1LL, nullptr), "v02 nullptr out param safe");
}

// ---- v03 重复校准: 第二次 calibrate 覆盖（不叠加偏移）----
void v03() {
    TimeBase tb;
    tb.calibrate(1'700'000'000'000LL, 5'000'000LL);
    long long w1 = 0;
    tb.try_to_wall_ms(6'000'000LL, &w1);
    check(w1 == 1'700'000'001'000LL, "v03 first calibration converts +1s");
    // 重新校准到新参照点（重启语义）：换算按新参照点，结果不得包含旧偏移
    tb.calibrate(1'800'000'000'000LL, 9'000'000LL);
    long long w2 = 0;
    tb.try_to_wall_ms(10'000'000LL, &w2);
    check(w2 == 1'800'000'001'000LL, "v03 recalibration replaces reference (no offset stacking)");
}

// ---- v04 换算只做一次（幂等）：同输入重复调用同输出 ----
void v04() {
    TimeBase tb;
    tb.calibrate(1'700'000'000'000LL, 5'000'000LL);
    const long long a1 = tb.to_wall_ms(6'000'000LL);
    const long long a2 = tb.to_wall_ms(6'000'000LL);
    const long long a3 = tb.to_wall_ms(6'000'000LL);
    check(a1 == a2 && a2 == a3 && a1 == 1'700'000'001'000LL, "v04 to_wall_ms idempotent");
    const long long m1 = tb.to_mono_us(a1);
    const long long m2 = tb.to_mono_us(a1);
    check(m1 == m2 && m1 == 6'000'000LL, "v04 to_mono_us idempotent (same wall input)");
    // 双向往返再回到原 mono
    check(tb.to_wall_ms(m1) == a1, "v04 wall->mono->wall stable");
}

// ---- v05 负/大输入 ----
void v05() {
    TimeBase tb;
    tb.calibrate(1'700'000'000'000LL, 0LL);
    // mono 负值（steady_clock 以 epoch 起点计时的理论负区间）：换算线性不崩溃
    const long long neg = tb.to_wall_ms(-1'000'000LL);
    check(neg == 1'700'000'000'000LL - 1000LL, "v05 negative mono linear");
    // 大跨度: +1e12 us (≈11.6 天) 换算 /1000 精确（1e12µs = 1e9ms）
    const long long big = tb.to_wall_ms(1'000'000'000'000LL);
    check(big == 1'700'000'000'000LL + 1'000'000'000LL, "v05 large span /1000 exact");
    // UTC → mono 大跨度对称
    check(tb.to_mono_us(1'700'000'000'000LL + 1'000'000'000LL) == 1'000'000'000'000LL,
          "v05 large span *1000 exact");
    // 负 wall（UTC 早于参照点 1ms → -1000µs；×1000 线性）
    const long long negw = tb.to_mono_us(1'699'999'999'999LL);
    check(negw == -1000LL, "v05 negative wall linear");
}

// ---- v06 空 locked 列表 ----
void v06() {
    auto empty = compute_replay_sync({}, {{"CAM-01", 1000}}, true, 300);
    check(empty.available && empty.in_sync && empty.max_deviation_ms == 0,
          "v06 empty locked list available (no false unavailable)");
    auto empty_un = compute_replay_sync({}, {{"CAM-01", 1000}}, false, 300);
    check(!empty_un.available && empty_un.reason == "UNCALIBRATED",
          "v06 empty locked but uncalibrated still UNCALIBRATED");
}

// ---- v07 多缺路 + 未知键不影响锁定路 ----
void v07() {
    std::map<std::string, long long> heads{{"CAM-01", 1000}, {"CAM-03", 1100}, {"CAM-99", 9999}};
    auto multi = compute_replay_sync({"CAM-01", "CAM-02", "CAM-03", "CAM-04"}, heads, true, 300);
    check(!multi.available && multi.reason == "MISSING_CLOCK" && multi.missing.size() == 2,
          "v07 two missing reported");
    check(multi.missing[0] == "CAM-02" && multi.missing[1] == "CAM-04",
          "v07 missing in locked order (CAM-02, CAM-04)");
    // 未知键 CAM-99 不进 positions 也不进 missing（只消费锁定路）
    check(multi.prompt.empty(), "v07 no prompt on missing path");
    // 全锁定 + 未知键共存: 未知键不污染 max−min
    std::map<std::string, long long> full{{"CAM-01", 1000}, {"CAM-02", 1100}, {"CAM-99", 8888}};
    auto ok = compute_replay_sync({"CAM-01", "CAM-02"}, full, true, 300);
    check(ok.available && ok.in_sync && ok.max_deviation_ms == 100,
          "v07 unknown extra key ignored in deviation");
}

// ---- v08 负阈值（极端配置）不崩溃且有显式语义 ----
void v08() {
    std::map<std::string, long long> same{{"CAM-01", 1000}, {"CAM-02", 1000}};
    auto zero = compute_replay_sync({"CAM-01", "CAM-02"}, same, true, 0);
    check(zero.available && zero.in_sync && zero.max_deviation_ms == 0,
          "v08 threshold 0: identical heads in_sync");
    std::map<std::string, long long> diff{{"CAM-01", 1000}, {"CAM-02", 1001}};
    auto z1 = compute_replay_sync({"CAM-01", "CAM-02"}, diff, true, 0);
    check(z1.available && !z1.in_sync && !z1.prompt.empty(),
          "v08 threshold 0: 1ms dev prompts");
    // 负阈值: dev>=0 恒 > threshold → 不同步（保守语义），不崩溃
    auto neg = compute_replay_sync({"CAM-01", "CAM-02"}, same, true, -5);
    check(neg.available && !neg.in_sync, "v08 negative threshold conservative not crash");
}

// ---- v09 seek 容差边界: |d|==tol 恰 reached ----
void v09() {
    SeekController sc(500);
    const long long g = sc.request_seek(10'000LL);
    check(sc.on_position(g, 10'000LL - 500LL) && sc.state().reached, "v09 dev==-tol reached (<=");
    sc.reset();
    const long long g2 = sc.request_seek(10'000LL);
    check(sc.on_position(g2, 10'000LL + 501LL), "v09 dev==+tol+1 accepted");
    check(!sc.state().reached && sc.state().locating, "v09 dev==+tol+1 not reached");
    // 负容差夹具构造按实现归一（tol<0 → 0）：dev0 也达不到负容差 → locating
    SeekController sc_neg(-100);
    check(sc_neg.tolerance_ms() == 0, "v09 negative tolerance normalized to 0");
    const long long g3 = sc_neg.request_seek(1000LL);
    check(sc_neg.on_position(g3, 1000LL) && sc_neg.state().reached, "v09 tol0 exact target reached");
}

// ---- v10 多代陈旧回调保持最后接受位置；reset 归零 ----
void v10() {
    SeekController sc(1000);
    const long long g1 = sc.request_seek(10'000LL);
    check(sc.on_position(g1, 8'000LL), "v10 gen1 accepted at 8000");
    const long long g2 = sc.request_seek(20'000LL);
    const long long g3 = sc.request_seek(30'000LL);
    check(!sc.on_position(g1, 99'999LL), "v10 gen1 stale after two seeks");
    check(!sc.on_position(g2, 99'999LL), "v10 gen2 stale after newer seek");
    check(sc.state().position_ms == 8'000LL, "v10 position stays last accepted");
    sc.reset();
    check(sc.state().generation == 0 && sc.state().position_ms == 0 && !sc.state().reached,
          "v10 reset clears all");
    // reset 后旧代回调仍被拒（当前代 0 ≠ g3）
    check(!sc.on_position(g3, 1LL), "v10 post-reset generation mismatch guard");
}

// ---- v11 同代多次回调: 最后接受值为准 ----
void v11() {
    SeekController sc(1000);
    const long long g = sc.request_seek(50'000LL);
    check(sc.on_position(g, 40'000LL), "v11 first accepted");
    check(sc.on_position(g, 49'900LL), "v11 second accepted same gen");
    check(sc.state().position_ms == 49'900LL, "v11 last accepted wins");
    check(sc.state().reached && !sc.state().locating, "v11 reached at tolerance edge");
}

// ---- v12 初始 generation 0 语义: 未 seek 时 gen0 回调按当前代接受（实现行为:
//      on_position 用 != 比较代数，0==0 为当前代）——按实际行为断言；注意 target=0
//      + tol=1000 时 pos=100 落入容差 → reached=true（无激活 seek 的 reached 属
//      初始态语义，登记 O-2 交实现参考；「定位未到达不提前显示」验收针对 seek 后
//      的 locating 阶段，由 v09/v10 覆盖）----
void v12() {
    SeekController sc(1000);
    check(sc.on_position(0, 100LL), "v12 gen0 callback accepted as current (!= semantics)");
    check(sc.state().position_ms == 100LL, "v12 position recorded");
    check(sc.state().reached && !sc.state().locating,
          "v12 pre-seek gen0 within default target 0+tol (actual behavior, O-2)");
    // 首个 request_seek 仍从 gen1 起（用户可见世代 1），gen0 回调随即陈旧
    const long long g = sc.request_seek(50'000LL);
    check(g == 1, "v12 first seek generation is 1");
    check(!sc.on_position(0, 100LL), "v12 gen0 stale after first seek");
}

// ---- v13 单路: available 且 dev=0（<2 无从偏差）----
void v13() {
    auto one = compute_replay_sync({"CAM-05"}, {{"CAM-05", 1'700'000'000'000LL}}, true, 300);
    check(one.available && one.in_sync && one.max_deviation_ms == 0 && one.reason.empty(),
          "v13 single camera available dev0");
    check(one.prompt.empty(), "v13 no prompt for single");
}

// ---- v14 未校准 + 时钟齐备 → UNCALIBRATED（守卫优先于时钟）----
void v14() {
    std::map<std::string, long long> heads{{"CAM-01", 1000}, {"CAM-02", 1001}};
    auto un = compute_replay_sync({"CAM-01", "CAM-02"}, heads, false, 300);
    check(!un.available && un.reason == "UNCALIBRATED" && un.missing.empty(),
          "v14 uncalibrated priority over head presence");
    // prompt 空: 未校准不给出偏差提示（无从换算 UTC）
    check(un.prompt.empty(), "v14 no prompt when uncalibrated");
}

}  // namespace

int main() {
    v01();
    v02();
    v03();
    v04();
    v05();
    v06();
    v07();
    v08();
    v09();
    v10();
    v11();
    v12();
    v13();
    v14();
    std::printf("vext summary %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
