#pragma once

// railhmi-core: 检测职责与报警规则/事件层（ITEM-006）。
// 按 ai_profile 分派判定（front 危险区 ROI / pantograph 三态持续确认 / wheel_rail 标定基准 /
// cab_door 车门 ROI / none 不检测）；多帧确认、跟踪 ID 与抑制窗口；
// 报警级别严格按 PRD §8.2 映射；确认只改 ack_state 不删除事件。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/相机。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/config.h"

namespace railhmi {
namespace core {

enum class AlarmLevel { L1 = 1, L2 = 2, L3 = 3 };
const char* to_string(AlarmLevel v);

enum class AlarmType {
    PersonOnRail,        // 前方轨道人员
    FrontObstacle,       // 前方障碍物
    WheelDerail,         // 轮轨明显异常/疑似脱轨
    PantographAbnormal,  // 受电弓异常（升弓不到位）
    DoorAjar,            // 车门未关严
    FrontCameraDevice,   // 前方主摄离线/冻结（设备）
    SideCameraDevice,    // 侧视摄像头离线（设备）
    Device,              // 其它设备故障
};
const char* to_string(AlarmType v);

enum class AckState { Unacked, Acked };
const char* to_string(AckState v);

enum class PantographState { Unknown, NotRaised, Normal, Abnormal };
const char* to_string(PantographState v);
enum class DoorState { Unknown, Closed, Ajar };
const char* to_string(DoorState v);

// 设备事件分类（用于级别映射：前方主摄=2、侧视=3、其它=2）。
enum class DeviceCategory { FrontPrimary, Side, Other };
const char* to_string(DeviceCategory v);

struct Detection {
    std::string camera_id;
    AiProfile profile = AiProfile::None;
    long long t_ms = 0;
    int track_id = 0;
    bool in_roi = false;              // ROI/危险区/标定基准命中（由调用方按 roi.yaml 计算）
    double confidence = 0.0;
    double distance_m = -1.0;
    std::string label;                // "person" / "obstacle" / ...
    PantographState pantograph = PantographState::Unknown;
    DoorState door = DoorState::Unknown;
    bool wheel_abnormal = false;
};

struct AlarmEvent {
    long long id = 0;
    AlarmLevel level = AlarmLevel::L3;
    AlarmType type = AlarmType::Device;
    std::string source_cam;
    long long ts_ms = 0;              // 事件产生时间（core 用单调毫秒；wall/持久化由上层）
    long long last_update_ms = 0;     // 抑制窗口内更新同一事件时刷新
    int track_id = 0;
    double confidence = 0.0;
    double distance_m = -1.0;
    std::string description;
    AckState ack_state = AckState::Unacked;
    bool record_available = false;
    std::string snapshot_ref;
};

struct RuleConfig {
    int confirm_frames = 3;
    int confirm_window_ms = 500;
    double confidence_min = 0.5;
    int suppress_window_ms = 3000;
};

// PRD §8.2 级别映射（不得被 UI 改写）。
AlarmLevel map_level(AlarmType t);
// 设备事件级别：前方主摄=2、侧视=3、其它=2。
AlarmLevel map_device_level(DeviceCategory c);
// 由相机位置（cameras.yaml position）推导设备类别：primary->FrontPrimary、
// left/right->Side、其余(roof/underfloor/cab)->Other。使设备级别映射不依赖调用方隐式选择。
DeviceCategory device_category_from_position(CameraPosition p);

// ROI 命中：归一化矩形 [0,1]。
bool roi_contains(const RoiConfig& r, double nx, double ny);
const RoiConfig* find_roi(const std::vector<RoiConfig>& rois,
                          const std::string& camera, const std::string& name);

class RuleEngine {
public:
    explicit RuleEngine(RuleConfig cfg);

    // 送入一帧检测结果；返回**本次新产生**的事件（被抑制/更新的不重复返回）。
    std::vector<AlarmEvent> on_detection(const Detection& d);

    // 送入设备事件（来自流状态机的升级/冻结）；返回新事件。
    std::vector<AlarmEvent> on_device_event(const std::string& camera_id, DeviceCategory cat,
                                            long long t_ms, const std::string& detail);

    // 确认事件：只改 ack_state，不删除事件。返回 false 表示 id 不存在。
    bool ack(long long id);

    const std::vector<AlarmEvent>& events() const { return events_; }
    const AlarmEvent* find(long long id) const;
    std::size_t unacked_count() const;
    RuleConfig config() const { return cfg_; }

private:
    std::string make_key(const std::string& cam, AlarmType type, int track_id) const;
    void reset_camera_confirm(const std::string& cam);
    AlarmEvent make_event(const Detection& d, AlarmType type, long long t_ms);
    void update_event(AlarmEvent* e, const Detection& d);

    RuleConfig cfg_;
    long long next_id_ = 1;

    struct Confirm {
        int count = 0;
        long long first_ms = 0;
        long long last_ms = 0;
    };
    std::map<std::string, Confirm> confirm_;       // key -> 多帧确认累计
    std::map<std::string, long long> last_emit_ms_; // key -> 最近一次产生事件时间（抑制）
    std::map<std::string, long long> key_event_id_; // key -> 抑制窗口对应事件 id
    std::vector<AlarmEvent> events_;
};

}  // namespace core
}  // namespace railhmi
