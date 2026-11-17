// railhmi-core BLE 门磁测试：广播报文解析、机队汇总真值、可选配置节。
// 协议事实来自 ustone-BLE门磁感应传感器-开发向导.pdf / -使用说明.pdf（v0.1, 2024/10/30）。
// 纯 core，无 Qt/DBus/BlueZ 依赖；可在最小工具链上回归。

#include "test_util.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/door_sensor.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

namespace {

using namespace railhmi::core;

std::vector<std::uint8_t> frame(std::uint8_t door_value, int battery_pct, bool with_battery = true) {
    // byte0 固定标识 0x07；byte1..4 设备 ID 小端序 0x12345678；之后 <Key,Value>。
    std::vector<std::uint8_t> v = {0x07, 0x78, 0x56, 0x34, 0x12, 0x08, door_value};
    if (with_battery) {
        v.push_back(0x63);
        v.push_back(static_cast<std::uint8_t>(battery_pct & 0xFF));
        v.push_back(static_cast<std::uint8_t>((battery_pct >> 8) & 0xFF));
    }
    return v;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has_default(const AppSettings& s, const std::string& path) {
    for (const auto& d : s.applied_defaults)
        if (d == path) return true;
    return false;
}

bool has_error(const ConfigLoadResult& r, const std::string& path) {
    for (const auto& e : r.errors)
        if (e.path.find(path) != std::string::npos) return true;
    return false;
}

void test_parse_closed_and_ajar() {
    const DoorFrameFormat fmt;
    DoorFrame f = parse_door_frame(frame(0, 80), fmt);
    railhmi_test::check(f.valid && f.reason.empty(), "valid closed frame parses");
    railhmi_test::check(f.device_id == 0x12345678u, "device id read little-endian");
    railhmi_test::check(f.has_door && f.door == DoorState::Closed, "door value 0 -> Closed");
    railhmi_test::check(f.has_battery && f.battery_pct == 80, "battery 2B little-endian percent");

    f = parse_door_frame(frame(1, 15), fmt);
    railhmi_test::check(f.valid && f.has_door && f.door == DoorState::Ajar,
                        "感应不到磁铁=1 -> 门未关严");
}

void test_parse_layout_variants() {
    const DoorFrameFormat fmt;
    // 厂商 31 字节广播：数据报文尾部零填充，解析必须停在第一个 0x00 key。
    std::vector<std::uint8_t> padded = frame(1, 66);
    padded.resize(kDoorMaxAdvBytes, 0x00);
    const DoorFrame p = parse_door_frame(padded, fmt);
    railhmi_test::check(p.valid && p.door == DoorState::Ajar && p.battery_pct == 66,
                        "31-byte padded advertisement parses correctly");

    // BlueZ ManufacturerData 常带 2 字节 company id 前缀：允许在前 4 字节内定位 0x07。
    std::vector<std::uint8_t> prefixed = {0x4c, 0x00};
    const std::vector<std::uint8_t> body = frame(0, 42);
    prefixed.insert(prefixed.end(), body.begin(), body.end());
    const DoorFrame q = parse_door_frame(prefixed, fmt);
    railhmi_test::check(q.valid && q.door == DoorState::Closed && q.battery_pct == 42,
                        "company-id prefix tolerated");

    // 显式 payload_offset 也等价。
    DoorFrameFormat off;
    off.payload_offset = 2;
    const DoorFrame r = parse_door_frame(prefixed, off);
    railhmi_test::check(r.valid && r.device_id == 0x12345678u, "explicit payload_offset works");
}

void test_parse_rejects_and_unknown_key() {
    const DoorFrameFormat fmt;
    const DoorFrame empty = parse_door_frame(nullptr, 0, fmt);
    railhmi_test::check(!empty.valid && empty.reason == kDoorReasonUnsupported,
                        "null/short frame -> UNSUPPORTED token");

    std::vector<std::uint8_t> short_frame = {0x07, 1, 2, 3, 4, 0x08};
    const DoorFrame s = parse_door_frame(short_frame, fmt);
    railhmi_test::check(!s.valid && s.reason == kDoorReasonUnsupported,
                        "truncated frame does not fabricate door state");

    // 别的 BLE 设备的广播：前 4 字节没有 0x07 -> 不误判成门磁。
    std::vector<std::uint8_t> other = {0x16, 0x08, 0x00, 0xa8, 0xd5, 0x7c,
                                       0xa8, 0x48, 0x34, 0x07, 0x02, 0x01};
    const DoorFrame o = parse_door_frame(other, fmt);
    railhmi_test::check(!o.valid && o.reason == kDoorReasonUnsupported,
                        "non-door advertisement rejected");

    // 未知 Key 无从判定 Value 长度：保留已解析的门磁状态并停止，绝不猜测后续布局。
    std::vector<std::uint8_t> unknown = frame(1, 0, false);
    unknown.push_back(0x77);
    unknown.push_back(0x00);
    const DoorFrame u = parse_door_frame(unknown, fmt);
    railhmi_test::check(u.valid && u.has_door && u.door == DoorState::Ajar && !u.has_battery,
                        "unknown key stops parsing but keeps decoded fields");

    // 电池百分比越界不写入，仍保留门磁真值。
    std::vector<std::uint8_t> bad_battery = {0x07, 1, 2, 3, 4, 0x08, 0x00, 0x63, 0x10, 0x27};
    const DoorFrame b = parse_door_frame(bad_battery, fmt);
    railhmi_test::check(b.valid && !b.has_battery && b.door == DoorState::Closed,
                        "out-of-range battery ignored, not clamped to a fake value");
}

// 现场实测（RK3588 btmon，BWD1，MAC C0:6D:9A:AC:C3:87，广播名 T1）：
//   ManufacturerData company=0xFF07，value(23B)
//   磁铁贴合(关闭)：FF FF FF 06 00 63 62 00 ... <计数>
//   磁铁分开(未关严)：FF FF FF 06 01 63 62 00 ... <计数>
// 唯一状态位是 0x06 后面的 1B 布尔值；PDF 标的门磁 Key 0x08 与现场固件不符。
// 0x07 帧首被 BlueZ 当成 company id 低字节剥掉；runtime 会把它拼回，
// 因此这里用「company 小端 + value」的 25B 候选作为解析输入。
std::vector<std::uint8_t> real_bwd1_frame(std::uint8_t door_value) {
    std::vector<std::uint8_t> v = {
        0x07, 0xFF,                      // company 0xFF07 小端（低位即 PDF 的 0x07 marker）
        0xFF, 0xFF, 0xFF, 0xFF,          // 设备 ID = 0xFFFFFFFF
        0x06, door_value,                // 现场固件门磁字段（PDF 写 0x08），1B 布尔
        0x63, 0x62, 0x00                 // 电池 Key 0x63 + 98%
    };
    v.resize(25, 0x00);
    v.back() = 0xD0;                     // 广播计数（每次 +1）
    return v;
}

void test_parse_real_bwd1_advertisement() {
    const DoorFrameFormat strict;
    DoorFrameFormat relaxed;
    relaxed.vendor_tolerant = true;

    const std::vector<std::uint8_t> closed = real_bwd1_frame(0);
    const std::vector<std::uint8_t> ajar_frame = real_bwd1_frame(1);

    // 严格按 PDF（Key 0x08）解析现场帧会失败，必须走已确认厂商帧的宽松布局。
    const DoorFrame s = parse_door_frame(closed, strict);
    railhmi_test::check(!s.valid && s.reason == kDoorReasonUnsupported,
                        "real BWD1 frame rejected by strict PDF layout (key 0x06)");

    const DoorFrame c = parse_door_frame(closed, relaxed);
    railhmi_test::check(c.valid && c.reason.empty(),
                        "real BWD1 frame decodes under vendor-tolerant layout");
    railhmi_test::check(c.has_door && c.door == DoorState::Closed,
                        "live capture: magnet present -> 0x06 value 0 -> Closed");
    railhmi_test::check(c.has_battery && c.battery_pct == 98,
                        "real closed frame -> battery 98%");
    railhmi_test::check(c.device_id == 0xFFFFFFFFu,
                        "real frame device id read from advertised bytes");
    railhmi_test::check(kDoorVendorCompany == 0xFF07,
                        "BWD1 company id constant matches live capture");

    const DoorFrame a = parse_door_frame(ajar_frame, relaxed);
    railhmi_test::check(a.valid && a.has_door && a.door == DoorState::Ajar &&
                            a.has_battery && a.battery_pct == 98,
                        "live capture: magnet separated -> 0x06 value 1 -> Ajar");

    // 0x06 后的值不是 0/1 时不得当成门磁状态。
    std::vector<std::uint8_t> bad_value = real_bwd1_frame(0);
    bad_value[6] = 0x7F;
    railhmi_test::check(!parse_door_frame(bad_value, relaxed).has_door,
                        "0x06 with non-boolean value never treated as door state");

    // PDF 标注的 Key 0x08 帧仍必须按严格布局解析（兼容文档）。
    railhmi_test::check(parse_door_frame(frame(1, 66), strict).door == DoorState::Ajar,
                        "documented Key 0x08 frame still parses in strict mode");

    // 宽松定位必须是有界的：12 个未知头部字节之后即使有 0x63 也不能硬猜。
    std::vector<std::uint8_t> long_header = {0x07, 0xFF, 0xFF, 0xFF, 0xFF};
    for (int i = 0; i < 12; ++i) long_header.push_back(0x66);
    long_header.push_back(0x63);
    long_header.push_back(0x62);
    long_header.push_back(0x00);
    railhmi_test::check(!parse_door_frame(long_header, relaxed).valid,
                        "vendor-tolerant skips only a bounded header window");

    // 其它厂商的 ManufacturerData 即便误开宽松模式也不得解析成门磁。
    const std::vector<std::uint8_t> other_vendor = {
        0x4C, 0x00, 0x00, 0x91, 0xDD, 0x38, 0x2F, 0xA1, 0xE9, 0xDB};
    railhmi_test::check(!parse_door_frame(other_vendor, relaxed).valid,
                        "other-vendor manufacturer data never decoded as a door frame");
}

DoorSensorState make_state(const std::string& mac, DoorState door, long long now,
                           bool verified = true, const std::string& reason = "") {
    DoorSensorState d;
    d.id = "DOOR-" + mac;
    d.mac = mac;
    d.door = door;
    d.last_seen_ms = now;
    d.online = true;
    d.verified = verified;
    d.reason = reason;
    return d;
}

void test_summary_truth_table() {
    const long long now = 1000000;
    const int stale = 60000;

    DoorFleetSummary none = summarize_door_sensors({}, now, stale, 20);
    railhmi_test::check(none.level == StatusLevel::Unavailable && none.value == "未接入" &&
                            none.alarm_text.empty(),
                        "no configured sensor -> 未接入 / unavailable");

    std::vector<DoorSensorState> all_closed = {
        make_state("AA:BB:CC:DD:EE:01", DoorState::Closed, now),
        make_state("AA:BB:CC:DD:EE:02", DoorState::Closed, now)};
    DoorFleetSummary ok = summarize_door_sensors(all_closed, now, stale, 20);
    railhmi_test::check(ok.level == StatusLevel::Ok && ok.value == "全关" && ok.online == 2,
                        "all online + closed -> 全关 / ok");

    all_closed[1].door = DoorState::Ajar;
    DoorFleetSummary ajar = summarize_door_sensors(all_closed, now, stale, 20);
    railhmi_test::check(ajar.level == StatusLevel::Alarm && ajar.ajar == 1 &&
                            !ajar.alarm_text.empty(),
                        "any ajar -> L2 alarm text for P1 alarm bar");

    all_closed[1].door = DoorState::Closed;
    all_closed[1].last_seen_ms = now - stale - 1;
    DoorFleetSummary stale_sum = summarize_door_sensors(all_closed, now, stale, 20);
    railhmi_test::check(stale_sum.level == StatusLevel::Unavailable && stale_sum.online == 1,
                        "stale/offline -> unavailable, never normal");

    std::vector<DoorSensorState> unknown = {make_state("A", DoorState::Unknown, now)};
    railhmi_test::check(summarize_door_sensors(unknown, now, stale, 20).level ==
                            StatusLevel::Unavailable,
                        "unknown door state -> unavailable");

    std::vector<DoorSensorState> unverified = {
        make_state("B", DoorState::Closed, now, false, kDoorReasonUnverified)};
    railhmi_test::check(summarize_door_sensors(unverified, now, stale, 20).level ==
                            StatusLevel::Unavailable,
                        "unverified device never reported as 全关");

    std::vector<DoorSensorState> low = {make_state("C", DoorState::Closed, now)};
    low[0].has_battery = true;
    low[0].battery_pct = 12;
    DoorFleetSummary warn = summarize_door_sensors(low, now, stale, 20);
    railhmi_test::check(warn.level == StatusLevel::Warning && warn.low_battery == 1,
                        "low battery -> warning");
}

void test_labels() {
    railhmi_test::check(std::string(door_state_token(DoorState::Closed)) == "closed" &&
                            std::string(door_state_token(DoorState::Ajar)) == "ajar" &&
                            std::string(door_state_label(DoorState::Unknown)) == "未知",
                        "stable door state tokens / labels");
}

void test_config_section() {
    ConfigLoadResult real = load_config_dir(RAILHMI_CONFIG_DIR);
    railhmi_test::check(real.ok, "baseline app.yaml with door_sensor section loads OK");
    railhmi_test::check(real.settings.app.door_sensor.enabled &&
                            real.settings.app.door_sensor.name_filter == "T1" &&
                            real.settings.app.door_sensor.devices.empty(),
                        "door sensor enabled with T1 name filter, no fabricated MAC");
    // 11-16 现场放宽：厂家确认 1s 广播，读取节奏改为 1s（上限仍 10s）。
    railhmi_test::check(real.settings.app.door_sensor.poll_interval_ms == 1000 &&
                            real.settings.app.door_sensor.stale_ms == 60000,
                        "1s fast poll / stale values read from config");
    const std::string app_full = read_file(std::string(RAILHMI_CONFIG_DIR) + "/app.yaml");
    const std::string old_poll = "poll_interval_ms: 1000";
    const std::size_t poll_at = app_full.find(old_poll);
    railhmi_test::check(poll_at != std::string::npos, "fixture can locate poll_interval_ms");
    // 上限 10000ms 仍必须保留：不能把轮询放得比 10s 更慢。
    std::string too_slow = app_full;
    if (poll_at != std::string::npos) too_slow.replace(poll_at, old_poll.size(), "poll_interval_ms: 30000");
    ConfigLoadResult slow = load_config_from_strings(
        read_file(std::string(RAILHMI_CONFIG_DIR) + "/cameras.yaml"), too_slow,
        read_file(std::string(RAILHMI_CONFIG_DIR) + "/thresholds.yaml"),
        read_file(std::string(RAILHMI_CONFIG_DIR) + "/roi.yaml"));
    railhmi_test::check(!slow.ok && has_error(slow, "app.door_sensor.poll_interval_ms"),
                        "poll interval > 10s rejected with concrete field path");
    // 下限 1000ms：不允许把 D-Bus 轮询压到亚秒级，避免无意义的系统总线压力。
    std::string too_fast = app_full;
    if (poll_at != std::string::npos) too_fast.replace(poll_at, old_poll.size(), "poll_interval_ms: 200");
    ConfigLoadResult fast = load_config_from_strings(
        read_file(std::string(RAILHMI_CONFIG_DIR) + "/cameras.yaml"), too_fast,
        read_file(std::string(RAILHMI_CONFIG_DIR) + "/thresholds.yaml"),
        read_file(std::string(RAILHMI_CONFIG_DIR) + "/roi.yaml"));
    railhmi_test::check(!fast.ok && has_error(fast, "app.door_sensor.poll_interval_ms"),
                        "poll interval < 1s rejected with concrete field path");

    // 删掉整个可选节：必须回退 enabled=false 并登记默认值，不静默启用。
    const std::string cameras = read_file(std::string(RAILHMI_CONFIG_DIR) + "/cameras.yaml");
    std::string app = read_file(std::string(RAILHMI_CONFIG_DIR) + "/app.yaml");
    const std::string thresholds = read_file(std::string(RAILHMI_CONFIG_DIR) + "/thresholds.yaml");
    const std::string roi = read_file(std::string(RAILHMI_CONFIG_DIR) + "/roi.yaml");
    const std::size_t cut = app.find("  door_sensor:");
    railhmi_test::check(cut != std::string::npos, "test fixture can locate door_sensor section");
    if (cut != std::string::npos) app.resize(cut);
    ConfigLoadResult absent = load_config_from_strings(cameras, app, thresholds, roi);
    railhmi_test::check(absent.ok && !absent.settings.app.door_sensor.enabled &&
                            has_default(absent.settings, "app.door_sensor"),
                        "absent optional section -> disabled + applied_defaults recorded");

    // 非法值报出具体字段路径。
    ConfigLoadResult bad = load_config_from_strings(
        cameras,
        app + "  door_sensor:\n    enabled: true\n    stale_ms: 5\n    devices:\n"
              "      - id: D1\n        mac: NOT-A-MAC\n",
        thresholds, roi);
    railhmi_test::check(!bad.ok && has_error(bad, "app.door_sensor.stale_ms") &&
                            has_error(bad, "app.door_sensor.devices[0].mac"),
                        "invalid door_sensor values report concrete field paths");
}

}  // namespace

void run_door_sensor_tests() {
    test_parse_closed_and_ajar();
    test_parse_layout_variants();
    test_parse_rejects_and_unknown_key();
    test_parse_real_bwd1_advertisement();
    test_summary_truth_table();
    test_labels();
    test_config_section();
}
