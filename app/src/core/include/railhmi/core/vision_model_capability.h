#pragma once

// railhmi-core: 视觉模型能力描述与就绪核验（视觉推理 ITEM-001）。
//
// 纯逻辑：不加载模型、不依赖 rknn/Qt/GStreamer/相机。真实 `rknn_query` 由 target 侧
// `railhmi_media` 的 model probe 读取后产出 `ModelDescriptor` 文本，交本模块**逐项核验**；
// 只有结构/形状/布局/量化全部匹配声明的规格，才置 `ready`。
//
// 硬约束（InterfaceContract §6 / VISION_DESIGN §0.3）：
//   * 模型路径与名称来自配置解析（`app.ai.models` + 可配 models 目录），不在代码写死；
//   * 文件存在不等于 ready；不得只按文件名判定能力；
//   * 模型缺失/张量不匹配/运行时不可用各自返回稳定 reason，不静默用默认值遮蔽。
//
// 仅依赖 C++ 标准库。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/config.h"

namespace railhmi {
namespace core {

// 稳定 reason token（脱敏，可被探针/UI/事件消费；不含路径/凭据）。
namespace vision_reason {
inline const char* kReady() { return "READY"; }
inline const char* kModelMissing() { return "MODEL_MISSING"; }
inline const char* kModelLoadFailed() { return "MODEL_LOAD_FAILED"; }
inline const char* kModelMismatch() { return "MODEL_MISMATCH"; }
inline const char* kRknnRuntimeUnavailable() { return "RKNN_RUNTIME_UNAVAILABLE"; }
inline const char* kProfileUnsupported() { return "PROFILE_UNSUPPORTED"; }
inline const char* kNotReady() { return "NOT_READY"; }
}  // namespace vision_reason

// 张量数据类型（映射 rknn_tensor_type；未知值显式 Unknown）。
enum class TensorType { Unknown = 0, Float32, Float16, Int8, Uint8, Int16, Uint16, Int32, Uint32, Int64, Bool };
// 张量布局（映射 rknn_tensor_format）。
enum class TensorFormat { Unknown = 0, Nchw, Nhwc, Nc1hwc2, Undefined };
// 量化类型（映射 rknn_tensor_qnt_type）。
enum class QuantType { None = 0, Dfp, Affine, LayerNorm, Unsupported };
enum class TensorDir { Input = 0, Output };

const char* to_string(TensorType v);
const char* to_string(TensorFormat v);
const char* to_string(QuantType v);
bool parse_tensor_type(const std::string& s, TensorType* out);
bool parse_tensor_format(const std::string& s, TensorFormat* out);
bool parse_quant_type(const std::string& s, QuantType* out);

struct TensorDesc {
    TensorDir dir = TensorDir::Input;
    uint32_t index = 0;
    std::string name;
    uint32_t n_dims = 0;
    std::vector<uint32_t> dims;        // 长度 = n_dims（最多记录 8 维，超出部分只累计）
    uint32_t extra_dims = 0;           // n_dims > dims.size() 时的额外维数（仅记录，不参与匹配）
    TensorType type = TensorType::Unknown;
    TensorFormat fmt = TensorFormat::Unknown;
    QuantType quant = QuantType::None;
    int32_t zp = 0;
    float scale = 0.0f;
    uint32_t n_elems = 0;
    uint32_t size = 0;
};

// 由 target 侧 `rknn_query` 读取的实际模型描述（也可由夹具构造）。
struct ModelDescriptor {
    std::string path;                  // 解析自配置/CLI，不写死
    std::string file_name;             // 仅身份展示，不作为 ready 依据
    bool file_present = false;
    std::string load_status = "ok";    // "ok" 或稳定 token（MODEL_MISSING/MODEL_LOAD_FAILED/RKNN_RUNTIME_UNAVAILABLE）
    std::string runtime_api_version;   // rknn sdk api 版本（或 unknown(...)）
    std::string runtime_drv_version;   // rknn 驱动版本（或 unknown(...)）
    std::vector<TensorDesc> inputs;
    std::vector<TensorDesc> outputs;
};

// 声明/推断的模型种类（结构核验，不看文件名判定 ready）。
enum class VisionModelKind { Unknown = 0, YoloV5Detect, YoloV8Seg };
const char* to_string(VisionModelKind v);

// 期望规格（由声明名 + profile 选择；判定仍以结构核验为准）。
struct ModelSpec {
    VisionModelKind kind = VisionModelKind::Unknown;
    std::string declared_name;
    int input_count = -1;              // -1 = 不校验
    int output_count = -1;
    int in_c = -1;
    int in_h = -1;
    int in_w = -1;
    bool in_must_be_quantized = false; // INT8/UINT8 + Affine
    int detect_classes = -1;           // YOLOv5: 3*(5+classes) 的类别数（-1 = 不校验）
    bool seg_require_proto = false;    // YOLOv8-seg: 需要 proto 张量
    int seg_proto_channels = 0;
    int seg_proto_h = 0;
    int seg_proto_w = 0;
    int seg_class_count = -1;          // YOLOv8-seg 类别数（-1 = 不校验）
};

// 按声明名 + profile 取规格。声明名未知时返回 kind=Unknown（调用方据此给 MODEL_MISMATCH）。
ModelSpec spec_for_declared(const std::string& declared_name, AiProfile profile);

struct CapabilityCheck {
    bool ready = false;
    std::string reason;                // 稳定 token
    VisionModelKind kind = VisionModelKind::Unknown;
    std::string detail;                // 已脱敏的一句话说明（不含凭据）
    std::vector<std::string> matched;  // 逐项通过
    std::vector<std::string> mismatched;
};

// 结构识别（不看文件名）：返回推断的种类；detail 记录依据。
VisionModelKind identify_kind(const ModelDescriptor& d, std::string* detail = nullptr);

// 核验：描述 vs 规格。ready 仅当全部通过。
CapabilityCheck check_capability(const ModelDescriptor& d, const ModelSpec& spec);

// profile 对某种类是否具备模型证据（本轮：front 需 seg+detect；其余无专用证据）。
bool profile_accepts_kind(AiProfile p, VisionModelKind kind);

// profile 所需的模型种类集合（本轮证据）。
struct ProfileRequirement {
    AiProfile profile = AiProfile::None;
    std::vector<VisionModelKind> required_kinds;
    bool has_model_evidence = false;
};
ProfileRequirement requirement_for_profile(AiProfile p);

// 配置中的登记项（app.ai.models）→ 声明名（name/version 分离；version 仅作身份展示）。
struct DeclaredModel {
    AiProfile profile = AiProfile::None;
    std::string name;
    std::string version;
};
std::vector<DeclaredModel> declared_models(const AiConfig& ai);

// 模型文件路径解析：<models_dir>/<name>.rknn。目录与名称均来自配置/CLI，不含设备地址。
std::string resolve_model_path(const std::string& models_dir, const std::string& name);

// 文本往返（target 侧 probe 输出、core 侧解析）：键值行，便于归档与 grep。
std::string model_descriptor_text(const ModelDescriptor& d);
bool parse_model_descriptor_text(const std::string& text, ModelDescriptor* out, std::string* err);

// JSON（工具/证据；不引第三方库）。
std::string model_descriptor_json(const ModelDescriptor& d);
std::string capability_check_json(const CapabilityCheck& c, const ModelDescriptor& d);

// 每 profile 能力状态（探针输出；不含相机地址/凭据）。
struct ModelAvailability {
    AiProfile profile = AiProfile::None;
    std::string model_name;
    std::string model_version;
    std::string path;
    bool file_present = false;
    bool available = false;
    std::string reason;
    std::string detail;
};
std::string model_availability_json(const std::vector<ModelAvailability>& items);

}  // namespace core
}  // namespace railhmi
