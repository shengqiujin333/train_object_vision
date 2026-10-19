#pragma once

// railhmi-core: YOLOv5 人形检测后处理（视觉推理 ITEM-003 的纯逻辑部分）。
//
// 输入：三个尺度的原始输出张量（本项目 RKNN 导出为 [1,85*3,H,W] int8 NCHW，
//       box/obj/cls 已是 sigmoid 后的 [0,1] 值）+ 预处理 letterbox 参数；
// 输出：原图归一化框 x/y/w/h（x,y∈[0,1]，x+w≤1，y+h≤1），仅保留类别白名单（默认 person=0）。
//
// 关键约定（对齐已部署的历史解码 `code/yolov5_postprocess.cc`，避免训练/部署不一致）：
//   * 通道布局为**通道优先**：channel (85*a + ch) 的平面长度为 grid_h*grid_w；
//     cx/cy 在 ch=0/1，w/h 在 ch=2/3，obj 在 ch=4，类别在 ch=5..84。
//   * box 解码：bx=(v*2-0.5+j)*stride，bw=(v*2)^2*anchor_w，再减去半宽半高。
//   * 输出已含 sigmoid（`apply_sigmoid=false` 为默认）；若模型改为输出 logits，置 true。
//   * 阈值/NMS 由 `DetectConfig` 传入（来自配置/CLI），**不在解码路径写死历史常量**。
//   * NMS 按类别分组、贪心、IoU 采用与历史实现一致的 +1 像素约定（记录在实现文档）。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/vision_model_capability.h"  // TensorType
#include "railhmi/core/vision_preprocess.h"        // LetterboxParams

namespace railhmi {
namespace core {

namespace detect_reason {
inline const char* kOk() { return "DETECT_OK"; }
inline const char* kEmpty() { return "DETECT_EMPTY"; }
inline const char* kBadTensor() { return "DET_BAD_TENSOR"; }
inline const char* kBadConfig() { return "DET_BAD_CONFIG"; }
}  // namespace detect_reason

// 单尺度输出张量视图（不拥有数据）。
struct RawDetectTensor {
    const std::uint8_t* data = nullptr;
    int channels = 0;                       // 85*anchors
    int height = 0;
    int width = 0;
    TensorType dtype = TensorType::Int8;
    int32_t zero_point = 0;
    float scale = 0.0f;
    bool is_float = false;                  // Float32 时为 true（data 指向 float）
};

struct DetectConfig {
    int input_w = 640;
    int input_h = 640;
    float score_threshold = 0.25f;          // 目标分数阈值（obj*cls）
    float nms_iou = 0.45f;                  // NMS IoU 阈值
    int max_detections = 300;               // 结果上限
    std::vector<int> class_whitelist = {0}; // 仅 person（COCO 0）
    int num_classes = 80;
    bool apply_sigmoid = false;             // 输出是否仍是 logits（本项目为 false）
    std::vector<int> anchors;               // 3 尺度 × 3 anchor × (w,h)，共 18 个
    // 溯源（用于证据；不参与计算）
    std::string score_threshold_source;
    std::string nms_iou_source;
};

// 结果框：原图归一化坐标 + 分数 + 类别 + 来源定位（尺度/锚点/网格单元）。
struct DetectBox {
    float x = 0, y = 0, w = 0, h = 0;   // 归一化，x+w≤1，y+h≤1
    float score = 0;
    int class_id = 0;
    std::string label;                   // person / cls_<id>
    int scale_index = -1;
    int anchor_index = -1;
    int cell_i = -1, cell_j = -1;
};

struct DetectResult {
    bool ok = false;                    // 解码成功（含“成功但无框”）
    std::string reason;                 // detect_reason token
    std::string detail;                 // 已脱敏说明
    std::vector<DetectBox> boxes;       // 已按分数降序、已 NMS、已逆 letterbox
    // 统计（可观察）
    long long candidates = 0;           // 通过 obj 预筛的候选数
    long long dropped_score = 0;        // obj*cls 未达阈值
    long long dropped_class = 0;        // 类别不在白名单
    long long nms_suppressed = 0;       // 被 NMS 抑制
    long long dropped_bounds = 0;       // 逆 letterbox 后越界被丢弃
    long long truncated = 0;            // 超出 max_detections 被截断
};

// 默认 anchors（yolov5s；与已部署模型一致，可覆盖）。
std::vector<int> default_yolov5_anchors();
// 类别标签：0 -> "person"，其余 "cls_<id>"。
std::string detect_label(int class_id);

// 解码三尺度 YOLOv5 输出 -> 原图归一化框（仅白名单类别）。
// letterbox 用于把模型空间坐标逆变换回原图；src_w/src_h 为原图尺寸。
bool decode_yolov5(const std::vector<RawDetectTensor>& outputs, const DetectConfig& cfg,
                   const LetterboxParams& letterbox, int src_w, int src_h, DetectResult* out,
                   std::string* err);

// 单框逆 letterbox + 归一化 + 裁剪；返回 false 表示完全越界应丢弃。
bool inverse_letterbox_normalize(float mx1, float my1, float mx2, float my2,
                                 const LetterboxParams& lb, int src_w, int src_h, float* x,
                                 float* y, float* w, float* h);

// IoU（与历史解码一致的 +1 像素约定；坐标为同一像素空间）。
float box_iou_plus1(float ax1, float ay1, float ax2, float ay2, float bx1, float by1, float bx2,
                    float by2);

}  // namespace core
}  // namespace railhmi
