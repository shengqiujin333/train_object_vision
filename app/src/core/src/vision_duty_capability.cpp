// railhmi-core: 其它职责可用性语义实现（视觉推理 ITEM-010）。
//
// 核心不变量（任何代码路径都不得违反）：
//   1) 返回值默认 `state = DutyState::Unknown`；只有“已声明标签 + 分数达阈值”才可能改变它；
//   2) 不可用（无声明/无描述/不匹配/加载失败）时恒为 unknown，且不会产出报警；
//   3) 配置登记名只被记录，从不参与 available 判定。

#include "railhmi/core/vision_duty_capability.h"

#include <algorithm>
#include <sstream>

namespace railhmi {
namespace core {

namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) o += ' ';
                else o += c;
        }
    }
    return o;
}

// 张量形状（NCHW 或 NHWC）。
bool chw_of(const TensorDesc& t, int* c, int* h, int* w) {
    if (t.dims.size() < 4) return false;
    if (t.fmt == TensorFormat::Nhwc) {
        *h = static_cast<int>(t.dims[1]);
        *w = static_cast<int>(t.dims[2]);
        *c = static_cast<int>(t.dims[3]);
    } else {
        *c = static_cast<int>(t.dims[1]);
        *h = static_cast<int>(t.dims[2]);
        *w = static_cast<int>(t.dims[3]);
    }
    return *c > 0 && *h > 0 && *w > 0;
}

bool quantized_type(TensorType t) {
    return t == TensorType::Int8 || t == TensorType::Uint8;
}

void add(std::vector<std::string>* v, const std::string& s) { v->push_back(s); }

}  // namespace

const char* to_string(DutyKind v) {
    switch (v) {
        case DutyKind::Pantograph: return "pantograph";
        case DutyKind::WheelRail: return "wheel_rail";
        case DutyKind::CabDoor: return "cab_door";
    }
    return "?";
}

bool parse_duty_kind(const std::string& s, DutyKind* out) {
    if (s == "pantograph") { *out = DutyKind::Pantograph; return true; }
    if (s == "wheel_rail") { *out = DutyKind::WheelRail; return true; }
    if (s == "cab_door") { *out = DutyKind::CabDoor; return true; }
    return false;
}

bool duty_of_profile(AiProfile p, DutyKind* out) {
    switch (p) {
        case AiProfile::Pantograph: *out = DutyKind::Pantograph; return true;
        case AiProfile::WheelRail: *out = DutyKind::WheelRail; return true;
        case AiProfile::CabDoor: *out = DutyKind::CabDoor; return true;
        default: return false;
    }
}

AiProfile profile_of_duty(DutyKind d) {
    switch (d) {
        case DutyKind::Pantograph: return AiProfile::Pantograph;
        case DutyKind::WheelRail: return AiProfile::WheelRail;
        case DutyKind::CabDoor: return AiProfile::CabDoor;
    }
    return AiProfile::None;
}

const char* to_string(DutyState v) {
    switch (v) {
        case DutyState::Unknown: return "unknown";
        case DutyState::NotRaised: return "not_raised";
        case DutyState::Normal: return "normal";
        case DutyState::Abnormal: return "abnormal";
        case DutyState::Closed: return "closed";
        case DutyState::Ajar: return "ajar";
    }
    return "unknown";
}

bool parse_duty_state(const std::string& s, DutyState* out) {
    if (s == "unknown") { *out = DutyState::Unknown; return true; }
    if (s == "not_raised") { *out = DutyState::NotRaised; return true; }
    if (s == "normal") { *out = DutyState::Normal; return true; }
    if (s == "abnormal") { *out = DutyState::Abnormal; return true; }
    if (s == "closed") { *out = DutyState::Closed; return true; }
    if (s == "ajar") { *out = DutyState::Ajar; return true; }
    return false;
}

const char* duty_state_display_zh(DutyState v) {
    switch (v) {
        case DutyState::Unknown: return "未知";
        case DutyState::NotRaised: return "未升弓";
        case DutyState::Normal: return "正常";
        case DutyState::Abnormal: return "异常";
        case DutyState::Closed: return "关闭";
        case DutyState::Ajar: return "异常开启";
    }
    return "未知";
}

bool duty_state_allowed(DutyKind duty, DutyState state) {
    switch (duty) {
        case DutyKind::Pantograph:
            return state == DutyState::Unknown || state == DutyState::NotRaised ||
                   state == DutyState::Normal || state == DutyState::Abnormal;
        case DutyKind::WheelRail:
            return state == DutyState::Unknown || state == DutyState::Normal ||
                   state == DutyState::Abnormal;
        case DutyKind::CabDoor:
            return state == DutyState::Unknown || state == DutyState::Closed ||
                   state == DutyState::Ajar;
    }
    return false;
}

