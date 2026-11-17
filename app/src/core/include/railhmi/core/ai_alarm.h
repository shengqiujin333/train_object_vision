#pragma once

// railhmi-core: AI 推理结果 -> 报警判定（2026-11-17 现场需求）。
//
// 只做「检测框 -> 是否报警」的纯逻辑，不依赖 Qt/媒体层：
//   * 车前方（front）：envelope 里的每个框带 in_roi 标志，该标志由视觉层的**同帧两阶段**
//     （yolov8s-seg 轨道掩码 ∩ yolov5s 人/物检测）计算 —— 只有落在轨道区域内的目标
//     才置 in_roi=1。因此「轮轨识别与人/物识别交叉重叠后报警」在 core 侧的判据就是 in_roi。
//   * 受电弓（pantograph）/ 轮轨（wheel_rail）：当前**没有专用模型**，现场要求临时复用与
//     车前方相同的检测模型；此时任一置信度达阈值的检测框即视为异常并报警。这只是链路验证，
//     不代表已具备受电弓三态/轮轨几何判定能力（文档与 UI 必须如实标注）。
//   * 车门（cab_door）由 BLE 门磁负责；none 不检测 —— 两者都不产生 AI 报警。
//
// 多帧确认与解除保持（避免单帧误检刷屏、避免目标短暂丢失时红框闪烁）在此实现，
// 逐路独立状态由 `AiAlarmTracker` 持有。

#include <map>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_inference_envelope.h"

namespace railhmi {
namespace core {

struct AiAlarmConfig {
    int confirm_frames = 2;          // 连续满足条件的推理轮次（>=1）
    long long clear_hold_ms = 1500;  // 目标消失后保持多久再解除（防止红框闪烁）
    double confidence_min = 0.5;     // 与 thresholds.confidence_min 同源
};

struct AiAlarmState {
    bool active = false;
    AlarmLevel level = AlarmLevel::L3;
    AlarmType type = AlarmType::Device;
    int confirm = 0;
    double confidence = -1.0;
    int boxes = 0;
    long long since_ms = 0;       // 本轮报警首次达成确认的时刻
    long long last_hit_ms = -1;   // 最近一次满足条件的时刻
    std::string description;
};

// 单帧条件判定（纯函数，供 UI 桥接与单测共用）。
// 返回 hit=true 时同时给出级别与类型；未命中时 level 无意义（不要把 hit=false 当正常状态发布）。
struct AiAlarmCandidate {
    bool hit = false;
    AlarmLevel level = AlarmLevel::L3;
    AlarmType type = AlarmType::Device;
    double confidence = -1.0;
    int boxes = 0;
};
AiAlarmCandidate evaluate_ai_alarm(AiProfile profile, const std::vector<EnvelopeBox>& boxes,
                                   double confidence_min);

// 事件类型 token（与既有事件表一致：person_on_rail / front_obstacle / wheel_derail /
// pantograph_abnormal；其它返回 "device"）。
const char* ai_alarm_type_token(AlarmType t);
const char* ai_alarm_description(AiProfile profile, AlarmType t);

// 逐路状态机：前 N 帧确认、解除保持、跃迁只在状态变化时上报一次。
class AiAlarmTracker {
public:
    explicit AiAlarmTracker(AiAlarmConfig cfg = AiAlarmConfig());

    // 送入该路**本轮**检测框。返回 true 表示发生跃迁（normal->alarm 或 alarm->normal）。
    // out 始终被写入该路最新状态（无跃迁时也更新 confidence/boxes）。
    bool update(const std::string& camera_id, AiProfile profile,
                const std::vector<EnvelopeBox>& boxes, long long now_ms, AiAlarmState* out);

    // 该路当前状态；未知相机返回 active=false 的默认值。
    AiAlarmState state_of(const std::string& camera_id) const;
    bool active(const std::string& camera_id) const;
    std::size_t active_count() const;

    // 推理不可用/停止时清空全部路（红框必须熄灭，不能留上一轮的火）。
    void clear();

private:
    AiAlarmConfig cfg_;
    std::map<std::string, AiAlarmState> states_;
};

}  // namespace core
}  // namespace railhmi
