#pragma once

// railhmi-core: 其它职责可用性语义（视觉推理 ITEM-010：pantograph / wheel_rail / cab_door）。
//
// 目标（VISION_DESIGN §1、INTERFACE_CONTRACT §6）：
//   * 无专用模型证据的职责必须返回 `unavailable` + 稳定 reason（`PROFILE_UNSUPPORTED` /
//     `MODEL_MISMATCH` 等），语义状态为 **unknown**；
//   * `unknown` **绝不**映射为 `normal`（受电弓/车门的“正常”只能来自已声明并核验的模型标签）；
//   * `ai_enabled=true` 或配置里登记了模型名**都不构成识别能力**——只有提供能力声明
//     （输入/输出规格 + 标签→状态映射）并与实际 `rknn_query` 描述逐项匹配后才 `ready`；
//   * 状态词表按职责校验（受电弓：not_raised/normal/abnormal；车门：closed/ajar；
//     轮轨：normal/abnormal），越界映射即 `MODEL_MISMATCH`。
//
// 纯标准库；不含 Qt/GStreamer/rknn；不读设备、不猜标签。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/config.h"                     // AiProfile
#include "railhmi/core/rules.h"                      // Detection / PantographState / DoorState
#include "railhmi/core/vision_model_capability.h"    // ModelDescriptor / TensorDesc / reason token

namespace railhmi {
namespace core {

// 本轮无专用模型证据的职责。
enum class DutyKind { Pantograph = 0, WheelRail, CabDoor };
const char* to_string(DutyKind v);
bool parse_duty_kind(const std::string& s, DutyKind* out);

// profile <-> duty（front/none 不是 duty）。
bool duty_of_profile(AiProfile p, DutyKind* out);
AiProfile profile_of_duty(DutyKind d);

// 职责语义状态。`Unknown` 是**缺省值**，不是“正常”。
enum class DutyState { Unknown = 0, NotRaised, Normal, Abnormal, Closed, Ajar };
const char* to_string(DutyState v);
bool parse_duty_state(const std::string& s, DutyState* out);
const char* duty_state_display_zh(DutyState v);   // UI 文案：未知/未升弓/正常/异常/关闭/异常开启

// 状态词表按职责校验（未知词/越界词一律 false）。
bool duty_state_allowed(DutyKind duty, DutyState state);
// 与 core::rules 的既有状态枚举互转（仅当该职责允许该状态）。
bool to_pantograph_state(DutyState s, PantographState* out);
bool to_door_state(DutyState s, DoorState* out);

// 职责可用性稳定 reason（脱敏，可被探针/UI/事件消费）。
namespace duty_reason {
inline const char* kReady() { return "READY"; }
inline const char* kProfileUnsupported() { return "PROFILE_UNSUPPORTED"; }  // 未声明专用能力
inline const char* kModelMissing() { return "MODEL_MISSING"; }
inline const char* kModelMismatch() { return "MODEL_MISMATCH"; }
inline const char* kNotReady() { return "NOT_READY"; }
}  // namespace duty_reason

// 模型标签 -> 职责状态（必须显式声明；不得由实现猜）。
struct DutyLabelMapping {
    std::string label;
    DutyState state = DutyState::Unknown;
};

// 声明能力（来源：配置/CLI，**不写死模型名**）。声明本身不等于能力，须与描述核验。
struct DutyDeclaration {
    DutyKind duty = DutyKind::Pantograph;
    std::string declared_model_name;   // 与 app.ai.models 的 name 对齐（身份，不是能力）
    std::string declared_by;           // 来源说明（如 "cli:--duty-declare"），证据用
    int input_count = 1;
    int input_c = 3;
    int input_h = 0;
    int input_w = 0;
    int output_count = 0;              // 0 = 不校验；>0 时必须等于标签数（类分数输出）
    bool require_quantized_input = true;
    std::vector<DutyLabelMapping> labels;
    float score_min = 0.5f;            // 状态判定阈值（可由配置覆盖）