bool to_pantograph_state(DutyState s, PantographState* out) {
    switch (s) {
        case DutyState::Unknown: *out = PantographState::Unknown; return true;
        case DutyState::NotRaised: *out = PantographState::NotRaised; return true;
        case DutyState::Normal: *out = PantographState::Normal; return true;
        case DutyState::Abnormal: *out = PantographState::Abnormal; return true;
        default: return false;
    }
}

bool to_door_state(DutyState s, DoorState* out) {
    switch (s) {
        case DutyState::Unknown: *out = DoorState::Unknown; return true;
        case DutyState::Closed: *out = DoorState::Closed; return true;
        case DutyState::Ajar: *out = DoorState::Ajar; return true;
        default: return false;
    }
}

bool DutyDeclaration::complete() const {
    if (declared_model_name.empty()) return false;
    if (input_count != 1) return false;
    if (input_c <= 0 || input_h <= 0 || input_w <= 0) return false;
    if (output_count < 0) return false;
    if (output_count > 0 && output_count != static_cast<int>(labels.size())) return false;
    if (labels.empty()) return false;
    for (const auto& m : labels) {
        if (m.label.empty()) return false;
        if (m.state == DutyState::Unknown) return false;   // 映射到 unknown 无意义
        if (!duty_state_allowed(duty, m.state)) return false;
    }
    return true;
}

DutyAvailability evaluate_duty(const DutyEvidenceInput& in) {
    DutyAvailability a;
    a.duty = in.duty;
    a.config_registered = in.registered_in_config;
    a.registered_model_name = in.registered_model_name;
    a.declared_model_name = in.declaration_provided ? in.declaration.declared_model_name : "";
    a.state = DutyState::Unknown;   // 不变量：默认未知

    // 1) 没有能力声明：配置登记不构成能力（即使登记了模型名）。
    if (!in.declaration_provided) {
        a.available = false;
        a.reason = duty_reason::kProfileUnsupported();
        if (in.registered_in_config && !in.registered_model_name.empty()) {
            a.detail = "config registers '" + in.registered_model_name +
                       "' but no duty capability is declared/verified; registration is not evidence";
        } else {
            a.detail = "no dedicated model capability declared for this duty";
        }
        a.evidence_source = "none";
        return a;
    }

    a.evidence_source = "declaration+descriptor";

    // 2) 声明本身必须完整且状态词表合法。
    if (!in.declaration.complete()) {
        a.available = false;
        a.reason = duty_reason::kModelMismatch();
        a.detail = "duty declaration incomplete or contains invalid label/state mapping";
        add(&a.mismatched, "declaration");
        return a;
    }
    add(&a.matched, "declaration");

    // 3) 必须有真实描述（rknn_query 或夹具）。
    if (!in.descriptor_present) {
        a.available = false;
        a.reason = duty_reason::kModelMissing();
        a.detail = "declared duty model has no verified tensor descriptor";
        return a;
    }
    if (in.descriptor.load_status != "ok") {
        a.available = false;
        a.reason = in.descriptor.load_status.empty() ? duty_reason::kNotReady()
                                                     : in.descriptor.load_status;
        a.detail = "descriptor load status is not ok";
        return a;
    }

    // 4) 结构核验：输入个数/形状/量化 + 输出个数。
    if (static_cast<int>(in.descriptor.inputs.size()) != in.declaration.input_count) {
        add(&a.mismatched, "input_count" + std::to_string(in.descriptor.inputs.size()) + "!=" +
                               std::to_string(in.declaration.input_count));
    } else {
        add(&a.matched, "input_count");
        int c = 0, h = 0, w = 0;
        const TensorDesc& t = in.descriptor.inputs[0];
        if (!chw_of(t, &c, &h, &w)) {
            add(&a.mismatched, "input_shape_unreadable");
        } else {
            if (c != in.declaration.input_c) add(&a.mismatched, "input_c");
            else add(&a.matched, "input_c");
            if (h != in.declaration.input_h || w != in.declaration.input_w) {
                add(&a.mismatched, "input_hw" + std::to_string(h) + "x" + std::to_string(w) + "!=" +
                                       std::to_string(in.declaration.input_h) + "x" +
                                       std::to_string(in.declaration.input_w));
            } else {
                add(&a.matched, "input_hw");
            }
            if (in.declaration.require_quantized_input) {
                if (!quantized_type(t.type) || t.quant != QuantType::Affine) {
                    add(&a.mismatched, "input_not_quantized");
                } else {
                    add(&a.matched, "input_quantized");
                }
            }
        }
    }
    if (in.declaration.output_count > 0) {
        if (static_cast<int>(in.descriptor.outputs.size()) != in.declaration.output_count) {
            add(&a.mismatched, "output_count" + std::to_string(in.descriptor.outputs.size()) + "!=" +
                                   std::to_string(in.declaration.output_count));
        } else {
            add(&a.matched, "output_count");
            // 支持的解码布局：每个输出 1 个元素（类别分数）；其它布局无解码器 -> 不置 ready。
            bool all_single = true;
            for (const auto& o : in.descriptor.outputs)
                if (o.n_elems != 1) all_single = false;
            if (!all_single) add(&a.mismatched, "unsupported_decode_layout");
            else add(&a.matched, "class_score_layout");
        }
    }

    // 5) 记录结构识别结论（仅证据，不参与判定）。
    std::string kind_detail;
    const VisionModelKind kind = identify_kind(in.descriptor, &kind_detail);
    if (!a.mismatched.empty()) {
        a.available = false;
        a.reason = duty_reason::kModelMismatch();
        a.detail = "descriptor does not match declared duty capability (detected=" +
                   std::string(to_string(kind)) + ";" +
                   (kind_detail.empty() ? "n/a" : kind_detail) + ")";
        return a;
    }

    a.available = true;
    a.reason = duty_reason::kReady();
    a.detail = "declared capability verified structurally (detected=" +
               std::string(to_string(kind)) + "); semantic state stays unknown until a declared label passes threshold";
    a.state = DutyState::Unknown;   // READY 不等于“正常”
    return a;
}

