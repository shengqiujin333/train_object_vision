#pragma once

// railhmi-media: 帧租约消费与推理预处理（视觉推理 ITEM-002）。
//
// 唯一像素入口是 `FrameLease`（不可变、共享所有权）；**不提供** FrameRef 重载，结构上保证
// 不从元数据推导像素、不复用显示缓冲裸地址。NV12 走已有 librga（BT601_FULL，接受两个成功码），
// BGR→RGB 只在预处理内显式转换；letterbox/归一化/量化在 railhmi-core 纯逻辑中完成。
// 非法格式/stride/尺寸/未映射平面返回稳定 reason，并计入 rejected 统计，不静默丢帧。
//
// target 专用（依赖 librga）；公开头不含 GStreamer。

#include <map>
#include <string>

#include "railhmi/core/vision_preprocess.h"
#include "railhmi/media/frame_lease.h"

namespace railhmi {
namespace media {

struct FramePreprocessConfig {
    railhmi::core::PreprocessConfig core;
    // true: NV12 先经 librga 转 BGR 再进 core 预处理（板端主链路）；
    // false: 用 core 的参考 NV12 实现（一致性校验/无 RGA 环境）。
    bool prefer_rga_for_nv12 = true;
};

struct LeasePreprocessResult {
    bool ok = false;
    std::string reason;          // preprocess_reason token（含 NOT_READY）
    std::string detail;          // 已脱敏说明
    std::string source_format;   // nv12|bgr888|rgb888|unknown
    railhmi::core::PreprocessedInput input;
};

class FramePreprocessor {
public:
    explicit FramePreprocessor(FramePreprocessConfig cfg);

    // 消费帧租约并产出量化张量。空租约 -> NOT_READY；平面临界 -> PRE_UNMAPPED。
    LeasePreprocessResult prepare(const FrameLease& lease);

    long long prepared() const { return prepared_; }
    long long rejected() const { return rejected_; }
    std::map<std::string, long long> rejected_by_reason() const { return rejects_; }

private:
    FramePreprocessConfig cfg_;
    long long prepared_ = 0;
    long long rejected_ = 0;
    std::map<std::string, long long> rejects_;
};

}  // namespace media
}  // namespace railhmi
