#pragma once

// railhmi-core: 同帧两阶段判定（视觉推理 ITEM-005，front）。
//
// 在同一帧身份 (session_id, camera_id, generation, seq) 下组合人员检测与轨道分割：
//   * 人员框中心点落在 rail 掩码内 -> mask_hit / in_roi 语义 + label=person；
//   * 配置 danger_zone（roi.yaml）时再与该 ROI 求交；
//   * 分割失败 / 掩码为空 -> 判定**不可用**，不输出“轨道内安全”结论；
//   * 旧 generation / 乱序 / 重复 seq 由 FrameGate 拒绝；
//   * 跨帧或跨 generation 缓存默认关闭，启用时必须登记最大年龄与失配策略。
//
// 纯标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/rules.h"                     // RoiConfig / roi_contains
#include "railhmi/core/vision_detect_postprocess.h"  // DetectBox
#include "railhmi/core/vision_seg_postprocess.h"     // SegBox / SegMask

namespace railhmi {
namespace core {

namespace two_stage_reason {
inline const char* kOk() { return "TWO_STAGE_OK"; }
inline const char* kNoAlarm() { return "TWO_STAGE_NO_ALARM"; }
inline const char* kNoPerson() { return "TWO_STAGE_NO_PERSON"; }
inline const char* kUnavailable() { return "TWO_STAGE_UNAVAILABLE"; }
inline const char* kFrameMismatch() { return "TWO_STAGE_FRAME_MISMATCH"; }
inline const char* kStale() { return "TWO_STAGE_STALE"; }
inline const char* kBadConfig() { return "TWO_STAGE_BAD_CONFIG"; }
}  // namespace two_stage_reason

// 跨帧缓存策略（默认关闭；启用时 max_age_ms>0 必须显式登记）。
struct TwoStageCacheConfig {
    bool enabled = false;
    long long max_age_ms = 0;                  // <=0 表示不允许复用
    bool reject_on_generation_change = true;   // 跨 generation 永不复用（实时报警）
    std::string policy_note;                   // 证据用说明
};

struct TwoStageConfig {
    bool require_same_frame = true;         // 检测/分割必须同 (session,camera,generation,seq)
    const RoiConfig* danger_zone = nullptr;  // 可空：未配置 danger_zone 时不额外求交
    TwoStageCacheConfig cache;
    std::string danger_zone_source;          // 证据用（例如 roi.yaml CAM-01/danger_zone）
};

// 输入：persons/rails/rail_masks 为**调用方持有**的数组指针，必须在本调用期间有效
// （零拷贝）；传入临时对象的地址会在调用返回前失效。
struct TwoStageInput {
    FrameId frame;              // 当前帧身份
    FrameId detect_frame;       // 人员检测结果所属帧
    FrameId seg_frame;          // 分割结果所属帧
    bool detect_available = true;
    bool seg_available = true;
    long long now_mono_us = 0;  // 缓存年龄基准（单调微秒）
    const std::vector<DetectBox>* persons = nullptr;    // 人员检测框（已是白名单 person）
    const std::vector<SegBox>* rails = nullptr;         // rail 框
    const std::vector<SegMask>* rail_masks = nullptr;   // 与 rails 对应
    int image_w = 0;
    int image_h = 0;
};

struct TwoStageDetection {
    DetectBox box;
    bool mask_hit = false;         // 中心点在 rail 掩码内
    bool in_danger_zone = false;   // 如配置 danger_zone
    bool on_rail_alarm = false;    // mask_hit && (danger_zone ? in_danger_zone : true)
    std::string label = "person";
};

struct TwoStageResult {
    bool ok = false;          // 判定过程可用（不表示有报警）
    bool available = false;   // 检测与分割同帧可用
    std::string reason;
    std::string detail;
    FrameId frame;
    std::vector<TwoStageDetection> detections;  // 命中的 person（mask_hit 且未因 danger_zone 被排除者仍保留，带标记）
    long long person_total = 0;
    long long mask_hit = 0;
    long long alarm_hits = 0;
    long long dropped_no_mask = 0;
    long long dropped_out_of_zone = 0;
    long long dropped_out_of_bounds = 0;
    bool used_cache = false;
    long long cache_age_ms = -1;
};

// 同一帧身份（session/camera/generation/seq 全等）。
bool same_frame(const FrameId& a, const FrameId& b);

// 严格同帧判定（无缓存）。返回 false 仅表示参数/配置错误；不可用/帧不一致/无目标是
// 有效结果（out->available / out->reason 已填），调用方应以 available 判定可用性。
bool evaluate_two_stage(const TwoStageInput& in, const TwoStageConfig& cfg, TwoStageResult* out,
                        std::string* err);

// 跨帧掩码缓存（默认关闭；启用需登记 max_age_ms 与失配策略）。
class TwoStageCache {
public:
    TwoStageCache() = default;
    explicit TwoStageCache(TwoStageCacheConfig cfg) : cfg_(std::move(cfg)) {}

    bool lookup(const FrameId& frame, long long now_mono_us, std::vector<SegMask>* masks,
                long long* age_ms) const;
    void store(const FrameId& frame, long long now_mono_us, const std::vector<SegMask>& masks);
    void clear();
    bool has() const { return has_; }
    const TwoStageCacheConfig& config() const { return cfg_; }

private:
    TwoStageCacheConfig cfg_;
    bool has_ = false;
    FrameId frame_;
    long long mono_us_ = 0;
    std::vector<SegMask> masks_;
};

// 带身份接纳与可选缓存的判定器（每路一个）。
class TwoStageEvaluator {
public:
    TwoStageEvaluator(TwoStageConfig cfg, std::string session_id);

    TwoStageResult evaluate(const TwoStageInput& in);

    const TwoStageCache& cache() const { return cache_; }
    std::uint64_t generation() const { return gate_.generation(); }

private:
    TwoStageConfig cfg_;
    FrameGate gate_;
    TwoStageCache cache_;
};

}  // namespace core
}  // namespace railhmi