DutyLabelResult map_duty_prediction(const DutyDeclaration& decl,
                                    const std::vector<DutyPrediction>& preds,
                                    float threshold_override) {
    DutyLabelResult r;
    r.declaration_present = !decl.labels.empty();
    const float threshold =
        threshold_override >= 0.0f ? threshold_override : (decl.score_min > 0.f ? decl.score_min : 0.5f);

    for (const auto& p : preds) {
        ++r.considered;
        const DutyLabelMapping* hit = nullptr;
        for (const auto& m : decl.labels) {
            if (m.label == p.label) {
                hit = &m;
                break;
            }
        }
        if (!hit) {
            ++r.blocked_unknown_label;   // 未声明标签一律拦截（不猜语义）
            continue;
        }
        if (p.score < threshold) {
            ++r.below_threshold;
            continue;
        }
        if (r.state == DutyState::Unknown || p.score > r.used_score) {
            r.state = hit->state;
            r.used_label = p.label;
            r.used_score = p.score;
        }
    }
    // 不变量：没有过阈值的已声明标签 -> unknown（绝不回落 normal）。
    if (r.state == DutyState::Unknown) {
        r.used_label.clear();
        r.used_score = 0.0f;
    }
    return r;
}

DutyAdapter::DutyAdapter(DutyKind duty, std::string camera_id)
    : duty_(duty), camera_id_(std::move(camera_id)) {
    input_.duty = duty_;
}

void DutyAdapter::set_config_registration(bool registered, const std::string& model_name) {
    input_.registered_in_config = registered;
    input_.registered_model_name = model_name;
}

void DutyAdapter::set_declaration(const DutyDeclaration& d) {
    input_.declaration = d;
    input_.declaration.duty = duty_;
    input_.declaration_provided = true;
}

void DutyAdapter::clear_declaration() {
    input_.declaration = DutyDeclaration{};
    input_.declaration.duty = duty_;
    input_.declaration_provided = false;
}

void DutyAdapter::set_descriptor(const ModelDescriptor& d) {
    input_.descriptor = d;
    input_.descriptor_present = true;
}

void DutyAdapter::clear_descriptor() {
    input_.descriptor = ModelDescriptor{};
    input_.descriptor_present = false;
}

DutyAvailability DutyAdapter::evaluate() {
    availability_ = evaluate_duty(input_);
    ++stats_.evaluations;
    if (availability_.semantics_unknown()) ++stats_.unknown_results;
    return availability_;
}

