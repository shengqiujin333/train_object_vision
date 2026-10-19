#pragma once

// railhmi-core: 视觉推理输入预处理（视觉推理 ITEM-002 的纯逻辑部分）。
//
// 职责：把 8-bit 定长像素（BGR888/RGB888 或参考 NV12）按 letterbox 等比缩放到模型输入尺寸，
// 显式完成 BGR→RGB，按 (value-mean)/std 归一化，并按输入张量 dtype/scale/zero_point 量化写值。
//
// 边界（InterfaceContract §5/§6、VISION_DESIGN §7.1）：
//   * 只处理像素缓冲；不接触 FrameRef/FrameLease，也不从元数据推导像素（租约消费在 media 层）。
//   * stride/尺寸/格式非法一律返回显式 reason，**不静默丢帧、不越界读**。
//   * 通道顺序只在预处理内显式转换，不用格式名掩盖颠倒。
//   * NV12 的主链路转换由板端 librga 完成（media 层）；此处的 NV12 参考实现用于夹具、
//     一致性校验与无 RGA 环境，不冒充目标性能。
//
// 仅依赖 C++ 标准库。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/vision_model_capability.h"  // TensorType

namespace railhmi {
namespace core {

// 稳定 reason token（脱敏；可被探针/诊断/计数消费）。
namespace preprocess_reason {
inline const char* kOk() { return "PREPARED"; }
inline const char* kNotReady() { return "NOT_READY"; }
inline const char* kUnmapped() { return "PRE_UNMAPPED"; }
inline const char* kNullBuffer() { return "PRE_NULL_BUFFER"; }
inline const char* kInvalidDimensions() { return "PRE_INVALID_DIMENSIONS"; }
inline const char* kUnsupportedFormat() { return "PRE_UNSUPPORTED_FORMAT"; }
inline const char* kStrideTooSmall() { return "PRE_STRIDE_TOO_SMALL"; }
inline const char* kBadConfig() { return "PRE_BAD_CONFIG"; }
inline const char* kConversionFailed() { return "PRE_CONVERSION_FAILED"; }
}  // namespace preprocess_reason

// letterbox 参数（等比缩放 + 居中补边）。
struct LetterboxParams {
    int src_w = 0;
    int src_h = 0;
    int dst_w = 0;
    int dst_h = 0;
    double scale = 1.0;   // src -> resized 的等比因子
    int resized_w = 0;
    int resized_h = 0;
    int pad_x = 0;        // 左侧补边
    int pad_y = 0;        // 上侧补边
    int pad_right = 0;    // 右侧补边（奇数余量时与 pad_x 差 1）
    int pad_bottom = 0;
};

// 计算 letterbox：scale=min(dst_w/src_w, dst_h/src_h)，resized=round(src*scale)，补边居中。
// 非法尺寸或非正 scale 返回 false 并给出 reason。
bool compute_letterbox(int src_w, int src_h, int dst_w, int dst_h, LetterboxParams* out,
                       std::string* err);

enum class ChannelOrder { Bgr = 0, Rgb };

// 输入张量量化规格（来自 rknn_query 的输入张量属性；ITEM-001 产出）。
struct InputTensorSpec {
    TensorType dtype = TensorType::Uint8;
    int32_t zero_point = 0;
    float scale = 1.0f;
    bool quantized = false;   // dtype in {Int8,Uint8} 时为 true
};

struct PreprocessConfig {
    int target_w = 640;
    int target_h = 640;
    double mean = 0.0;
    double std = 255.0;         // (value-mean)/std
    std::uint8_t pad_value = 114;
    bool convert_to_rgb = true; // 源 BGR -> 模型 RGB；源已是 RGB 时置 false
    InputTensorSpec input;      // dtype/scale/zp
};

// 预处理结果：HWC 三通道张量，通道顺序为转换后的模型输入顺序（默认 RGB）。
struct PreprocessedInput {
    int width = 0;
    int height = 0;
    int channels = 3;
    TensorType dtype = TensorType::Uint8;
    int32_t zero_point = 0;
    float scale = 1.0f;
    bool quantized = false;
    LetterboxParams letterbox;
    std::vector<std::uint8_t> data;   // int8/uint8: w*h*3 字节；float32: w*h*3*4 字节
    // 统计（用于正确性证据/诊断）：量化后逐通道均值与整体范围（float32 时为归一化值）
    double channel_mean[3] = {0, 0, 0};
    double value_min = 0;
    double value_max = 0;
    bool has_data() const { return !data.empty(); }
};

// 量化/反量化：normalized = (v-mean)/std 后由本函数写值；q = clip(round(n/scale)+zp)。
int32_t quantize_value(double normalized, const InputTensorSpec& spec);
double dequantize_value(int32_t q, const InputTensorSpec& spec);

// packed 8-bit 源（可带行 stride）-> letterbox -> （可选 BGR→RGB）-> 归一化 -> 量化。
// 非法 stride/尺寸/配置返回 false 并置 reason（不产生部分数据）。
bool prepare_from_packed(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                         ChannelOrder order, const PreprocessConfig& cfg, PreprocessedInput* out,
                         std::string* err);

// NV12（带 stride）参考转换 + 同一 letterbox/归一化/量化链路。
// 板端主链路走 librga；此函数用于夹具与一致性校验。
bool prepare_from_nv12(const std::uint8_t* y, const std::uint8_t* uv, int src_w, int src_h,
                       int y_stride, int uv_stride, const PreprocessConfig& cfg,
                       PreprocessedInput* out, std::string* err);

// 由 InputTensorSpec 判定 quantized 字段（Int8/Uint8 -> true）。
void normalize_input_spec(InputTensorSpec* spec);

}  // namespace core
}  // namespace railhmi
