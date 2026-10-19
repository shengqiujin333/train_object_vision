#pragma once

// railhmi-media: 真实 RKNN 检测器（视觉推理 ITEM-009）。
//
// 以 `VisionDetector` 接口替换 `SimDetector`：像素只来自 `FrameLease`（经 ITEM-002 预处理），
// 真实执行 `rknn_run`，用 ITEM-003/004 的后处理解码，front 走 ITEM-005 同帧两阶段判定，
// 结果以 ITEM-006 的 `InferenceEnvelope` 返回（可用性/身份/latency/框/掩码引用）。
//
// 能力边界（与 ITEM-001 的证据地图一致，不按文件名/配置登记就宣称具备能力）：
//   * front：需要 yolov5s(det) + yolov8s-seg(seg) 两个模型都通过结构核验才 ready；
//   * wheel_rail / pantograph / cab_door：本轮**无专用模型证据** -> 显式 unavailable +
//     PROFILE_UNSUPPORTED（职责级能力语义由 ITEM-010 落地，本层不冒充）。
// 模型名称/路径全部来自配置（app.ai.models + models 目录），代码不写死模型名。
//
// 精度、检出率、真实场景性能不在本层结论内：这里只保证链路真实执行并如实报告。

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "railhmi/core/rules.h"                       // RoiConfig
#include "railhmi/core/vision_detect_postprocess.h"   // DetectConfig/DetectResult
#include "railhmi/core/vision_model_capability.h"     // DeclaredModel/CapabilityCheck
#include "railhmi/core/vision_duty_capability.h"     // ITEM-010 职责可用性语义
#include "railhmi/core/vision_seg_postprocess.h"      // SegConfig/SegResult
#include "railhmi/core/vision_two_stage.h"            // TwoStageConfig/Result
#include "railhmi/media/frame_preprocess.h"
#include "railhmi/media/rknn_session.h"
#include "railhmi/media/vision_ai_worker.h"           // VisionDetector/VisionInferOutcome

namespace railhmi {
namespace media {

// 推理侧稳定 reason token（叠加在 ITEM-001/006 的 token 之上）。
namespace infer_reason {
inline const char* kInferFailed() { return "INFER_FAILED"; }            // rknn_run/输出异常
inline const char* kPreprocessFailed() { return "PREPROCESS_FAILED"; }  // 帧租约预处理失败
inline const char* kDecodeFailed() { return "DECODE_FAILED"; }          // 后处理解码失败
inline const char* kInputMismatch() { return "INPUT_MISMATCH"; }        // 输入张量规格不符
}  // namespace infer_reason

struct RknnDetectorConfig {
    std::string models_dir;
    std::vector<railhmi::core::DeclaredModel> declared;   // app.ai.models（名称/版本来自配置）
    railhmi::core::DetectConfig detect;                   // score_threshold 来自 thresholds.confidence_min
    railhmi::core::SegConfig seg;
    std::vector<railhmi::core::RoiConfig> rois;           // roi.yaml（danger_zone 求交）
    std::string danger_zone_camera = "CAM-01";
    std::string danger_zone_name = "danger_zone";
    bool enable_danger_zone = true;
    bool prefer_rga_for_nv12 = true;
    railhmi::core::TwoStageCacheConfig two_stage_cache;   // 默认关闭（实时报警不用跨帧缓存）
    // 职责专用模型声明（可选；由配置/CLI 提供，**不由文件名推断**）。
    // 只有“声明能力 + 对应 .rknn 描述逐项核验通过”后该职责才 ready（视觉 ITEM-010）。
    std::vector<railhmi::core::DutyDeclaration> duty_declarations;
    std::vector<std::pair<railhmi::core::DutyKind, std::string>> duty_model_paths;
};

class RknnVisionDetector : public VisionDetector {
public:
    struct ModelStatus {
        railhmi::core::VisionModelKind kind = railhmi::core::VisionModelKind::Unknown;
        std::string declared_name;
        std::string version;
        std::string path;
        bool present = false;
        bool ready = false;
        std::string reason = "NOT_READY";
        std::string detail;
        railhmi::core::CapabilityCheck capability;
        // 输入规格（来自 rknn_query；证据用）
        std::string input_dtype;
        int input_h = 0;
        int input_w = 0;
        float input_scale = 0.f;
        int input_zp = 0;
        std::string runtime_api;
        std::string runtime_drv;
        int output_count = 0;
    };