DutyAvailability DutyAdapter::apply(const std::vector<DutyPrediction>& preds,
                                    float threshold_override) {
    if (availability_.reason.empty() || stats_.evaluations == 0) evaluate();
    if (!availability_.available) {
        // 不可用时拒绝推理结果：保持 unknown，不产生任何“正常/异常”结论。
        ++stats_.rejected_unavailable;
        availability_.state = DutyState::Unknown;
        return availability_;
    }
    ++stats_.predictions;
    const DutyLabelResult r =
        map_duty_prediction(input_.declaration, preds, threshold_override);
    stats_.blocked_unknown_label += r.blocked_unknown_label;
    stats_.below_threshold += r.below_threshold;
    availability_.state = r.state;
    if (r.state == DutyState::Unknown) {
        ++stats_.unknown_results;
        availability_.detail = "no declared label passed threshold; state stays unknown";
    } else {
        availability_.detail = "state from declared label '" + r.used_label + "' score=" +
                               std::to_string(r.used_score);
    }
    return availability_;
}

bool DutyAdapter::to_detection(long long ts_ms, Detection* out) const {
    if (!out) return false;
    if (!availability_.available) return false;
    if (availability_.state == DutyState::Unknown) return false;   // unknown 不产生判定

    Detection d;
    d.camera_id = camera_id_;
    d.profile = profile_of_duty(duty_);
    d.t_ms = ts_ms;
    d.track_id = -1;              // 职责状态不是跟踪目标；null=-1（不使用 0 冒充）
    d.in_roi = true;              // 状态是整帧职责语义，不是 ROI 命中
    d.confidence = 0.0;
    d.label = to_string(duty_);
    switch (duty_) {
        case DutyKind::Pantograph: {
            PantographState st = PantographState::Unknown;
            if (!to_pantograph_state(availability_.state, &st)) return false;
            d.pantograph = st;
            break;
        }
        case DutyKind::WheelRail:
            d.wheel_abnormal = availability_.state == DutyState::Abnormal;
            break;
        case DutyKind::CabDoor: {
            DoorState st = DoorState::Unknown;
            if (!to_door_state(availability_.state, &st)) return false;
            d.door = st;
            break;
        }
    }
    *out = d;
    return true;
}

std::string duty_availability_json(const DutyAvailability& a) {
    std::ostringstream o;
    o << "{";
    o << "\"duty\":\"" << to_string(a.duty) << "\"";
    o << ",\"available\":" << (a.available ? "true" : "false");
    o << ",\"reason\":\"" << json_escape(a.reason) << "\"";
    o << ",\"detail\":\"" << json_escape(a.detail) << "\"";
    o << ",\"state\":\"" << to_string(a.state) << "\"";
    o << ",\"state_display\":\"" << duty_state_display_zh(a.state) << "\"";
    o << ",\"semantics_unknown\":" << (a.semantics_unknown() ? "true" : "false");
    o << ",\"config_registered\":" << (a.config_registered ? "true" : "false");
    o << ",\"registered_model_name\":\"" << json_escape(a.registered_model_name) << "\"";
    o << ",\"declared_model_name\":\"" << json_escape(a.declared_model_name) << "\"";
    o << ",\"evidence_source\":\"" << json_escape(a.evidence_source) << "\"";
    auto arr = [](const std::vector<std::string>& v) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) s += ",";
            s += "\"" + json_escape(v[i]) + "\"";
        }
        return s + "]";
    };
    o << ",\"matched\":" << arr(a.matched);
    o << ",\"mismatched\":" << arr(a.mismatched);
    o << "}";
    return o.str();
}

std::string duty_status_json(const std::vector<DutyStatusItem>& items) {
    std::ostringstream o;
    o << "{\"duties\":[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        const DutyStatusItem& it = items[i];
        if (i) o << ",";
        o << "{";
        o << "\"profile\":\"" << to_string(it.profile) << "\"";
        o << ",\"camera_id\":\"" << json_escape(it.camera_id) << "\"";
        o << ",\"availability\":" << duty_availability_json(it.availability);
        o << ",\"stats\":{";
        o << "\"evaluations\":" << it.stats.evaluations;
        o << ",\"predictions\":" << it.stats.predictions;
        o << ",\"unknown_results\":" << it.stats.unknown_results;
        o << ",\"rejected_unavailable\":" << it.stats.rejected_unavailable;
        o << ",\"blocked_unknown_label\":" << it.stats.blocked_unknown_label;
        o << ",\"below_threshold\":" << it.stats.below_threshold;
        o << "}}";
    }
    o << "]}";
    return o.str();
}

}  // namespace core
}  // namespace railhmi
