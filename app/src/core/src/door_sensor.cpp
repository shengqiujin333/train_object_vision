#include "railhmi/core/door_sensor.h"

#include <algorithm>

namespace railhmi {
namespace core {

namespace {

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

DoorFrame parse_door_frame(const std::uint8_t* data, std::size_t size,
                           const DoorFrameFormat& fmt) {
    DoorFrame out;
    out.reason = kDoorReasonUnsupported;
    if (!data || size < 7) return out;   // marker + 4B ID + 至少一组 <Key,Value>

    const std::size_t offset = fmt.payload_offset > 0
        ? static_cast<std::size_t>(fmt.payload_offset) : 0;
    if (offset >= size) return out;

    const std::uint8_t* p = data + offset;
    std::size_t n = size - offset;
    const std::uint8_t marker = static_cast<std::uint8_t>(fmt.marker);

    // 厂商标称帧首即 0x07；若前面还有 2 字节 company id 等前缀，在有限的窗口内容错定位。
    if (p[0] != marker) {
        bool found = false;
        const std::size_t limit = std::min<std::size_t>(4, n >= 7 ? n - 7 : 0);
        for (std::size_t i = 1; i <= limit; ++i) {
            if (p[i] == marker) {
                p += i;
                n -= i;
                found = true;
                break;
            }
        }
        if (!found) return out;
    }
    if (n < 7) return out;
    n = std::min<std::size_t>(n, kDoorMaxAdvBytes);

    out.device_id = read_le32(p + 1);
    std::size_t i = 5;                   // 跳过 marker(1) + device id(4)
    bool parsed_any = false;
    while (i + 1 < n) {
        const std::uint8_t key = p[i];
        if (key == 0x00) {
            // 现场 BWD1 的厂商数据区开头带一个 0x00 前导填充：
            //   07 FF | FF FF FF 06 | 00 | 63 64 00 | 00.. | 计数
            // 只有当已经解析出至少一个 Key 之后，0x00 才是 25 字节报文的零填充尾部。
            // 尚未解析到任何 Key 时不能提前 break，否则整帧会被误判为格式不符。
            if (parsed_any) break;
            ++i;
            continue;
        }
        if (key == kDoorKeyDoor ||
            (fmt.vendor_tolerant && key == kDoorKeyDoorLegacy &&
             (p[i + 1] == 0x00 || p[i + 1] == 0x01))) {
            // PDF 标 Key 0x08；现场 BWD1 固件实测发 0x06，Value 同为 1B 布尔。
            // 仅在已确认的厂商帧里识别，且 Value 必须是 0/1，绝不把任意 0x06
            // 字节当成门磁状态（其它设备的厂商数据不生成该候选）。
            out.has_door = true;
            out.door = p[i + 1] ? DoorState::Ajar : DoorState::Closed;
            parsed_any = true;
            i += 2;
        } else if (key == kDoorKeyBattery) {
            if (i + 2 >= n) break;
            const int pct = static_cast<int>(p[i + 1]) |
                            (static_cast<int>(p[i + 2]) << 8);
            if (pct >= 0 && pct <= 100) {
                out.has_battery = true;
                out.battery_pct = pct;
                parsed_any = true;
            }
            i += 3;
        } else {
            // 现场 BWD1 固件的 <Key,Value> 报文前有 FF FF FF 06 00 之类头部字节，
            // 与 PDF 的「byte5 起即数据报文」略有出入。仅在调用方已用 company id
            // 确认是厂商帧、且尚未解析到任何 Key 时，允许在有限窗口内跳过这些
            // 前导字节寻找第一个合法 Key；一旦解析出字段或超出窗口立即停止。
            if (fmt.vendor_tolerant && !parsed_any &&
                (i - 5) < kDoorVendorHeaderSkipLimit) {
                ++i;
                continue;
            }
            // 未知 Key 无从判定 Value 长度；保留已解析字段并停止，绝不猜测后续布局。
            break;
        }
    }

    if (out.has_door || out.has_battery) {
        out.valid = true;
        out.reason.clear();
    }
    return out;
}

DoorFrame parse_door_frame(const std::vector<std::uint8_t>& data,
                           const DoorFrameFormat& fmt) {
    return parse_door_frame(data.empty() ? nullptr : data.data(), data.size(), fmt);
}

DoorFleetSummary summarize_door_sensors(const std::vector<DoorSensorState>& states,
                                        long long now_ms, int stale_ms, int battery_low_pct) {
    DoorFleetSummary s;
    s.total = static_cast<int>(states.size());
    if (states.empty()) return s;         // level=Unavailable, value="未接入"

    int offline = 0;
    for (const auto& d : states) {
        const bool fresh = d.last_seen_ms > 0 &&
                           (now_ms - d.last_seen_ms) <= static_cast<long long>(stale_ms);
        if (!fresh) { ++offline; continue; }
        ++s.online;
        // 契约 §10：未核验 / 带 reason token 的设备不能按「正常」或「报警」定性，
        // 只能计入 unknown，最终汇总为 Unavailable。
        if (!d.verified || !d.reason.empty()) { ++s.unknown; continue; }
        if (d.door == DoorState::Ajar) ++s.ajar;
        else if (d.door == DoorState::Closed) { /* 正常 */ }
        else ++s.unknown;
        if (d.has_battery && d.battery_pct >= 0 && d.battery_pct <= battery_low_pct)
            ++s.low_battery;
    }

    if (s.ajar > 0) {
        s.level = StatusLevel::Alarm;
        s.value = std::to_string(s.ajar) + " 路未关严";
        s.alarm_text = "车门未关严（" + std::to_string(s.ajar) + " 路）";
    } else if (offline > 0 || s.unknown > 0) {
        // 契约 §6/§9：unknown / 不可用不能按正常默认。
        s.level = StatusLevel::Unavailable;
        s.value = std::to_string(s.online) + "/" + std::to_string(s.total) + " 在线";
    } else if (s.low_battery > 0) {
        s.level = StatusLevel::Warning;
        s.value = std::to_string(s.low_battery) + " 路低电量";
    } else {
        s.level = StatusLevel::Ok;
        s.value = "全关";
    }
    return s;
}

const char* door_state_label(DoorState s) {
    switch (s) {
        case DoorState::Closed: return "已关闭";
        case DoorState::Ajar: return "未关严";
        case DoorState::Unknown: break;
    }
    return "未知";
}

const char* door_state_token(DoorState s) {
    switch (s) {
        case DoorState::Closed: return "closed";
        case DoorState::Ajar: return "ajar";
        case DoorState::Unknown: break;
    }
    return "unknown";
}

}  // namespace core
}  // namespace railhmi
