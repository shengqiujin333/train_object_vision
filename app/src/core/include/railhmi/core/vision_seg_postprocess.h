#pragma once

// railhmi-core: YOLOv8-seg 轨道分割后处理（视觉推理 ITEM-004 的纯逻辑部分）。
//
// 输入：真实 RKNN 导出的 13 个输出张量（3 个尺度 × {box(DFL) / score / score_sum / seg 系数}
//       + 1 个 proto），NCHW 通道优先；
// 输出：rail 框（原图归一化）+ **每框独立** 的原图尺寸二值掩码 plane（0/1），与框一一对应。
//
// 对齐已部署的历史解码 `code/rknpu2_postprocess.cc`：
//   * `dfl_len = box_channels/4`；box 张量为 DFL logits，后处理内做 softmax 加权和；
//     `x1=(-d0+j+0.5)*stride`，`x2=(d2+j+0.5)*stride`（y 同理）。
//   * score 张量为 sigmoid 后概率；`score_sum` 仅作快速预筛（同阈值）。
//   * 掩码 = per-box 32 维系数 · proto(32×160×160)；`> mask_threshold` 为 1，否则 0；
//     默认按框裁剪（crop_to_box），再逆 letterbox 到原图坐标。
//   * 张量数量或形状与期望不符 -> 显式 `MODEL_MISMATCH`，不产出伪掩码。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/vision_detect_postprocess.h"  // RawDetectTensor / 反量化
#include "railhmi/core/vision_model_capability.h"    // TensorType / reason token

namespace railhmi {
namespace core {

namespace seg_reason {
inline const char* kOk() { return "SEG_OK"; }
inline const char* kEmpty() { return "SEG_EMPTY"; }
inline const char* kBadConfig() { return "SEG_BAD_CONFIG"; }
inline const char* kBadTensor() { return "SEG_BAD_TENSOR"; }
inline const char* kModelMismatch() { return "MODEL_MISMATCH"; }
}  // namespace seg_reason

struct SegConfig {
    int input_w = 640;
    int input_h = 640;
    float score_threshold = 0.25f;   // 类别分数阈值
    float nms_iou = 0.45f;
    int max_detections = 300;
    int num_classes = 1;             // rail
    std::vector<int> class_whitelist = {0};
    int proto_channels = 32;
    int proto_h = 160;
    int proto_w = 160;
    float mask_threshold = 0.0f;     // 点积 > 阈值 判为正（历史实现为 >0）
    bool crop_to_box = true;         // 仅框内像素置正
    // 溯源（用于证据；不参与计算）
    std::string score_threshold_source;
    std::string nms_iou_source;
    std::string mask_threshold_source;
};

struct SegBox {
    float x = 0, y = 0, w = 0, h = 0;  // 原图归一化
    float score = 0;
    int class_id = 0;
    std::string label;
    int scale_index = -1, cell_i = -1, cell_j = -1;
};

// 原图尺寸二值掩码；0/非零；stride_bytes=width（单字节）；独立所有权。
struct SegMask {
    int box_index = -1;        // 与 result.boxes 一一对应
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    std::vector<std::uint8_t> plane;  // stride*height 字节，值 0/1
    long long positive = 0;
};

struct SegResult {
    bool ok = false;
    std::string reason;
    std::string detail;
    std::vector<SegBox> boxes;
    std::vector<SegMask> masks;   // 与 boxes 同序一一对应
    long long candidates = 0;
    long long dropped_score = 0;
    long long dropped_class = 0;
    long long nms_suppressed = 0;
    long long dropped_bounds = 0;
    long long truncated = 0;
};

// 期望张量数：3 分支 × 4 + 1 proto = 13。
inline int expected_seg_tensor_count() { return 13; }

// 解码 13 输出 -> rail 框 + 每框原图二值掩码。
bool decode_yolov8_seg(const std::vector<RawDetectTensor>& outputs, const SegConfig& cfg,
                       const LetterboxParams& letterbox, int src_w, int src_h, SegResult* out,
                       std::string* err);

}  // namespace core
}  // namespace railhmi
