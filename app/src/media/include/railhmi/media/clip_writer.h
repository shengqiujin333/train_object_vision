#pragma once

// railhmi-media: 保护片段固化与原子发布（视频 ITEM-012，target 专用）。
//
// 把解码前压缩分支的压缩包（来自 RecordingTap::collect，已按请求窗口取并关键帧对齐）
// 直通复制封装为可定位的真实媒体文件（MPEG-TS / Matroska，不转码），
// 写入顺序：临时文件 → 完成封装 → 校验可解析 → flush+fsync 文件 → rename → fsync 目录
// → 事务索引发布。任何中断/无媒体写 recording_gaps 并如实标注可用性三态。
// 绝不以 .json 清单或空文件冒充录像。
//
// 公开头不含 GStreamer：GstBuffer/管线细节在 .cpp 内部。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "railhmi/core/clip_request.h"
#include "railhmi/core/compressed_ring.h"

namespace railhmi {
namespace media {

enum class ClipContainer { MpegTs, Matroska };
const char* clip_container_name(ClipContainer c);
const char* clip_container_extension(ClipContainer c);

struct ClipWriterConfig {
    std::string output_dir;              // 片段输出目录
    ClipContainer container = ClipContainer::MpegTs;
    int coverage_tolerance_ms = 1000;    // 覆盖完整度容差（窗口两端）
    int mux_timeout_ms = 20000;          // 封装/校验有限等待
    bool validate = true;                // 封装后重新解析校验（证明可解码）
};

// 事务索引发布回调（DB 写入由事件/持久化层完成）；返回 true=发布成功。
using ClipPublisher = std::function<bool(const core::ClipResult&)>;

class ClipWriter {
public:
    explicit ClipWriter(ClipWriterConfig cfg);

    // 固化：packets 按时间升序（已关键帧对齐）；codec="h264"|"h265"。
    // 返回含请求窗口、实际媒体窗口、可用性与缺口的固化结果。
    core::ClipResult solidify(const core::ClipRequest& req,
                              const std::vector<core::CompressedPacket>& packets,
                              const std::string& codec,
                              const ClipPublisher& publish);

    // 无媒体/失败路径：写缺口并返回 Unavailable。
    core::ClipResult record_unavailable(const core::ClipRequest& req, const std::string& reason);

    // 构造目标路径（不经写盘）；便于调用方登记/预览。
    std::string clip_path(const core::ClipRequest& req) const;

    const ClipWriterConfig& config() const { return cfg_; }
    core::ClipResult last() const { return last_; }

private:
    core::ClipResult cfg_fail(const core::ClipRequest& req, const std::string& reason);
    std::string final_path(const core::ClipRequest& req) const;
    std::string temp_path(const core::ClipRequest& req) const;
    // 直通封装 + 校验；成功返回 true 并填 size_bytes。GStreamer 内部。
    bool mux_and_validate(const core::ClipRequest& req, const std::vector<core::CompressedPacket>& packets,
                          const std::string& codec, const std::string& tmp, long long* size_bytes,
                          std::string* err);

    ClipWriterConfig cfg_;
    core::ClipResult last_;
};

}  // namespace media
}  // namespace railhmi
