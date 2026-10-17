#pragma once

// railhmi-core: 统一时间基准（ITEM-007 / DATA-001）。
// 单调微秒（steady_clock）为内部基准；启动时与 UTC 墙钟毫秒建立一次偏移，
// 之后 ts_wall_ms = to_wall_ms(mono_us)，成对写入事件/健康/流事件表。
// 纯标准库，可用显式参照点做确定性单元测试。

#include <string>

namespace railhmi {
namespace core {

class TimeBase {
public:
    // 记录一次 墙钟毫秒 <-> 单调微秒 参照点（启动校准时调用）。
    void calibrate(long long wall_ms, long long mono_us);

    bool calibrated() const { return calibrated_; }

    // 单调微秒 -> UTC 毫秒（需已校准；未校准时返回 mono_us/1000 的退化值）。
    long long to_wall_ms(long long mono_us) const;

    // 未校准时返回 false 且不写 *wall_ms：调用方据此**禁止写入 UTC 业务事件**（不合规不静默）。
    bool try_to_wall_ms(long long mono_us, long long* wall_ms) const;
    // 是否允许写入 UTC 业务事件（等价于已校准）。
    bool can_write_utc() const { return calibrated_; }

    // UTC 毫秒 -> 单调微秒。
    long long to_mono_us(long long wall_ms) const;

    static long long system_now_ms();   // system_clock 墙钟毫秒（UTC epoch）
    static long long steady_now_us();   // steady_clock 单调微秒（进程内基准）

private:
    long long ref_wall_ms_ = 0;
    long long ref_mono_us_ = 0;
    bool calibrated_ = false;
};

}  // namespace core
}  // namespace railhmi
