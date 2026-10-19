// railhmi-media: 真实 RKNN 检测器实现（视觉推理 ITEM-009）。
//
// 链路：FrameLease -> FramePreprocessor(ITEM-002) -> rknn_inputs_set/rknn_run ->
//       rknn_outputs_get(want_float) -> core 后处理(ITEM-003/004) -> 两阶段(ITEM-005) ->
//       InferenceEnvelope(ITEM-006)。像素只来自租约；不从 FrameRef/元数据推像素。

#include "railhmi/media/vision_rknn_detector.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <utility>

namespace railhmi {
namespace media {

namespace {

long long mono_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

// 由张量描述取 (c,h,w)（NCHW 或 NHWC）。
bool chw_of(const railhmi::core::TensorDesc& t, int* c, int* h, int* w) {
    if (t.dims.size() < 4) return false;
    if (t.fmt == railhmi::core::TensorFormat::Nhwc) {
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

uint32_t input_type_of(railhmi::core::TensorType t) {
    switch (t) {
        case railhmi::core::TensorType::Float32: return 0;
        case railhmi::core::TensorType::Int8: return 2;
        case railhmi::core::TensorType::Uint8: return 3;
        default: return 0xFFFFFFFFu;
    }
}

std::string dtype_name(railhmi::core::TensorType t) {
    return std::string(railhmi::core::to_string(t));
}

}  // namespace

struct RknnVisionDetector::DutyRuntime {
    railhmi::core::DutyKind kind = railhmi::core::DutyKind::Pantograph;
    std::unique_ptr<railhmi::core::DutyAdapter> adapter;
    railhmi::core::DutyDeclaration decl;
    std::unique_ptr<RknnSession> session;
    std::unique_ptr<FramePreprocessor> pre;
    uint32_t input_type = 0;
    std::string path;
    bool io_ready = false;
};

struct RknnVisionDetector::Model {
    railhmi::core::VisionModelKind kind = railhmi::core::VisionModelKind::Unknown;
    railhmi::core::DeclaredModel decl;
    std::string path;
    std::unique_ptr<RknnSession> session;
    railhmi::core::ModelDescriptor desc;
    railhmi::core::CapabilityCheck check;
    railhmi::core::PreprocessConfig preprocess;
    std::unique_ptr<FramePreprocessor> pre;
    uint32_t input_type = 0;
    bool ready = false;
    std::string reason = "NOT_READY";
    std::string detail;
};

RknnVisionDetector::RknnVisionDetector(RknnDetectorConfig cfg) : cfg_(std::move(cfg)) {
    // 默认值兜底（阈值本身来自配置/CLI；此处只保证结构性字段有效）。
    if (cfg_.detect.input_w <= 0) cfg_.detect.input_w = 640;
    if (cfg_.detect.input_h <= 0) cfg_.detect.input_h = 640;
    if (cfg_.detect.anchors.empty()) cfg_.detect.anchors = railhmi::core::default_yolov5_anchors();
    if (cfg_.seg.input_w <= 0) cfg_.seg.input_w = 640;
    if (cfg_.seg.input_h <= 0) cfg_.seg.input_h = 640;

    if (cfg_.enable_danger_zone) {
        const railhmi::core::RoiConfig* r =
            railhmi::core::find_roi(cfg_.rois, cfg_.danger_zone_camera, cfg_.danger_zone_name);
        danger_zone_ = r;
    }
    two_stage_cfg_.require_same_frame = true;
    two_stage_cfg_.danger_zone = danger_zone_;
    two_stage_cfg_.cache = cfg_.two_stage_cache;
    two_stage_cfg_.danger_zone_source =
        danger_zone_ ? (cfg_.danger_zone_camera + "/" + cfg_.danger_zone_name) : "none";
}

RknnVisionDetector::~RknnVisionDetector() = default;

bool RknnVisionDetector::load(std::string* err) {
    if (loaded_) {
        if (err) *err = load_error_;
        return ready(railhmi::core::AiProfile::Front);
    }
    loaded_ = true;
    load_error_.clear();

    // 模型名/版本全部来自配置：按声明名推断种类，每种只加载一次。
    railhmi::core::DeclaredModel det_decl;
    railhmi::core::DeclaredModel seg_decl;
    bool have_det = false;
    bool have_seg = false;
    for (const auto& d : cfg_.declared) {
        const railhmi::core::ModelSpec spec = railhmi::core::spec_for_declared(d.name, d.profile);
        if (spec.kind == railhmi::core::VisionModelKind::YoloV5Detect && !have_det) {
            det_decl = d;
            have_det = true;
        } else if (spec.kind == railhmi::core::VisionModelKind::YoloV8Seg && !have_seg) {
            seg_decl = d;
            have_seg = true;
        }
    }

    std::string e1;
    std::string e2;
    if (have_det) {
        if (!load_kind(railhmi::core::VisionModelKind::YoloV5Detect, det_decl, &e1))
            load_error_ += "det:" + e1 + ";";
    } else {
        load_error_ += "det:no declared yolov5 detect model in config;";
    }
    if (have_seg) {
        if (!load_kind(railhmi::core::VisionModelKind::YoloV8Seg, seg_decl, &e2))
            load_error_ += "seg:" + e2 + ";";
    } else {
        load_error_ += "seg:no declared yolov8-seg model in config;";
    }
    load_duties();   // ITEM-010：三职责可用性语义（unknown 为缺省）
    if (err) *err = load_error_;
    return ready(railhmi::core::AiProfile::Front);
}


// 职责可用性（ITEM-010）：无声明 -> PROFILE_UNSUPPORTED；声明 + 专用模型描述核验通过 -> READY。
// 配置登记只记录为身份，不参与 available 判定；不可用时状态恒为 unknown。
void RknnVisionDetector::load_duties() {
    const railhmi::core::DutyKind all[] = {railhmi::core::DutyKind::Pantograph,
                                          railhmi::core::DutyKind::WheelRail,
                                          railhmi::core::DutyKind::CabDoor};
    for (auto kind : all) {
        auto rt = std::unique_ptr<DutyRuntime>(new DutyRuntime());
        rt->kind = kind;
        rt->adapter.reset(new railhmi::core::DutyAdapter(kind));

        // 1) 配置登记（仅身份记录；配置登记不是能力证据）。
        const railhmi::core::AiProfile prof = railhmi::core::profile_of_duty(kind);
        for (const auto& d : cfg_.declared) {
            if (d.profile == prof) {
                rt->adapter->set_config_registration(true, d.name);
                break;
            }
        }

        // 2) 能力声明（来自配置/CLI）。
        for (const auto& decl : cfg_.duty_declarations) {
            if (decl.duty == kind) {
                rt->decl = decl;
                rt->adapter->set_declaration(decl);
                break;
            }
        }

        // 3) 专用模型（可选）：打开取描述，交 core 逐项核验。
        for (const auto& kv : cfg_.duty_model_paths) {
            if (kv.first != kind) continue;   // 每个职责只认自己的模型路径
            rt->path = kv.second;
            rt->session.reset(new RknnSession());
            railhmi::core::ModelDescriptor desc;
            std::string err;
            if (!rt->session->open(rt->path, &desc, &err)) {
                if (desc.load_status.empty()) desc.load_status = "MODEL_LOAD_FAILED";
                rt->adapter->set_descriptor(desc);
                break;
            }
            rt->adapter->set_descriptor(desc);
            // 类分数模型的输入规格（与检测/分割同一预处理链路）。
            if (!desc.inputs.empty()) {
                int c = 0, h = 0, w = 0;
                if (chw_of(desc.inputs[0], &c, &h, &w) && c == 3) {
                    railhmi::core::PreprocessConfig pc;
                    railhmi::core::InputTensorSpec is;
                    is.dtype = desc.inputs[0].type;
                    is.zero_point = desc.inputs[0].zp;
                    is.scale = desc.inputs[0].scale;
                    is.quantized = (desc.inputs[0].type == railhmi::core::TensorType::Int8 ||
                                    desc.inputs[0].type == railhmi::core::TensorType::Uint8);
                    railhmi::core::normalize_input_spec(&is);
                    pc.input = is;
                    pc.target_w = w;
                    pc.target_h = h;
                    pc.mean = 0.0;
                    pc.std = 255.0;
                    pc.convert_to_rgb = true;
                    pc.pad_value = 114;
                    FramePreprocessConfig pcfg;
                    pcfg.core = pc;
                    pcfg.prefer_rga_for_nv12 = cfg_.prefer_rga_for_nv12;
                    rt->pre.reset(new FramePreprocessor(pcfg));
                    rt->input_type = input_type_of(desc.inputs[0].type);
                    rt->io_ready = (rt->input_type != 0xFFFFFFFFu);
                }
            }
            break;
        }

        rt->adapter->evaluate();
        duty_rt_[static_cast<int>(kind)] = std::move(rt);
    }
}

// 类分数解码：第 i 个输出 -> 声明标签表中的第 i 个标签（顺序由声明固定）。
bool RknnVisionDetector::run_duty(DutyRuntime& rt, const FrameLease& lease,
                                  railhmi::core::DutyState* state, std::string* reason,
                                  std::string* detail) {
    auto fail = [&](const std::string& r, const std::string& d) {
        if (reason) *reason = r;
        if (detail) *detail = d;
        return false;
    };
    if (!rt.adapter || !rt.adapter->availability().available) {
        return fail(rt.adapter ? rt.adapter->availability().reason
                               : railhmi::core::duty_reason::kProfileUnsupported(),
                    rt.adapter ? rt.adapter->availability().detail : "no duty adapter");
    }
    if (!rt.pre || !rt.session || !rt.io_ready)
        return fail(infer_reason::kInputMismatch(), "duty model has no usable input spec");
    const LeasePreprocessResult pre = rt.pre->prepare(lease);
    if (!pre.ok) {
        ++counters_.preprocess_failed;
        return fail(infer_reason::kPreprocessFailed(), pre.reason + " " + pre.detail);
    }
    std::vector<RknnOutputTensor> outs;
    std::string err;
    if (!rt.session->set_input(pre.input.data.data(), pre.input.data.size(), rt.input_type, &err) ||
        !rt.session->run(&err) || !rt.session->get_outputs_float(&outs, &err)) {
        ++counters_.infer_failed;
        return fail(infer_reason::kInferFailed(), err);
    }
    std::vector<railhmi::core::DutyPrediction> preds;
    preds.reserve(outs.size());
    for (std::size_t i = 0; i < outs.size(); ++i) {
        railhmi::core::DutyPrediction pr;
        if (i < rt.decl.labels.size()) pr.label = rt.decl.labels[i].label;
        pr.score = outs[i].buf ? *static_cast<const float*>(outs[i].buf) : 0.0f;
        preds.push_back(pr);
    }
    const railhmi::core::DutyAvailability a = rt.adapter->apply(preds);
    if (state) *state = a.state;
    if (reason) *reason = railhmi::core::duty_reason::kReady();
    if (detail) *detail = std::string("duty state=") + railhmi::core::to_string(a.state);
    return true;
}

bool RknnVisionDetector::load_kind(railhmi::core::VisionModelKind kind,
                                   const railhmi::core::DeclaredModel& decl, std::string* err) {
    auto m = std::unique_ptr<Model>(new Model());
    m->kind = kind;
    m->decl = decl;
    m->path = railhmi::core::resolve_model_path(cfg_.models_dir, decl.name);
    m->session.reset(new RknnSession());

    // 失败路径统一处理：先把 reason/detail 写入 Model 并**拷贝**到局部，再移交所有权。
    // 不能在 models_[kind]=std::move(m) 之后解引用 m（D-01：模型缺失路径曾因此 SIGSEGV）。
    auto fail_with = [&](std::unique_ptr<Model> model, std::string reason,
                         std::string detail) -> bool {
        model->ready = false;
        model->reason = std::move(reason);
        model->detail = std::move(detail);
        const std::string reason_copy = model->reason;
        models_[kind] = std::move(model);
        if (err) *err = reason_copy;
        return false;
    };

    std::string open_err;
    if (!m->session->open(m->path, &m->desc, &open_err)) {
        const std::string reason =
            m->desc.load_status.empty() ? "MODEL_LOAD_FAILED" : m->desc.load_status;
        return fail_with(std::move(m), reason, open_err);
    }

    const railhmi::core::ModelSpec spec =
        railhmi::core::spec_for_declared(decl.name, railhmi::core::AiProfile::Front);
    m->check = railhmi::core::check_capability(m->desc, spec);
    if (!m->check.ready)
        return fail_with(std::move(m), m->check.reason, m->check.detail);

    if (m->desc.inputs.size() != 1) {
        return fail_with(std::move(m), infer_reason::kInputMismatch(),
                         "input tensor count=" + std::to_string(m->desc.inputs.size()));
    }

    const railhmi::core::TensorDesc& in = m->desc.inputs[0];
    int c = 0, h = 0, w = 0;
    if (!chw_of(in, &c, &h, &w) || c != 3) {
        return fail_with(std::move(m), infer_reason::kInputMismatch(),
                         "input shape unsupported (c=" + std::to_string(c) + ")");
    }
    m->input_type = input_type_of(in.type);
    if (m->input_type == 0xFFFFFFFFu) {
        return fail_with(std::move(m), infer_reason::kInputMismatch(),
                         "input dtype unsupported: " + dtype_name(in.type));
    }

    railhmi::core::InputTensorSpec is;
    is.dtype = in.type;
    is.zero_point = in.zp;
    is.scale = in.scale;
    is.quantized = (in.type == railhmi::core::TensorType::Int8 ||
                    in.type == railhmi::core::TensorType::Uint8);
    railhmi::core::normalize_input_spec(&is);
    m->preprocess.input = is;
    m->preprocess.target_w = w;
    m->preprocess.target_h = h;
    m->preprocess.mean = 0.0;
    m->preprocess.std = 255.0;   // 与模型导出量化参数一致（scale≈1/255、zp=-128）
    m->preprocess.convert_to_rgb = true;
    m->preprocess.pad_value = 114;

    FramePreprocessConfig pcfg;
    pcfg.core = m->preprocess;
    pcfg.prefer_rga_for_nv12 = cfg_.prefer_rga_for_nv12;
    m->pre.reset(new FramePreprocessor(pcfg));

    m->ready = true;
    m->reason = railhmi::core::vision_reason::kReady();
    m->detail = "capability verified";
    models_[kind] = std::move(m);
    return true;
}

RknnVisionDetector::Model* RknnVisionDetector::model_for(railhmi::core::VisionModelKind kind) {
    auto it = models_.find(kind);
    return it == models_.end() ? nullptr : it->second.get();
}

const RknnVisionDetector::Model* RknnVisionDetector::model_for(
    railhmi::core::VisionModelKind kind) const {
    auto it = models_.find(kind);
    return it == models_.end() ? nullptr : it->second.get();
}

std::string RknnVisionDetector::model_version(railhmi::core::AiProfile p) const {
    railhmi::core::DutyKind dk = railhmi::core::DutyKind::Pantograph;
    if (railhmi::core::duty_of_profile(p, &dk)) {
        auto it = duty_rt_.find(static_cast<int>(dk));
        if (it == duty_rt_.end()) return std::string();
        // 职责模型版本仅在“已声明并可核验”时展示；不可用时不高调宣称模型能力。
        return it->second->adapter && it->second->adapter->availability().available
                   ? it->second->decl.declared_model_name
                   : std::string();
    }
    const Model* det = model_for(railhmi::core::VisionModelKind::YoloV5Detect);
    const Model* seg = model_for(railhmi::core::VisionModelKind::YoloV8Seg);
    if (p == railhmi::core::AiProfile::Front) {
        std::string v;
        if (det && det->ready) v = det->decl.version;
        if (seg && seg->ready) v += (v.empty() ? "" : "+") + seg->decl.version;
        return v;
    }
    return std::string();
}

bool RknnVisionDetector::ready(railhmi::core::AiProfile p) const {
    // 职责（pantograph/wheel_rail/cab_door）：只有“声明能力 + 描述核验通过”才 ready（ITEM-010）。
    railhmi::core::DutyKind dk = railhmi::core::DutyKind::Pantograph;
    if (railhmi::core::duty_of_profile(p, &dk)) {
        auto it = duty_rt_.find(static_cast<int>(dk));
        if (it == duty_rt_.end() || !it->second->adapter) return false;
        return it->second->adapter->availability().available && it->second->io_ready;
    }
    if (p != railhmi::core::AiProfile::Front) return false;
    const railhmi::core::ProfileRequirement req = railhmi::core::requirement_for_profile(p);
    for (auto kind : req.required_kinds) {
        const Model* m = model_for(kind);
        if (!m || !m->ready) return false;
    }
    return true;
}

std::string RknnVisionDetector::unavailable_reason(railhmi::core::AiProfile p) const {
    railhmi::core::DutyKind dk = railhmi::core::DutyKind::Pantograph;
    if (railhmi::core::duty_of_profile(p, &dk)) {
        auto it = duty_rt_.find(static_cast<int>(dk));
        if (it == duty_rt_.end() || !it->second->adapter)
            return railhmi::core::duty_reason::kProfileUnsupported();
        const railhmi::core::DutyAvailability& a = it->second->adapter->availability();
        if (a.available && !it->second->io_ready) return infer_reason::kInputMismatch();
        return a.reason;
    }
    if (p != railhmi::core::AiProfile::Front)
        return railhmi::core::duty_reason::kProfileUnsupported();
    const railhmi::core::ProfileRequirement req = railhmi::core::requirement_for_profile(p);
    for (auto kind : req.required_kinds) {
        const Model* m = model_for(kind);
        if (!m) return railhmi::core::vision_reason::kModelMissing();
        if (!m->ready) return m->reason.empty() ? railhmi::core::vision_reason::kNotReady() : m->reason;
    }
    return railhmi::core::vision_reason::kReady();
}

std::vector<railhmi::core::DutyStatusItem> RknnVisionDetector::duties() const {
    std::vector<railhmi::core::DutyStatusItem> out;
    const railhmi::core::DutyKind all[] = {railhmi::core::DutyKind::Pantograph,
                                           railhmi::core::DutyKind::WheelRail,
                                           railhmi::core::DutyKind::CabDoor};
    for (auto kind : all) {
        railhmi::core::DutyStatusItem it;
        it.duty = kind;
        it.profile = railhmi::core::profile_of_duty(kind);
        auto found = duty_rt_.find(static_cast<int>(kind));
        if (found != duty_rt_.end() && found->second->adapter) {
            it.availability = found->second->adapter->availability();
            it.stats = found->second->adapter->stats();
        } else {
            it.availability.duty = kind;
        }
        out.push_back(std::move(it));
    }
    return out;
}

bool RknnVisionDetector::run_model(Model& m, const FrameLease& lease,
                                   std::vector<RknnOutputTensor>* outs, std::string* reason,
                                   std::string* detail) {
    if (!m.pre || !m.session || !m.ready) {
        if (reason) *reason = railhmi::core::vision_reason::kNotReady();
        if (detail) *detail = "model not ready";
        return false;
    }
    const long long t0 = mono_us();
    const LeasePreprocessResult pre = m.pre->prepare(lease);
    counters_.last_pre_us = mono_us() - t0;
    if (!pre.ok) {
        ++counters_.preprocess_failed;
        if (reason) *reason = infer_reason::kPreprocessFailed();
        if (detail) *detail = pre.reason + " " + pre.detail;
        return false;
    }
    std::string err;
    if (!m.session->set_input(pre.input.data.data(), pre.input.data.size(), m.input_type, &err) ||
        !m.session->run(&err) || !m.session->get_outputs_float(outs, &err)) {
        ++counters_.infer_failed;
        if (reason) *reason = infer_reason::kInferFailed();
        if (detail) *detail = err;
        return false;
    }
    return true;
}

bool RknnVisionDetector::run_detect(const FrameLease& lease, railhmi::core::DetectResult* out,
                                    std::string* reason, std::string* detail,
                                    railhmi::core::LetterboxParams* lb) {
    Model* m = model_for(railhmi::core::VisionModelKind::YoloV5Detect);
    if (!m) {
        if (reason) *reason = railhmi::core::vision_reason::kModelMissing();
        if (detail) *detail = "detect model not loaded";
        return false;
    }
    std::vector<RknnOutputTensor> outs;
    const long long t0 = mono_us();
    if (!run_model(*m, lease, &outs, reason, detail)) return false;
    counters_.last_det_us = mono_us() - t0;

    std::vector<railhmi::core::RawDetectTensor> tensors;
    tensors.reserve(outs.size());
    for (const auto& o : outs) {
        railhmi::core::RawDetectTensor t;
        int c = 0, h = 0, w = 0;
        if (!o.desc || !chw_of(*o.desc, &c, &h, &w)) {
            if (reason) *reason = infer_reason::kDecodeFailed();
            if (detail) *detail = "output shape unreadable";
            return false;
        }
        t.data = static_cast<const uint8_t*>(o.buf);
        t.channels = c;
        t.height = h;
        t.width = w;
        t.dtype = railhmi::core::TensorType::Float32;
        t.is_float = true;
        t.fmt = o.desc->fmt;
        t.scale = 0.f;
        t.zero_point = 0;
        tensors.push_back(t);
    }

    // letterbox 参数：与模型输入相同规格（预处理用的同一份配置）。
    railhmi::core::LetterboxParams params;
    std::string lerr;
    const railhmi::core::FrameLayout& layout = lease.descriptor().layout;
    if (!railhmi::core::compute_letterbox(static_cast<int>(layout.width),
                                         static_cast<int>(layout.height), m->preprocess.target_w,
                                         m->preprocess.target_h, &params, &lerr)) {
        if (reason) *reason = infer_reason::kPreprocessFailed();
        if (detail) *detail = "letterbox: " + lerr;
        return false;
    }
    if (lb) *lb = params;

    std::string derr;
    railhmi::core::DetectConfig dcfg = cfg_.detect;
    dcfg.input_w = m->preprocess.target_w;
    dcfg.input_h = m->preprocess.target_h;
    const bool ok = railhmi::core::decode_yolov5(tensors, dcfg, params,
                                                 static_cast<int>(layout.width),
                                                 static_cast<int>(layout.height), out, &derr);
    if (!ok) {
        ++counters_.decode_failed;
        if (reason) *reason = infer_reason::kDecodeFailed();
        if (detail) *detail = derr;
        return false;
    }
    counters_.det_boxes += static_cast<long long>(out->boxes.size());
    return true;
}

bool RknnVisionDetector::run_seg(const FrameLease& lease, railhmi::core::SegResult* out,
                                 std::string* reason, std::string* detail,
                                 railhmi::core::LetterboxParams* lb) {
    Model* m = model_for(railhmi::core::VisionModelKind::YoloV8Seg);
    if (!m) {
        if (reason) *reason = railhmi::core::vision_reason::kModelMissing();
        if (detail) *detail = "seg model not loaded";
        return false;
    }
    std::vector<RknnOutputTensor> outs;
    const long long t0 = mono_us();
    if (!run_model(*m, lease, &outs, reason, detail)) return false;
    counters_.last_seg_us = mono_us() - t0;

    std::vector<railhmi::core::RawDetectTensor> tensors;
    tensors.reserve(outs.size());
    for (const auto& o : outs) {
        railhmi::core::RawDetectTensor t;
        int c = 0, h = 0, w = 0;
        if (!o.desc || !chw_of(*o.desc, &c, &h, &w)) {
            if (reason) *reason = infer_reason::kDecodeFailed();
            if (detail) *detail = "output shape unreadable";
            return false;
        }
        t.data = static_cast<const uint8_t*>(o.buf);
        t.channels = c;
        t.height = h;
        t.width = w;
        t.dtype = railhmi::core::TensorType::Float32;
        t.is_float = true;
        t.fmt = o.desc->fmt;
        t.scale = 0.f;
        t.zero_point = 0;
        tensors.push_back(t);
    }

    railhmi::core::LetterboxParams params;
    std::string lerr;
    const railhmi::core::FrameLayout& layout = lease.descriptor().layout;
    if (!railhmi::core::compute_letterbox(static_cast<int>(layout.width),
                                         static_cast<int>(layout.height), m->preprocess.target_w,
                                         m->preprocess.target_h, &params, &lerr)) {
        if (reason) *reason = infer_reason::kPreprocessFailed();
        if (detail) *detail = "letterbox: " + lerr;
        return false;
    }
    if (lb) *lb = params;

    std::string derr;
    railhmi::core::SegConfig scfg = cfg_.seg;
    scfg.input_w = m->preprocess.target_w;
    scfg.input_h = m->preprocess.target_h;
    const bool ok = railhmi::core::decode_yolov8_seg(tensors, scfg, params,
                                                     static_cast<int>(layout.width),
                                                     static_cast<int>(layout.height), out, &derr);
    if (!ok) {
        ++counters_.decode_failed;
        if (reason) *reason = infer_reason::kDecodeFailed();
        if (detail) *detail = derr;
        return false;
    }
    counters_.seg_boxes += static_cast<long long>(out->boxes.size());
    for (const auto& mk : out->masks) counters_.mask_positive += mk.positive;
    return true;
}

VisionInferOutcome RknnVisionDetector::infer(const AiFrame& frame, railhmi::core::AiProfile p) {
    ++counters_.infer_calls;
    const long long t_start = mono_us();
    VisionInferOutcome out;
    out.envelope.frame.session_id = frame.session_id;
    out.envelope.frame.camera_id = frame.camera_id;
    out.envelope.frame.generation = frame.generation;
    out.envelope.frame.seq = frame.seq;
    out.envelope.profile = p;
    out.envelope.capture_mono_us = frame.capture_mono_us;
    out.envelope.model_version = model_version(p);

    auto unavailable = [&](const std::string& reason, const std::string& detail) {
        out.available = false;
        out.reason = reason;
        out.detail = detail;
        out.envelope.availability = railhmi::core::AiAvailability::Unavailable;
        out.envelope.reason = reason;
        out.envelope.boxes.clear();
        out.envelope.masks.clear();
        out.envelope.result_mono_us = mono_us();
        out.envelope.latency_ms = railhmi::core::latency_ms_of(out.envelope.capture_mono_us,
                                                              out.envelope.result_mono_us);
        out.infer_us = out.envelope.result_mono_us - t_start;
        ++counters_.infer_unavailable;
        return out;
    };

    if (!frame.lease.valid())
        return unavailable(railhmi::core::vision_reason::kNotReady(), "empty frame lease");

    // 职责（pantograph/wheel_rail/cab_door，ITEM-010）：类分数解码 -> 声明标签 -> 状态。
    // 职责状态不是检测框：envelope 仅表达可用性（ready + 空框），状态经 detector->duties() 发布；
    // 不可用时恒 unknown 且不产生任何判定。
    railhmi::core::DutyKind dk = railhmi::core::DutyKind::Pantograph;
    if (railhmi::core::duty_of_profile(p, &dk)) {
        auto it = duty_rt_.find(static_cast<int>(dk));
        if (it == duty_rt_.end() || !it->second->adapter)
            return unavailable(railhmi::core::duty_reason::kProfileUnsupported(),
                               "no duty adapter");
        if (!ready(p))
            return unavailable(unavailable_reason(p),
                               it->second->adapter->availability().detail);
        railhmi::core::DutyState state = railhmi::core::DutyState::Unknown;
        std::string reason;
        std::string detail;
        if (!run_duty(*it->second, frame.lease, &state, &reason, &detail))
            return unavailable(reason, detail);
        railhmi::core::EnvelopeBuildInput bin;
        bin.frame = out.envelope.frame;
        bin.profile = p;
        bin.model_version = it->second->decl.declared_model_name;
        bin.capture_mono_us = frame.capture_mono_us;
        bin.result_mono_us = mono_us();
        bin.detect_ready = true;
        bin.requires_segmentation = false;
        out.envelope = railhmi::core::build_envelope(bin);
        out.available = out.envelope.ready();
        out.reason = out.envelope.reason;
        out.detail = detail;
        out.infer_us = out.envelope.result_mono_us - t_start;
        if (out.available) ++counters_.infer_ok;
        else ++counters_.infer_unavailable;
        return out;
    }

    if (!ready(p))
        return unavailable(unavailable_reason(p), "profile has no verified model capability");

    railhmi::core::DetectResult det;
    std::string d_reason;
    std::string d_detail;
    railhmi::core::LetterboxParams d_lb;
    if (!run_detect(frame.lease, &det, &d_reason, &d_detail, &d_lb))
        return unavailable(d_reason, d_detail);

    railhmi::core::SegResult seg;
    railhmi::core::LetterboxParams s_lb;
    bool seg_ok = false;
    if (p == railhmi::core::AiProfile::Front) {
        seg_ok = run_seg(frame.lease, &seg, &d_reason, &d_detail, &s_lb);
        if (!seg_ok) {
            // 分割失败不得退化为“轨道内安全”：整帧判不可用（含 reason）。
            return unavailable(d_reason, d_detail);
        }
    }

    // 框与掩码（front：同帧两阶段判定）。
    std::vector<railhmi::core::DetectBox> boxes;
    std::vector<char> in_roi;
    std::vector<railhmi::core::MaskRef> masks;

    if (p == railhmi::core::AiProfile::Front) {
        std::shared_ptr<railhmi::core::SegResult> segp(
            new railhmi::core::SegResult(std::move(seg)));
        out.seg = segp;

        railhmi::core::TwoStageInput in;
        in.frame = out.envelope.frame;
        in.detect_frame = out.envelope.frame;
        in.seg_frame = out.envelope.frame;
        in.detect_available = det.ok;
        in.seg_available = seg_ok;
        in.now_mono_us = mono_us();
        in.persons = &det.boxes;
        in.rails = &segp->boxes;
        in.rail_masks = &segp->masks;
        in.image_w = static_cast<int>(frame.lease.descriptor().layout.width);
        in.image_h = static_cast<int>(frame.lease.descriptor().layout.height);

        auto it = two_stage_.find(frame.camera_id);
        if (it == two_stage_.end()) {
            it = two_stage_
                     .emplace(frame.camera_id,
                              std::unique_ptr<railhmi::core::TwoStageEvaluator>(
                                  new railhmi::core::TwoStageEvaluator(two_stage_cfg_,
                                                                      frame.session_id)))
                     .first;
        }
        railhmi::core::TwoStageResult ts;
        std::string ts_err;
        const bool ts_ok = railhmi::core::evaluate_two_stage(in, two_stage_cfg_, &ts, &ts_err);
        if (!ts_ok || !ts.available) {
            // 分割失败/掩码为空 -> 不可用（reason 取自 ITEM-005 判定，不输出“轨道内安全”）。
            return unavailable(ts.reason.empty() ? railhmi::core::envelope_reason::kSegUnavailable()
                                                : ts.reason,
                               ts_ok ? ts.detail : ts_err);
        }
        boxes.reserve(ts.detections.size());
        in_roi.reserve(ts.detections.size());
        for (const auto& d : ts.detections) {
            boxes.push_back(d.box);
            in_roi.push_back(d.on_rail_alarm ? 1 : 0);
        }
        // rail 掩码：原图坐标独立二值 plane，零拷贝引用（所有权在 out.seg）。
        // box_index=-1：掩码属于 rail 框集合，不与 person boxes 一一对应（记录于实现文档）。
        for (std::size_t i = 0; i < segp->masks.size(); ++i) {
            railhmi::core::MaskRef m;
            m.box_index = -1;
            m.width = segp->masks[i].width;
            m.height = segp->masks[i].height;
            m.stride_bytes = segp->masks[i].stride_bytes;
            m.plane = &segp->masks[i].plane;
            m.kind = "rail";
            masks.push_back(m);
        }
    } else {
        for (const auto& b : det.boxes) {
            boxes.push_back(b);
            in_roi.push_back(0);
        }
    }

    railhmi::core::EnvelopeBuildInput bin;
    bin.frame = out.envelope.frame;
    bin.profile = p;
    bin.model_version = out.envelope.model_version;
    bin.capture_mono_us = frame.capture_mono_us;
    bin.result_mono_us = mono_us();
    bin.detect_ready = det.ok;
    bin.seg_ready = seg_ok;
    bin.requires_segmentation = (p == railhmi::core::AiProfile::Front);
    bin.boxes = &boxes;
    bin.masks = &masks;
    bin.in_roi_flags = in_roi;
    out.envelope = railhmi::core::build_envelope(bin);
    out.available = out.envelope.ready();
    out.reason = out.envelope.reason;
    out.infer_us = out.envelope.result_mono_us - t_start;
    if (out.available) ++counters_.infer_ok;
    else ++counters_.infer_unavailable;
    return out;
}

std::vector<RknnVisionDetector::ModelStatus> RknnVisionDetector::models() const {
    std::vector<ModelStatus> out;
    for (const auto& kv : models_) {
        const Model& m = *kv.second;
        ModelStatus s;
        s.kind = m.kind;
        s.declared_name = m.decl.name;
        s.version = m.decl.version;
        s.path = m.path;
        s.present = m.desc.file_present;
        s.ready = m.ready;
        s.reason = m.reason;
        s.detail = m.detail;
        s.capability = m.check;
        s.runtime_api = m.desc.runtime_api_version;
        s.runtime_drv = m.desc.runtime_drv_version;
        s.output_count = static_cast<int>(m.desc.outputs.size());
        if (!m.desc.inputs.empty()) {
            const auto& in = m.desc.inputs[0];
            int c = 0, h = 0, w = 0;
            if (chw_of(in, &c, &h, &w)) {
                s.input_h = h;
                s.input_w = w;
            }
            s.input_dtype = dtype_name(in.type);
            s.input_scale = in.scale;
            s.input_zp = in.zp;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<RknnVisionDetector::ProfileStatus> RknnVisionDetector::profiles() const {
    const railhmi::core::AiProfile all[] = {railhmi::core::AiProfile::Front,
                                            railhmi::core::AiProfile::WheelRail,
                                            railhmi::core::AiProfile::Pantograph,
                                            railhmi::core::AiProfile::CabDoor};
    std::vector<ProfileStatus> out;
    for (auto p : all) {
        ProfileStatus s;
        s.profile = p;
        s.ready = ready(p);
        s.reason = s.ready ? railhmi::core::vision_reason::kReady() : unavailable_reason(p);
        s.model_version = model_version(p);
        if (p == railhmi::core::AiProfile::Front) {
            const railhmi::core::ProfileRequirement req = railhmi::core::requirement_for_profile(p);
            for (auto kind : req.required_kinds) {
                const Model* m = model_for(kind);
                if (m) s.model_names.push_back(m->decl.name);
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::string RknnVisionDetector::status_json() const {
    std::ostringstream o;
    o << "{\"engine\":\"rknn\"";
    o << ",\"models_dir\":\"" << json_escape(cfg_.models_dir) << "\"";
    o << ",\"models\":[";
    const auto ms = models();
    for (std::size_t i = 0; i < ms.size(); ++i) {
        const auto& m = ms[i];
        if (i) o << ",";
        o << "{";
        o << "\"kind\":\"" << railhmi::core::to_string(m.kind) << "\"";
        o << ",\"declared\":\"" << json_escape(m.declared_name) << "\"";
        o << ",\"version\":\"" << json_escape(m.version) << "\"";
        o << ",\"present\":" << (m.present ? "true" : "false");
        o << ",\"ready\":" << (m.ready ? "true" : "false");
        o << ",\"reason\":\"" << json_escape(m.reason) << "\"";
        o << ",\"detail\":\"" << json_escape(m.detail) << "\"";
        o << ",\"input_dtype\":\"" << json_escape(m.input_dtype) << "\"";
        o << ",\"input_h\":" << m.input_h << ",\"input_w\":" << m.input_w;
        o << ",\"input_scale\":" << m.input_scale << ",\"input_zp\":" << m.input_zp;
        o << ",\"output_count\":" << m.output_count;
        o << ",\"runtime_api\":\"" << json_escape(m.runtime_api) << "\"";
        o << ",\"runtime_drv\":\"" << json_escape(m.runtime_drv) << "\"";
        o << "}";
    }
    o << "],\"profiles\":[";
    const auto ps = profiles();
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i];
        if (i) o << ",";
        o << "{";
        o << "\"profile\":\"" << railhmi::core::to_string(p.profile) << "\"";
        o << ",\"ready\":" << (p.ready ? "true" : "false");
        o << ",\"reason\":\"" << json_escape(p.reason) << "\"";
        o << ",\"model_version\":\"" << json_escape(p.model_version) << "\"";
        o << "}";
    }
    o << "],\"counters\":{";
    o << "\"infer_calls\":" << counters_.infer_calls;
    o << ",\"infer_ok\":" << counters_.infer_ok;
    o << ",\"infer_unavailable\":" << counters_.infer_unavailable;
    o << ",\"det_boxes\":" << counters_.det_boxes;
    o << ",\"seg_boxes\":" << counters_.seg_boxes;
    o << ",\"mask_positive\":" << counters_.mask_positive;
    o << ",\"preprocess_failed\":" << counters_.preprocess_failed;
    o << ",\"infer_failed\":" << counters_.infer_failed;
    o << ",\"decode_failed\":" << counters_.decode_failed;
    o << ",\"last_pre_us\":" << counters_.last_pre_us;
    o << ",\"last_det_us\":" << counters_.last_det_us;
    o << ",\"last_seg_us\":" << counters_.last_seg_us;
    o << "},\"danger_zone\":\"" << json_escape(two_stage_cfg_.danger_zone_source) << "\"";
    // ITEM-010：三职责可用性语义（available/reason/state=unknown/evidence）与统计。
    o << ",\"duty_status\":" << railhmi::core::duty_status_json(duties());
    o << "}";
    return o.str();
}

}  // namespace media
}  // namespace railhmi
