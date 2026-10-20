// _verify17_vext.cpp — VERIFY-N012-OVERALL 交付整体轮自有扩展夹具（软件验证 VERIFY-20261021-n012-v1；判据与 ITEM-017 VERIFY-20261018-v1 已验证夹具逐字节一致，仅路径自新）。
// 板端 g++ 直编、链接交付 librailhmi_core.a 黑盒驱动 core::LatencyHistogram 与
// measurement_json/measurements_archive_json，覆盖交付探针/交付单测未断言的边界（判据独立推导）：
//
//  h01..h09  最近秩分位语义（奇 n=7 逐点：p50=4号/p90=p99=max；p≤0→min、p≥100→max、越界戒断）
//  h10..h14  偶 n=8 分位（rank=4 严格）+ p12.5/p87.5 半点 rank
//  h15..h17  cap 窗口裁剪语义：add 有序数组 erase(begin()) —— 乱序输入丢弃当前最小值（
//            反直觉域，交付单测未覆盖单调以外的插入序）
//  h18..h18c 单调输入 = drop-oldest 等价口径（交付单测已覆盖，回归）
//  h19/h19b  恒值样本 + cap 重复样本窗口
//  h20/h21   1e9 us 大值域 mean 不上溢
//  h22/h22b  负样本存取（直方图存任意 ll 不失真）
//  h23       mean 连续 add 一致
//  j01..j06  单条 JSON 全嵌套块（latency/sync/storage/recovery/window 6 位精度）
//  j07..j10  三类源 token × 三态 token 完整面（Recorded/ToBeExecuted/NotApplicable）
//  j11..j13  归档：空档精确串、双记录序、三记录条数
//
// 输出: v17 <PASS|FAIL> <case>；退出码=失败数。

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "railhmi/core/metrics.h"

using railhmi::core::LatencyHistogram;
using railhmi::core::MeasurementRecord;
using railhmi::core::MeasurementStatus;
using railhmi::core::SourceClass;
using railhmi::core::measurement_json;
using railhmi::core::measurements_archive_json;
using railhmi::core::to_string;

namespace {
int g_fail = 0;
void check(bool ok, const char* name, const std::string& detail = "") {
    if (ok) std::printf("v17 PASS %s\n", name);
    else {
        ++g_fail;
        std::printf("v17 FAIL %s %s\n", name, detail.c_str());
    }
}
}  // namespace