    // 基本完整性：模型名/输入形状/标签表；不完整 -> MODEL_MISMATCH。
    // 支持的职责解码布局：每个输出 1 个元素（类别分数），标签数与输出数一致。
    bool complete() const;
};

// 求值输入：配置登记（非能力）+ 声明（能力声明）+ 实际描述（rknn_query 或夹具）。
struct DutyEvidenceInput {
    DutyKind duty = DutyKind::Pantograph;
    bool registered_in_config = false;
    std::string registered_model_name;
    bool declaration_provided = false;
    DutyDeclaration declaration;
    bool descriptor_present = false;
    ModelDescriptor descriptor;
};

struct DutyAvailability {
    DutyKind duty = DutyKind::Pantograph;
    bool available = false;
    std::string reason = duty_reason::kProfileUnsupported();
    std::string detail;
    DutyState state = DutyState::Unknown;   // 不可用时恒 unknown
    std::vector<std::string> matched;
    std::vector<std::string> mismatched;
    std::string evidence_source = "none";   // none | declaration+descriptor
    bool config_registered = false;         // 仅记录：登记不构成能力
    std::string registered_model_name;
    std::string declared_model_name;

    bool semantics_unknown() const { return state == DutyState::Unknown; }
};

// 求值：无声明 -> PROFILE_UNSUPPORTED；有声明无描述 -> MODEL_MISSING；
// 描述失败/结构或标签表不匹配 -> 对应 reason/MODEL_MISMATCH；全通过 -> READY（state 仍为 unknown）。
DutyAvailability evaluate_duty(const DutyEvidenceInput& in);

// 一次推理的候选标签（来自模型输出，调用方已解码）。
struct DutyPrediction {
    std::string label;
    float score = 0.0f;
};

struct DutyLabelResult {
    DutyState state = DutyState::Unknown;   // 未过阈值/未声明标签 -> unknown（绝不默认 normal）
    std::string used_label;
    float used_score = 0.0f;
    long long considered = 0;
    long long blocked_unknown_label = 0;
    long long below_threshold = 0;
    bool declaration_present = false;
};

// 标签 -> 状态映射。threshold_override > 0 时覆盖声明阈值。
// 仅“已声明的标签 + 分数达阈值”才产生非 unknown 状态；多标签时取最高分。
DutyLabelResult map_duty_prediction(const DutyDeclaration& decl,
                                    const std::vector<DutyPrediction>& preds,
                                    float threshold_override = -1.0f);

struct DutyAdapterStats {
    long long evaluations = 0;
    long long predictions = 0;
    long long unknown_results = 0;        // 映射结果为 unknown 的次数
    long long rejected_unavailable = 0;   // 不可用时被拒绝接受的推理结果
    long long blocked_unknown_label = 0;
    long long below_threshold = 0;
};

// 职责适配入口（每职责一个；单线程使用）。
class DutyAdapter {
public:
    DutyAdapter(DutyKind duty, std::string camera_id = "");

    void set_config_registration(bool registered, const std::string& model_name);
    void set_declaration(const DutyDeclaration& d);   // declaration_provided = true
    void clear_declaration();
    void set_descriptor(const ModelDescriptor& d);    // descriptor_present = true
    void clear_descriptor();

    // 求值（幂等）。
    DutyAvailability evaluate();

    // 推理结果 -> 语义。仅 `available` 时接受；否则保持 unknown 并计 rejected_unavailable。
    DutyAvailability apply(const std::vector<DutyPrediction>& preds, float threshold_override = -1.0f);

    const DutyAvailability& availability() const { return availability_; }
    DutyState state() const { return availability_.state; }
    bool semantics_unknown() const { return availability_.state == DutyState::Unknown; }
    const DutyAdapterStats& stats() const { return stats_; }
    DutyKind duty() const { return duty_; }
    const std::string& camera_id() const { return camera_id_; }

    // 仅当 available 且 state != unknown 时生成 Detection（状态字段按职责填充）。
    bool to_detection(long long ts_ms, Detection* out) const;

private:
    DutyKind duty_;
    std::string camera_id_;
    DutyEvidenceInput input_;
    DutyAvailability availability_;
    DutyAdapterStats stats_;
};

// 三职责整体视图（P5/探针/证据）。
struct DutyStatusItem {
    DutyKind duty = DutyKind::Pantograph;
    AiProfile profile = AiProfile::None;
    std::string camera_id;
    DutyAvailability availability;
    DutyAdapterStats stats;
};

std::string duty_availability_json(const DutyAvailability& a);
std::string duty_status_json(const std::vector<DutyStatusItem>& items);

}  // namespace core
}  // namespace railhmi
