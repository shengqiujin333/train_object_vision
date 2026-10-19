#pragma once

// railhmi-core: 距离测量（可选后置，视觉推理 ITEM-011）。
//
// 硬约束（VISION_DESIGN §7.6 / INTERFACE_CONTRACT §6）：
//   * 只有提供**相机标定（含变焦档位）**时才由人员框像素高度换算 `distance_m`；
//   * 未标定 / 变焦未知 / 档位不匹配 / 非人员框 / 像素高非法 / 超出标定有效范围
//     一律返回 `metres = -1`（**未知**），**不用框尺寸臆造米值、不做静默 clamp**；
//   * 换算公式公开可核对：`distance = reference_distance_m * reference_height_px / box_height_px`
//     （历史锚点：500 m 处站立人 ≈89 px → `500*89/px`；参考档位 px/m = 89/1.70 ≈ 52.35）；
//   * 本模块不假设规则消费距离（`danger_proximity_m` 当前未消费，不得写成已实施门槛）。
//
// 纯标准库；不含 Qt/GStreamer/rknn；不读设备/不读 YAML。

#include <cstddef>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

// 距离求值稳定 reason token（脱敏，可被 P5/探针/证据消费）。
namespace distance_reason {
inline const char* kOk() { return "DISTANCE_OK"; }
inline const char* kNoCalibration() { return "NO_CALIBRATION"; }      // 该相机无标定
inline const char* kZoomUnknown() { return "ZOOM_UNKNOWN"; }          // 当前变焦档位未知
inline const char* kZoomMismatch() { return "ZOOM_MISMATCH"; }        // 档位与标定不符
inline const char* kNotPerson() { return "NOT_PERSON"; }              // 只对人员框换算
inline const char* kInvalidBox() { return "INVALID_BOX"; }            // 像素高非法
inline const char* kOutOfRange() { return "OUT_OF_RANGE"; }           // 超出标定有效范围
inline const char* kBadCalibration() { return "BAD_CALIBRATION"; }    // 标定参数自相矛盾
}  // namespace distance_reason

// 单相机标定（必须带变焦档位；参数全部显式，便于逐项核对）。
struct CameraCalibration {
    std::string camera_id;
    std::string zoom_level;                 // 变焦档位标识（必须非空；不允许“任意档位”）
    double reference_distance_m = 0.0;      // 标定时目标距离（如 500）
    double reference_height_px = 0.0;       // 该距离下的框像素高（如 89）
    double reference_object_height_m = 0.0; // 该目标真实身高（如 1.70）
    double min_distance_m = 0.0;            // 标定有效下限（<=0 表示不限）
    double max_distance_m = 0.0;            // 标定有效上限（<=0 表示不限）
    std::string source;                     // 标定来源（证据用；不含凭据）

    bool valid() const;
    // 参考档位下的像素每米（= reference_height_px / reference_object_height_m）。
    double px_per_m_at_reference() const;
    // 换算常数（= reference_distance_m * reference_height_px）。
    double reference_constant() const;
};

// 纯公式：distance = ref_distance * ref_px / px。px<=0 或标定非法时返回 -1。
double distance_from_box_height_px(const CameraCalibration& c, double box_height_px);

// 一次换算请求。
struct DistanceRequest {
    std::string camera_id;
    std::string label;             // 仅 "person" 换算
    double box_height_px = 0.0;    // 原图像素高度
    bool zoom_known = false;       // 当前变焦档位是否已知（未知则不得换算）
    std::string zoom_level;        // 当前档位
};

struct DistanceEstimate {
    bool valid = false;
    double meters = -1.0;                // -1 = 未知（绝不用 0 冒充）
    std::string reason = distance_reason::kNoCalibration();
    std::string detail;
    std::string used_zoom;
    double px_per_m_reference = -1.0;    // 证据：参考档位 px/m
    double box_height_px = 0.0;
};

// 标定集合（按相机；同一相机可登记多个变焦档位）。
class DistanceCalibrationSet {
public:
    void add(const CameraCalibration& c);
    // 精确匹配（相机 + 档位）。
    const CameraCalibration* find(const std::string& camera_id, const std::string& zoom_level) const;
    // 该相机是否有任意标定（用于区分 NO_CALIBRATION 与 ZOOM_MISMATCH）。
    bool has_camera(const std::string& camera_id) const;
    std::vector<std::string> zooms_of(const std::string& camera_id) const;
    const std::vector<CameraCalibration>& all() const { return items_; }
    std::size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }

private:
    std::vector<CameraCalibration> items_;
};

// 完整求值：档位/相机/标签/像素/范围全部门控（顺序固定，reason 可预期）。
DistanceEstimate estimate_distance(const DistanceCalibrationSet& set, const DistanceRequest& req);

// CLI/配置字符串解析：
//   "camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,obj_m=1.70,min=5,max=600,source=README:2026-10-13"
// camera 与 zoom 必填；其余必须为正（obj_m 可为 0 表示未提供，此时 px/m 记 -1）。
bool parse_calibration_spec(const std::string& spec, CameraCalibration* out, std::string* err);

// 调试视图文案：有效 -> "12.3 m"；无效 -> "—"（未知占位，绝不是 "0"）。
std::string distance_text(const DistanceEstimate& e);
std::string distance_json(const DistanceEstimate& e);

}  // namespace core
}  // namespace railhmi