int main() {
    // ---- h01..h09 最近秩分位（奇 n=7）----
    {
        LatencyHistogram h;
        for (long long v : {100, 200, 300, 400, 500, 600, 700}) h.add(v);
        check(h.count() == 7 && h.min_us() == 100 && h.max_us() == 700, "h01_odd7_minmax");
        check(h.percentile(50) == 400, "h02_odd7_p50_middle");
        check(h.percentile(90) == 700, "h03_odd7_p90_ceil0.9*7=6.3->7");
        check(h.percentile(99) == 700, "h04_odd7_p99_max");
        check(h.percentile(0) == 100 && h.percentile(100) == 700, "h05_bounds_minmax");
        check(h.percentile(-5) == 100, "h06_negative_p_maps_min");
        check(h.percentile(150) == 700, "h07_over100_maps_max");
        // 插入乱序不改变有序不变量（add 二分插入）
        check(h.percentile(25) == 200, "h08_odd7_p25_rank1.75->2");
        check(h.percentile(60) == 500, "h09_odd7_p60_rank4.2->5");
    }
    // ---- h10..h14 偶 n=8 分位核对（rank=ceil(.5*8)=4 → 第4样本）----
    {
        LatencyHistogram h;
        for (long long v : {10, 20, 30, 40, 50, 60, 70, 80}) h.add(v);
        check(h.percentile(50) == 40, "h10_even8_p50_lower_mid");
        check(h.percentile(90) == 80, "h11_even8_p90");
        check(h.percentile(12.5) == 10, "h12_even8_p12.5_rank1");
        check(h.percentile(87.5) == 70, "h13_even8_p87.5_rank7");
        check(h.percentile(1) == 10 && h.percentile(99) == 80, "h14_even8_p1_p99");
    }
    // ---- h15..h17 窗口裁剪语义：add 保持有序数组，超限 erase(begin()) ——
    // 裁剪丢的是**当前最小值**而非"最新插入"（交付 Metrics.h 注释对单调输入侧重不含乱序域）；
    // 乱序输入 {8000,1000,6000,2000,7000,4000} cap=4 数组裁剪序列：
    //   8000 → 1000,8000 → 1000,6000,8000 → 1000,2000,6000,8000 → erase(min=1000)
    //   → insert 7000: 2000,6000,7000,8000 → erase(min=2000) → insert 4000: 4000,6000,7000,8000。
    // 最终窗口 = 数组中最小的四个此前存活样本：{4000,6000,7000,8000}。
    {
        LatencyHistogram h(4);
        for (long long v : {8000, 1000, 6000, 2000, 7000, 4000}) h.add(v);
        check(h.count() == 4, "h15_capped4");
        check(h.min_us() == 4000 && h.max_us() == 8000, "h16_out_of_order_cap4_window_upper4");
        check(h.percentile(50) == 6000 && h.percentile(90) == 8000 && h.percentile(99) == 8000,
              "h17_out_of_order_cap4_percentiles");
    }
    // ---- h18..h18c 单调大流量窗口语义：单调递增下 erase(begin()) 等价 drop-oldest（交付单测口径）----
    {
        LatencyHistogram h(3);
        for (long long v : {100, 200, 300, 400, 500}) h.add(v);
        check(h.count() == 3 && h.min_us() == 300 && h.max_us() == 500,
              "h18_monotonic_newest_kept");
        check(h.percentile(50) == 400, "h18b_monotonic_p50");
        h.clear();
        check(h.count() == 0 && h.min_us() == -1 && h.max_us() == -1 && h.percentile(50) == -1 &&
                  h.mean_us() == -1.0,
              "h18c_clear_resets_all");
    }
    // ---- h19 恒值样本与重复样本窗口 ----
    {
        LatencyHistogram h;
        for (long long v : {5, 5, 5, 5}) h.add(v);
        check(h.min_us() == 5 && h.max_us() == 5 && h.percentile(50) == 5 && h.percentile(99) == 5 &&
                  h.mean_us() == 5.0,
              "h19_all_equal_constant");
        // 相邻样本重复：cap 窗口内 erase(begin()) 丢一个 5，窗口不破损。
        LatencyHistogram h2(2);
        h2.add(5);
        h2.add(5);
        h2.add(5);
        check(h2.count() == 2 && h2.min_us() == 5 && h2.max_us() == 5, "h19b_dupe_cap_window");
        LatencyHistogram big;
        big.add(0);
        big.add(1000000000LL);
        big.add(999999999LL);
        check(big.min_us() == 0 && big.max_us() == 1000000000LL, "h20_1e9_max_stored");
        check(big.mean_us() > 666666666.0 && big.mean_us() < 666666667.0, "h21_mean_large_no_overflow");
    }
    // ---- h22 负样本：负时延深度计数（数据面合法域，含负数即记录）----
    {
        LatencyHistogram h;
        h.add(-5);
        h.add(0);
        h.add(5);
        check(h.min_us() == -5 && h.max_us() == 5, "h22_negative_sample_stored");
        check(h.percentile(50) == 0 && h.percentile(90) == 5, "h22b_negative_p50_p90");
    }
    // ---- h23 mean 连续 add 双操作一致 ----
    {
        LatencyHistogram h;
        h.add(100);
        h.add(300);
        const double m2 = h.mean_us();
        h.add(500);
        const double m3 = h.mean_us();
        check(m2 == 200.0 && m3 == 300.0, "h23_mean_updates_add");
    }
    // ---- j01..j06 单条 JSON 结构 ----
    {
        MeasurementRecord r;
        r.source_class = SourceClass::Simulation;
        r.status = MeasurementStatus::Recorded;
        r.label = "verify17";
        r.window_s = 3.5;
        r.throughput.source_fps = 25.0;
        r.throughput.display_fps = 24.0;
        r.throughput.cpu_pct = 50.0;
        r.throughput.rss_kb = 12345;
        r.throughput.npu_pct = -1.0;
        r.throughput.npu_note = "unavailable:X";
        r.throughput.temp_c = 40.25;
        r.arrival_decode.add(10);
        r.decode_convert.add(20);
        r.convert_present.add(30);
        r.end_to_end.add(60);
        r.sync.available = true;
        r.sync.in_sync = true;
        r.sync.max_deviation_ms = 5;
        r.storage.measured_bitrate_bps = 2000000.0;
        r.storage.clip_bytes = 64000;
        r.storage.free_bytes = 1000;
        r.storage.total_bytes = 2000;
        r.recovery.attempts = 2;
        r.recovery.last_recovery_ms = 350;

        const std::string j = measurement_json(r);
        check(j.find("\"source_class\":\"simulation\"") != std::string::npos &&
                  j.find("\"status\":\"recorded\"") != std::string::npos,
              "j01_recorded_fields");
        check(j.find("\"arrival_decode\":{\"n\":1") != std::string::npos &&
                  j.find("\"p50\":10") != std::string::npos,
              "j02_latency_nest");
        check(j.find("\"max_deviation_ms\":5") != std::string::npos &&
                  j.find("\"sync\":{\"available\":true,\"in_sync\":true") != std::string::npos,
              "j03_sync_true_block");
        check(j.find("\"clip_bytes\":64000") != std::string::npos &&
                  j.find("\"free_bytes\":1000") != std::string::npos &&
                  j.find("\"total_bytes\":2000") != std::string::npos,
              "j04_storage_block");
        check(j.find("\"recovery\":{\"attempts\":2,\"last_recovery_ms\":350}") != std::string::npos,
              "j05_recovery_block");
        check(j.find("\"window_s\":3.500000") != std::string::npos, "j06_window_6dp");
    }
    // ---- j07..j10 三类源 × 三态 token 往返（独立 to_string 完整面）----
    check(std::string(to_string(SourceClass::Rtsp)) == "rtsp" &&
              std::string(to_string(SourceClass::File)) == "file" &&
              std::string(to_string(SourceClass::Simulation)) == "simulation",
          "j07_source_tokens");
    check(std::string(to_string(MeasurementStatus::Recorded)) == "recorded" &&
              std::string(to_string(MeasurementStatus::ToBeExecuted)) == "to_be_executed" &&
              std::string(to_string(MeasurementStatus::NotApplicable)) == "not_applicable",
          "j08_status_tokens");
    {
        MeasurementRecord t;
        t.source_class = SourceClass::Rtsp;
        t.status = MeasurementStatus::ToBeExecuted;
        t.reason = "CREDENTIAL_MISSING";
        const std::string jt = measurement_json(t);
        check(jt.find("\"status\":\"to_be_executed\"") != std::string::npos &&
                  jt.find("CREDENTIAL_MISSING") != std::string::npos,
              "j09_tbe_credential_reason");
        MeasurementRecord na;
        na.source_class = SourceClass::File;
        na.status = MeasurementStatus::NotApplicable;
        na.reason = "NO_FILE";
        check(measurement_json(na).find("\"status\":\"not_applicable\"") != std::string::npos,
              "j10_not_applicable_token");
    }
    // ---- j11..j13 归档结构 ----
    {
        const std::string e = measurements_archive_json({});
        check(e == "{\"schema\":\"railhmi.measure.v1\",\"records\":[]}", "j11_empty_archive_exact");
        MeasurementRecord a;
        a.source_class = SourceClass::Simulation;
        MeasurementRecord b;
        b.source_class = SourceClass::Rtsp;
        const std::string two = measurements_archive_json({a, b});
        const std::string sep = "{\"schema\":\"railhmi.measure.v1\",\"records\":[";
        check(two.find(sep) == 0 && two.find("\"source_class\":\"simulation\"") <
              two.find("\"source_class\":\"rtsp\""),
              "j12_two_records_order");
        std::vector<MeasurementRecord> v;
        for (int i = 0; i < 3; ++i) v.push_back(a);
        const std::string three = measurements_archive_json(v);
        std::size_t pos = 0;
        int commas = 0;
        while ((pos = three.find("\"source_class\":\"simulation\"", pos)) != std::string::npos) {
            ++commas;
            pos += 1;
        }
        check(commas == 3, "j13_three_records_count");
    }

    std::printf("v17 SUMMARY fails=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
