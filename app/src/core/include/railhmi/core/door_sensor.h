#pragma once

// railhmi-core: BLE 门磁广播报文解析与机队状态汇总（纯逻辑，无 Qt/DBus/BlueZ 依赖）。
//
// 协议事实来自仓库根目录两份厂商资料：
//   ustone-BLE门磁感应传感器-开发向导.pdf  (USTONE-HA-BSM1-DG v0.1, 2024/10/30)
//   ustone-BLE门磁感应传感器-使用说明.pdf  (USTONE-HA-BSM1-UM v0.1, 产品型号 BWD1)
//
// 广播（蓝牙 4.x legacy advertising，31 字节）：
//   byte 0        : 0x07 固定标识
//   byte 1..4     : 设备 ID，小端序
//   byte 5..30    : 25 字节数据报文，<Key, Value> 结构
//     Key 0x08 门磁数据：Value 1B 布尔值。感应不到磁铁=1（门未关严），否则=0（关闭），状态变化立即上报
//     Key 0x63 电池数据：Value 2B 小端无符号整型/百分比，约 5 分钟更新一次
//   注：现场固件实测门磁字段名为 0x06（非文档 0x08），且每次广播都携带、
//       并非只在状态变化时上报；见 kDoorKeyDoorLegacy 与下方现场帧说明。
//
// 文档正文写「每个 Key 占 1Byte，Value 占 1Byte」，但表格中电池 Value 为 2B；
// 这里按表格的 Key→长度表解析，遇到未知 Key 立即停止（保留已解析字段，不臆测长度）。
// 广播帧在 BlueZ 里可能位于 ManufacturerData / ServiceData，且可能带厂商 ID 前缀，
// 因此解析允许在前 4 字节内搜索 0x07 标识（见 DoorFrameFormat::payload_offset）。
//
// 现场实测（RK3588 btmon，BWD1，MAC C0:6D:9A:AC:C3:87，广播名 T1）：
//   31B ADV data 里的厂商 AD structure 为
//     1A FF | 07 FF | FF FF FF 06 00 63 62 00 ... 00 <计数>
//                    ↑ PDF 的 0x07 帧首被 BlueZ 当成 company id 0xFF07 的低字节剥掉
//   即 ManufacturerData 的 company=0xFF07，value 为 23B；
//   把 company 小端 07 FF 拼回 value 后，<Key,Value> 报文前还有 FF/FF/FF/06/00
//   几个头部字节，与文档「byte5 起即数据报文」略有出入（固件差异）。
//   因此 DoorFrameFormat::vendor_tolerant 仅对这种已确认的厂商帧，
//   允许在有限窗口内跳过未知前导字节定位第一个合法 Key，绝不无限猜测。
//
// 稳定 reason token 遵循 docs/interfaces/INTERFACE_CONTRACT.md §10；
// 未知/过期状态一律不可用，绝不按「正常」默认。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/rules.h"
#include "railhmi/core/status.h"

namespace railhmi {
namespace core {

inline constexpr const char* kDoorReasonUnconfigured = "UNCONFIGURED";  // 配置未启用
inline constexpr const char* kDoorReasonNotReady     = "NOT_READY";      // 无蓝牙适配器/系统总线
inline constexpr const char* kDoorReasonSourceError  = "SOURCE_ERROR";   // BlueZ DBus 调用失败
inline constexpr const char* kDoorReasonUnsupported  = "UNSUPPORTED";    // 广播帧格式不符
inline constexpr const char* kDoorReasonUnverified   = "UNVERIFIED";     // 未收到可核验的广播帧
inline constexpr const char* kDoorReasonStale        = "STALE";          // 超过 stale_ms 未收到广播

constexpr int kDoorKeyBattery = 0x63;
constexpr int kDoorKeyDoor    = 0x08;        // PDF 标注的门磁 Key
// 现场 BWD1 固件实测（RK3588 btmon，两次磁铁状态对比）：门磁字段名是 0x06，
// Value 同为 1B 布尔（0=感应到磁铁/关闭，1=感应不到/未关严），且每次广播都携带。
// 闭合帧 ...06 00 63 62 00...，分开帧 ...06 01 63 62 00...。PDF 的 0x08 与
// 「状态变化立即上报」均与现场不符，故保留 0x08 兼容文档、另按实测识别 0x06；
// 0x06 只在已确认的厂商帧（vendor_tolerant）且 Value 为 0/1 时生效，避免误判。
constexpr int kDoorKeyDoorLegacy = 0x06;
constexpr int kDoorMarker     = 0x07;
constexpr int kDoorVendorCompany = 0xFF07;   // 现场 BWD1：marker 0x07 落在 company id 低字节
constexpr std::size_t kDoorMaxAdvBytes = 31;
constexpr std::size_t kDoorVendorHeaderSkipLimit = 6;  // 宽松模式最多跳过的报文头部字节

struct DoorFrameFormat {
    int marker = kDoorMarker;   // 帧首固定标识
    int payload_offset = 0;     // 数据在 AD 载荷中的起始偏移（厂商 ID 前缀时用）
    // 仅对已由 company id 确认的厂商帧置 true：允许跳过 <Key,Value> 前的
    // 少量固件头部字节。默认 false 保持 PDF 严格布局与「未知 Key 立即停止」语义。
    bool vendor_tolerant = false;
};

struct DoorFrame {
    bool valid = false;
    std::string reason;                       // 失败时为稳定 token；成功时为空
    std::uint32_t device_id = 0;             // 广播里的设备 ID（小端序）
    bool has_door = false;
    DoorState door = DoorState::Unknown;      // 1=Ajar(感应不到磁铁)，0=Closed
    bool has_battery = false;
    int battery_pct = -1;                    // 0..100；-1=本帧未携带
};

// 解析一段广播字节（不做设备过滤）。data 可以比 31 字节短（ManufacturerData 值本身更短）。
DoorFrame parse_door_frame(const std::uint8_t* data, std::size_t size, const DoorFrameFormat& fmt);
DoorFrame parse_door_frame(const std::vector<std::uint8_t>& data, const DoorFrameFormat& fmt);

struct DoorSensorState {
    std::string id;                           // 配置登记 id；自动发现时为 AUTO-<MAC>
    std::string name;
    std::string mac;
    std::uint32_t device_id = 0;
    DoorState door = DoorState::Unknown;
    bool has_battery = false;
    int battery_pct = -1;
    int rssi = 0;
    long long last_seen_ms = 0;               // 单调毫秒；0=从未收到广播
    bool online = false;
    bool verified = false;                    // 已登记 MAC 或已解析到合法厂商帧
    std::string source;                       // registered / discovered / simulation
    std::string reason;                       // 稳定 token 或空
};

struct DoorFleetSummary {
    StatusLevel level = StatusLevel::Unavailable;
    std::string value = "未接入";
    std::string alarm_text;                   // 非空时 P1 当前报警条显示该文案
    int total = 0;
    int online = 0;
    int ajar = 0;
    int unknown = 0;
    int low_battery = 0;
};

// 汇总：任一在线设备门未关严 -> Alarm；离线/未知 -> Unavailable（不映射到正常）；
// 低电量 -> Warning；全部在线且关闭 -> Ok。
DoorFleetSummary summarize_door_sensors(const std::vector<DoorSensorState>& states,
                                        long long now_ms, int stale_ms, int battery_low_pct);

const char* door_state_label(DoorState s);    // 未知 / 已关闭 / 未关严
const char* door_state_token(DoorState s);    // unknown / closed / ajar

}  // namespace core
}  // namespace railhmi