    struct ProfileStatus {
        railhmi::core::AiProfile profile = railhmi::core::AiProfile::None;
        bool ready = false;
        std::string reason = "NOT_READY";
        std::string model_version;
        std::vector<std::string> model_names;
    };

    explicit RknnVisionDetector(RknnDetectorConfig cfg);
    ~RknnVisionDetector() override;

    RknnVisionDetector(const RknnVisionDetector&) = delete;
    RknnVisionDetector& operator=(const RknnVisionDetector&) = delete;

    // 加载并核验模型（幂等）。返回是否至少一个模型就绪（front 需两个）。
    bool load(std::string* err);

    std::string model_version(railhmi::core::AiProfile p) const override;
    bool ready(railhmi::core::AiProfile p) const override;
    std::string unavailable_reason(railhmi::core::AiProfile p) const override;
    VisionInferOutcome infer(const AiFrame& frame, railhmi::core::AiProfile p) override;
    std::string engine() const override { return "rknn"; }

    std::vector<ModelStatus> models() const;
    std::vector<ProfileStatus> profiles() const;
    // 三职责可用性语义（pantograph/wheel_rail/cab_door；ITEM-010）。
    std::vector<railhmi::core::DutyStatusItem> duties() const;
    std::string status_json() const;

    // 证据：最近一次推理的统计（每实例单线程使用）。
    struct Counters {
        long long infer_calls = 0;
        long long infer_ok = 0;
        long long infer_unavailable = 0;
        long long det_boxes = 0;
        long long seg_boxes = 0;
        long long mask_positive = 0;
        long long preprocess_failed = 0;
        long long infer_failed = 0;
        long long decode_failed = 0;
        long long last_det_us = 0;
        long long last_seg_us = 0;
        long long last_pre_us = 0;
    };
    const Counters& counters() const { return counters_; }

private:
    struct Model;
    struct DutyRuntime;

    void load_duties();
    bool run_duty(DutyRuntime& d, const FrameLease& lease, railhmi::core::DutyState* state,
                  std::string* reason, std::string* detail);
    bool load_kind(railhmi::core::VisionModelKind kind, const railhmi::core::DeclaredModel& decl,
                   std::string* err);
    Model* model_for(railhmi::core::VisionModelKind kind);
    const Model* model_for(railhmi::core::VisionModelKind kind) const;
    bool run_model(Model& m, const FrameLease& lease, std::vector<RknnOutputTensor>* outs,
                   std::string* reason, std::string* detail);
    bool run_detect(const FrameLease& lease, railhmi::core::DetectResult* out, std::string* reason,
                    std::string* detail, railhmi::core::LetterboxParams* lb);
    bool run_seg(const FrameLease& lease, railhmi::core::SegResult* out, std::string* reason,
                 std::string* detail, railhmi::core::LetterboxParams* lb);

    RknnDetectorConfig cfg_;
    std::map<railhmi::core::VisionModelKind, std::unique_ptr<Model>> models_;
    std::map<int, std::unique_ptr<DutyRuntime>> duty_rt_;   // key = DutyKind 整数值
    std::string load_error_;
    bool loaded_ = false;
    Counters counters_;
    // danger_zone 指针需稳定地址（TwoStageConfig 持有裸指针）。
    const railhmi::core::RoiConfig* danger_zone_ = nullptr;
    std::map<std::string, std::unique_ptr<railhmi::core::TwoStageEvaluator>> two_stage_;
    railhmi::core::TwoStageConfig two_stage_cfg_;
};

}  // namespace media
}  // namespace railhmi
