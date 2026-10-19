// railhmi-core: 视觉模型能力描述与就绪核验实现（视觉推理 ITEM-001）。
// 纯逻辑，仅标准库；见头文件说明。

#include "railhmi/core/vision_model_capability.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>

namespace railhmi {
namespace core {

const char* to_string(TensorType v) {
    switch (v) {
        case TensorType::Float32: return "float32";
        case TensorType::Float16: return "float16";
        case TensorType::Int8: return "int8";
        case TensorType::Uint8: return "uint8";
        case TensorType::Int16: return "int16";
        case TensorType::Uint16: return "uint16";
        case TensorType::Int32: return "int32";
        case TensorType::Uint32: return "uint32";
        case TensorType::Int64: return "int64";
        case TensorType::Bool: return "bool";
        case TensorType::Unknown:
        default: return "unknown";
    }
}

const char* to_string(TensorFormat v) {
    switch (v) {
        case TensorFormat::Nchw: return "nchw";
        case TensorFormat::Nhwc: return "nhwc";
        case TensorFormat::Nc1hwc2: return "nc1hwc2";
        case TensorFormat::Undefined: return "undefined";
        case TensorFormat::Unknown:
        default: return "unknown";
    }
}

const char* to_string(QuantType v) {
    switch (v) {
        case QuantType::None: return "none";
        case QuantType::Dfp: return "dfp";
        case QuantType::Affine: return "affine";
        case QuantType::LayerNorm: return "layernorm";
        case QuantType::Unsupported:
        default: return "unsupported";
    }
}

const char* to_string(VisionModelKind v) {
    switch (v) {
        case VisionModelKind::YoloV5Detect: return "yolov5_detect";
        case VisionModelKind::YoloV8Seg: return "yolov8_seg";
        case VisionModelKind::Unknown:
        default: return "unknown";
    }
}

bool parse_tensor_type(const std::string& s, TensorType* out) {
    static const struct { const char* n; TensorType v; } kMap[] = {
        {"float32", TensorType::Float32}, {"float16", TensorType::Float16},
        {"int8", TensorType::Int8},       {"uint8", TensorType::Uint8},
        {"int16", TensorType::Int16},     {"uint16", TensorType::Uint16},
        {"int32", TensorType::Int32},     {"uint32", TensorType::Uint32},
        {"int64", TensorType::Int64},     {"bool", TensorType::Bool},
        {"unknown", TensorType::Unknown},
    };
    for (const auto& e : kMap) {
        if (s == e.n) { *out = e.v; return true; }
    }
    return false;
}

bool parse_tensor_format(const std::string& s, TensorFormat* out) {
    if (s == "nchw") { *out = TensorFormat::Nchw; return true; }
    if (s == "nhwc") { *out = TensorFormat::Nhwc; return true; }
    if (s == "nc1hwc2") { *out = TensorFormat::Nc1hwc2; return true; }
    if (s == "undefined") { *out = TensorFormat::Undefined; return true; }
    if (s == "unknown") { *out = TensorFormat::Unknown; return true; }
    return false;
}

bool parse_quant_type(const std::string& s, QuantType* out) {
    if (s == "none") { *out = QuantType::None; return true; }
    if (s == "dfp") { *out = QuantType::Dfp; return true; }
    if (s == "affine") { *out = QuantType::Affine; return true; }
    if (s == "layernorm") { *out = QuantType::LayerNorm; return true; }
    if (s == "unsupported") { *out = QuantType::Unsupported; return true; }
    return false;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// 归一到 (channels, height, width)。格式未知时用可判定的启发式，无法判定返回 false。
bool normalize_chw(const TensorDesc& t, uint32_t* c, uint32_t* h, uint32_t* w, bool* ok) {
    *ok = false;
    if (t.n_dims != 4 || t.dims.size() < 4) return false;
    const uint32_t d0 = t.dims[0], d1 = t.dims[1], d2 = t.dims[2], d3 = t.dims[3];
    auto plausible = [](uint32_t ch, uint32_t hh, uint32_t ww) {
        return ch > 0 && hh > 0 && ww > 0 && ch <= 4096;
    };
    if (t.fmt == TensorFormat::Nchw) {
        *c = d1; *h = d2; *w = d3; *ok = plausible(d1, d2, d3); return *ok;
    }
    if (t.fmt == TensorFormat::Nhwc) {
        *c = d3; *h = d1; *w = d2; *ok = plausible(d3, d1, d2); return *ok;
    }
    // 格式未知：优先能形成方形空间维的解释。
    if (d2 == d3 && plausible(d1, d2, d3)) { *c = d1; *h = d2; *w = d3; *ok = true; return true; }
    if (d1 == d2 && plausible(d3, d1, d2)) { *c = d3; *h = d1; *w = d2; *ok = true; return true; }
    (void)d0;
    return false;
}

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
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

std::string num_f(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

std::string dims_join(const std::vector<uint32_t>& d) {
    std::string o;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i) o += ",";
        o += std::to_string(d[i]);
    }
    return o;
}

bool split_kv(const std::string& line, std::string* k, std::string* v) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    *k = line.substr(0, eq);
    *v = line.substr(eq + 1);
    return true;
}

bool parse_dims_csv(const std::string& s, std::vector<uint32_t>* out) {
    out->clear();
    if (s.empty()) return true;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        try {
            const unsigned long v = std::stoul(tok);
            out->push_back(static_cast<uint32_t>(v));
        } catch (...) {
            return false;
        }
    }
    return true;
}

}  // namespace

ModelSpec spec_for_declared(const std::string& declared_name, AiProfile profile) {
    ModelSpec s;
    s.declared_name = declared_name;
    const std::string n = lower(declared_name);
    if (contains(n, "yolov8") && contains(n, "seg")) {
        s.kind = VisionModelKind::YoloV8Seg;
        s.input_count = 1;
        s.output_count = 13;
        s.in_c = 3;
        s.in_h = 640;
        s.in_w = 640;
        s.in_must_be_quantized = true;
        s.seg_require_proto = true;
        s.seg_proto_channels = 32;
        s.seg_proto_h = 160;
        s.seg_proto_w = 160;
        s.seg_class_count = 1;  // rail（models/rail_labels.txt）
        return s;
    }
    if (contains(n, "yolov5")) {
        s.kind = VisionModelKind::YoloV5Detect;
        s.input_count = 1;
        s.output_count = 3;
        s.in_c = 3;
        s.in_h = 640;
        s.in_w = 640;
        s.in_must_be_quantized = true;
        s.detect_classes = 80;  // COCO（code/yolov5_postprocess.h YOLOV5_OBJ_CLASS_NUM=80）
        return s;
    }
    (void)profile;
    s.kind = VisionModelKind::Unknown;
    return s;
}

VisionModelKind identify_kind(const ModelDescriptor& d, std::string* detail) {
    if (detail) detail->clear();
    if (!d.inputs.empty() && d.outputs.size() == 13) {
        // 需要恰好一个 32 通道 160x160 的 proto 张量。
        int proto = 0;
        for (const auto& t : d.outputs) {
            uint32_t c = 0, h = 0, w = 0;
            bool ok = false;
            if (normalize_chw(t, &c, &h, &w, &ok) && ok && c == 32 && h == 160 && w == 160) ++proto;
        }
        if (proto == 1) {
            if (detail) *detail = "13 outputs with 1 proto(32x160x160)";
            return VisionModelKind::YoloV8Seg;
        }
    }
    if (!d.inputs.empty() && d.outputs.size() == 3) {
        bool all_ok = true;
        for (const auto& t : d.outputs) {
            uint32_t c = 0, h = 0, w = 0;
            bool ok = false;
            if (!normalize_chw(t, &c, &h, &w, &ok) || !ok || h != w) { all_ok = false; break; }
        }
        if (all_ok) {
            if (detail) *detail = "3 output scales";
            return VisionModelKind::YoloV5Detect;
        }
    }
    if (detail) *detail = "no known structure; outputs=" + std::to_string(d.outputs.size());
    return VisionModelKind::Unknown;
}

namespace {

void check_inputs(const ModelDescriptor& d, const ModelSpec& spec, CapabilityCheck* r) {
    if (spec.input_count >= 0) {
        if (static_cast<int>(d.inputs.size()) == spec.input_count) {
            r->matched.push_back("input.count=" + std::to_string(d.inputs.size()));
        } else {
            r->mismatched.push_back("input.count expected=" + std::to_string(spec.input_count) +
                                    " actual=" + std::to_string(d.inputs.size()));
        }
    }
    for (const auto& t : d.inputs) {
        uint32_t c = 0, h = 0, w = 0;
        bool ok = false;
        const bool norm = normalize_chw(t, &c, &h, &w, &ok) && ok;
        const std::string tag = "input[" + std::to_string(t.index) + "]";
        if (spec.in_c >= 0 || spec.in_h >= 0 || spec.in_w >= 0) {
            if (!norm) {
                r->mismatched.push_back(tag + ".dims unrecognized n_dims=" + std::to_string(t.n_dims));
            } else if ((spec.in_c < 0 || static_cast<int>(c) == spec.in_c) &&
                       (spec.in_h < 0 || static_cast<int>(h) == spec.in_h) &&
                       (spec.in_w < 0 || static_cast<int>(w) == spec.in_w)) {
                r->matched.push_back(tag + ".dims=" + std::to_string(c) + "x" +
                                     std::to_string(h) + "x" + std::to_string(w));
            } else {
                r->mismatched.push_back(tag + ".dims expected=" + std::to_string(spec.in_c) + "x" +
                                        std::to_string(spec.in_h) + "x" + std::to_string(spec.in_w) +
                                        " actual=" + std::to_string(c) + "x" + std::to_string(h) +
                                        "x" + std::to_string(w));
            }
        }
        if (spec.in_must_be_quantized) {
            const bool dtype_ok = (t.type == TensorType::Int8 || t.type == TensorType::Uint8);
            const bool qnt_ok = (t.quant == QuantType::Affine);
            if (dtype_ok && qnt_ok) {
                r->matched.push_back(tag + ".quant=" + to_string(t.type) + "/" + to_string(t.quant) +
                                     " zp=" + std::to_string(t.zp) + " scale=" + num_f(t.scale));
            } else {
                r->mismatched.push_back(tag + ".quant expected=int8|uint8/affine actual=" +
                                        to_string(t.type) + "/" + to_string(t.quant));
            }
        }
    }
}

void check_yolov5_outputs(const ModelDescriptor& d, const ModelSpec& spec, CapabilityCheck* r) {
    if (static_cast<int>(d.outputs.size()) != spec.output_count) {
        r->mismatched.push_back("output.count expected=" + std::to_string(spec.output_count) +
                                " actual=" + std::to_string(d.outputs.size()));
        return;
    }
    std::set<uint32_t> sides;
    uint32_t expect_c = 0;
    if (spec.detect_classes >= 0) expect_c = static_cast<uint32_t>(3 * (5 + spec.detect_classes));
    for (const auto& t : d.outputs) {
        uint32_t c = 0, h = 0, w = 0;
        bool ok = false;
        const std::string tag = "output[" + std::to_string(t.index) + "]";
        if (!normalize_chw(t, &c, &h, &w, &ok) || !ok) {
            r->mismatched.push_back(tag + ".dims unrecognized");
            continue;
        }
        if (h != w) {
            r->mismatched.push_back(tag + ".spatial not square h=" + std::to_string(h) +
                                    " w=" + std::to_string(w));
            continue;
        }
        if (expect_c != 0 && c != expect_c) {
            r->mismatched.push_back(tag + ".channels expected=" + std::to_string(expect_c) +
                                    " actual=" + std::to_string(c));
            continue;
        }
        sides.insert(h);
        r->matched.push_back(tag + ".head=c" + std::to_string(c) + " " + std::to_string(h) + "x" +
                             std::to_string(w));
    }
    if (sides.size() == 3) {
        r->matched.push_back("output.scales=" + std::to_string(sides.size()));
    } else {
        r->mismatched.push_back("output.scales expected=3 actual=" + std::to_string(sides.size()));
    }
}

void check_yolov8seg_outputs(const ModelDescriptor& d, const ModelSpec& spec, CapabilityCheck* r) {
    if (static_cast<int>(d.outputs.size()) != spec.output_count) {
        r->mismatched.push_back("output.count expected=" + std::to_string(spec.output_count) +
                                " actual=" + std::to_string(d.outputs.size()));
        return;
    }
    // 分组：proto（32x160x160）1 个；其余按空间尺寸分 3 组、每组 4 个。
    std::vector<const TensorDesc*> branch;
    int proto = 0;
    for (const auto& t : d.outputs) {
        uint32_t c = 0, h = 0, w = 0;
        bool ok = false;
        const std::string tag = "output[" + std::to_string(t.index) + "]";
        if (!normalize_chw(t, &c, &h, &w, &ok) || !ok) {
            r->mismatched.push_back(tag + ".dims unrecognized n_dims=" + std::to_string(t.n_dims));
            continue;
        }
        if (c == static_cast<uint32_t>(spec.seg_proto_channels) &&
            h == static_cast<uint32_t>(spec.seg_proto_h) &&
            w == static_cast<uint32_t>(spec.seg_proto_w)) {
            ++proto;
            r->matched.push_back(tag + ".proto=" + std::to_string(c) + "x" + std::to_string(h) + "x" +
                                 std::to_string(w));
            continue;
        }
        branch.push_back(&t);
    }
    if (proto == 1) {
        r->matched.push_back("output.proto.count=1");
    } else {
        r->mismatched.push_back("output.proto.count expected=1 actual=" + std::to_string(proto));
    }
    if (branch.size() != 12) {
        r->mismatched.push_back("output.branch.count expected=12 actual=" +
                                std::to_string(branch.size()));
        return;
    }
    // 按空间尺寸分组。
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>>> groups;
    for (const TensorDesc* t : branch) {
        uint32_t c = 0, h = 0, w = 0;
        bool ok = false;
        if (!normalize_chw(*t, &c, &h, &w, &ok) || !ok) continue;
        bool found = false;
        for (auto& g : groups) {
            if (g.first.first == h && g.first.second == w) { g.second.push_back(c); found = true; break; }
        }
        if (!found) groups.push_back({{h, w}, {c}});
    }
    if (groups.size() != 3) {
        r->mismatched.push_back("output.branch.scales expected=3 actual=" + std::to_string(groups.size()));
        return;
    }
    r->matched.push_back("output.branch.scales=3");
    for (const auto& g : groups) {
        std::vector<uint32_t> chans = g.second;
        std::sort(chans.begin(), chans.end());
        const std::string tag = "branch[" + std::to_string(g.first.first) + "x" +
                                std::to_string(g.first.second) + "]";
        if (chans.size() != 4) {
            r->mismatched.push_back(tag + ".tensors expected=4 actual=" + std::to_string(chans.size()));
            continue;
        }
        // 期望多重集：{4*dfl(64), nc, 1, 32}；nc 未知时只校验 64/1/32 与“一个类别通道”。
        const uint32_t expect_box = 64;
        bool box = false, seg = false;
        int one_count = 0;
        for (uint32_t c : chans) {
            if (c == expect_box) box = true;
            else if (c == static_cast<uint32_t>(spec.seg_proto_channels)) seg = true;
            else if (c == 1) ++one_count;
        }
        if (!box) r->mismatched.push_back(tag + ".box.channels expected=" + std::to_string(expect_box));
        if (!seg) r->mismatched.push_back(tag + ".seg.channels expected=" +
                                          std::to_string(spec.seg_proto_channels));
        if (one_count < 1) r->mismatched.push_back(tag + ".score.channels missing");
        if (box && seg && one_count >= 1) {
            r->matched.push_back(tag + ".channels=ok(" + dims_join(chans) + ")");
        }
    }
}

}  // namespace

CapabilityCheck check_capability(const ModelDescriptor& d, const ModelSpec& spec) {
    CapabilityCheck r;
    r.kind = spec.kind;
    if (!d.file_present || d.load_status == vision_reason::kModelMissing()) {
        r.reason = vision_reason::kModelMissing();
        r.detail = "model file not present";
        return r;
    }
    if (d.load_status == vision_reason::kRknnRuntimeUnavailable()) {
        r.reason = vision_reason::kRknnRuntimeUnavailable();
        r.detail = "librknnrt not loadable in this environment";
        return r;
    }
    if (d.load_status != "ok") {
        r.reason = vision_reason::kModelLoadFailed();
        r.detail = "load_status=" + d.load_status;
        return r;
    }
    if (spec.kind == VisionModelKind::Unknown) {
        r.reason = vision_reason::kModelMismatch();
        r.detail = "declared model name does not map to a verified spec";
        return r;
    }
    // 结构识别必须与声明种类一致（不按文件名判定 ready）。
    const VisionModelKind identified = identify_kind(d);
    if (identified != spec.kind) {
        r.reason = vision_reason::kModelMismatch();
        r.detail = std::string("declared=") + to_string(spec.kind) + " identified=" +
                   to_string(identified) + " (structure mismatch, file name ignored)";
        return r;
    }
    r.matched.push_back(std::string("identify=") + to_string(identified));

    check_inputs(d, spec, &r);
    if (spec.kind == VisionModelKind::YoloV5Detect) {
        check_yolov5_outputs(d, spec, &r);
    } else if (spec.kind == VisionModelKind::YoloV8Seg) {
        check_yolov8seg_outputs(d, spec, &r);
    }

    if (r.mismatched.empty()) {
        r.ready = true;
        r.reason = vision_reason::kReady();
        r.detail = "structure verified";
    } else {
        r.ready = false;
        r.reason = vision_reason::kModelMismatch();
        r.detail = std::to_string(r.mismatched.size()) + " mismatch item(s)";
    }
    return r;
}

bool profile_accepts_kind(AiProfile p, VisionModelKind kind) {
    if (kind == VisionModelKind::Unknown) return false;
    switch (p) {
        case AiProfile::Front:
            return kind == VisionModelKind::YoloV8Seg || kind == VisionModelKind::YoloV5Detect;
        case AiProfile::Pantograph:
        case AiProfile::WheelRail:
        case AiProfile::CabDoor:
        case AiProfile::None:
        default:
            return false;  // 本轮无专用模型证据
    }
}

ProfileRequirement requirement_for_profile(AiProfile p) {
    ProfileRequirement r;
    r.profile = p;
    if (p == AiProfile::Front) {
        r.required_kinds = {VisionModelKind::YoloV8Seg, VisionModelKind::YoloV5Detect};
        r.has_model_evidence = true;
    }
    return r;
}

std::vector<DeclaredModel> declared_models(const AiConfig& ai) {
    std::vector<DeclaredModel> out;
    for (const auto& m : ai.models) {
        DeclaredModel d;
        d.profile = m.profile;
        d.name = m.name;
        d.version = m.version;
        out.push_back(d);
    }
    return out;
}

std::string resolve_model_path(const std::string& models_dir, const std::string& name) {
    if (name.empty()) return std::string();
    std::string dir = models_dir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    std::string file = name;
    if (file.size() < 5 || lower(file.substr(file.size() - 5)) != ".rknn") file += ".rknn";
    if (dir.empty()) return file;
    return dir + "/" + file;
}

std::string model_descriptor_text(const ModelDescriptor& d) {
    std::ostringstream o;
    o << "descriptor_version=1\n";
    o << "model.path=" << d.path << "\n";
    o << "model.file=" << d.file_name << "\n";
    o << "model.file_present=" << (d.file_present ? 1 : 0) << "\n";
    o << "model.load_status=" << d.load_status << "\n";
    o << "model.runtime.api_version=" << d.runtime_api_version << "\n";
    o << "model.runtime.drv_version=" << d.runtime_drv_version << "\n";
    o << "input.count=" << d.inputs.size() << "\n";
    o << "output.count=" << d.outputs.size() << "\n";
    auto emit = [&o](const TensorDesc& t) {
        o << "tensor.dir=" << (t.dir == TensorDir::Input ? "input" : "output") << "\n";
        o << "tensor.index=" << t.index << "\n";
        o << "tensor.name=" << t.name << "\n";
        o << "tensor.n_dims=" << t.n_dims << "\n";
        o << "tensor.dims=" << dims_join(t.dims) << "\n";
        o << "tensor.extra_dims=" << t.extra_dims << "\n";
        o << "tensor.type=" << to_string(t.type) << "\n";
        o << "tensor.fmt=" << to_string(t.fmt) << "\n";
        o << "tensor.qnt=" << to_string(t.quant) << "\n";
        o << "tensor.zp=" << t.zp << "\n";
        o << "tensor.scale=" << num_f(t.scale) << "\n";
        o << "tensor.n_elems=" << t.n_elems << "\n";
        o << "tensor.size=" << t.size << "\n";
        o << "\n";
    };
    for (const auto& t : d.inputs) emit(t);
    for (const auto& t : d.outputs) emit(t);
    return o.str();
}

bool parse_model_descriptor_text(const std::string& text, ModelDescriptor* out, std::string* err) {
    if (!out) return false;
    *out = ModelDescriptor{};
    if (err) err->clear();
    TensorDesc cur;
    bool have_cur = false;
    std::istringstream in(text);
    std::string line;
    auto flush = [&]() {
        if (!have_cur) return;
        if (cur.dir == TensorDir::Input) out->inputs.push_back(cur);
        else out->outputs.push_back(cur);
        have_cur = false;
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string k, v;
        if (!split_kv(line, &k, &v)) continue;
        if (k == "model.path") out->path = v;
        else if (k == "model.file") out->file_name = v;
        else if (k == "model.file_present") out->file_present = (v == "1" || lower(v) == "true");
        else if (k == "model.load_status") out->load_status = v;
        else if (k == "model.runtime.api_version") out->runtime_api_version = v;
        else if (k == "model.runtime.drv_version") out->runtime_drv_version = v;
        else if (k == "tensor.dir") {
            flush();
            cur = TensorDesc{};
            cur.dir = (lower(v) == "input") ? TensorDir::Input : TensorDir::Output;
            have_cur = true;
        } else if (!have_cur) {
            continue;
        } else if (k == "tensor.index") {
            cur.index = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        } else if (k == "tensor.name") {
            cur.name = v;
        } else if (k == "tensor.n_dims") {
            cur.n_dims = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        } else if (k == "tensor.dims") {
            if (!parse_dims_csv(v, &cur.dims)) {
                if (err) *err = "bad tensor.dims: " + v;
                return false;
            }
        } else if (k == "tensor.extra_dims") {
            cur.extra_dims = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        } else if (k == "tensor.type") {
            if (!parse_tensor_type(lower(v), &cur.type)) cur.type = TensorType::Unknown;
        } else if (k == "tensor.fmt") {
            if (!parse_tensor_format(lower(v), &cur.fmt)) cur.fmt = TensorFormat::Unknown;
        } else if (k == "tensor.qnt") {
            if (!parse_quant_type(lower(v), &cur.quant)) cur.quant = QuantType::Unsupported;
        } else if (k == "tensor.zp") {
            cur.zp = static_cast<int32_t>(std::strtol(v.c_str(), nullptr, 10));
        } else if (k == "tensor.scale") {
            cur.scale = std::strtof(v.c_str(), nullptr);
        } else if (k == "tensor.n_elems") {
            cur.n_elems = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        } else if (k == "tensor.size") {
            cur.size = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        }
    }
    flush();
    if (out->inputs.empty() && out->outputs.empty() && out->load_status.empty()) {
        if (err) *err = "no descriptor content";
        return false;
    }
    return true;
}

std::string model_descriptor_json(const ModelDescriptor& d) {
    std::ostringstream o;
    o << "{";
    o << "\"path\":\"" << json_escape(d.path) << "\",";
    o << "\"file\":\"" << json_escape(d.file_name) << "\",";
    o << "\"file_present\":" << (d.file_present ? "true" : "false") << ",";
    o << "\"load_status\":\"" << json_escape(d.load_status) << "\",";
    o << "\"runtime\":{\"api_version\":\"" << json_escape(d.runtime_api_version)
      << "\",\"drv_version\":\"" << json_escape(d.runtime_drv_version) << "\"},";
    auto tensors_json = [&o](const std::vector<TensorDesc>& ts) {
        o << "[";
        for (size_t i = 0; i < ts.size(); ++i) {
            const TensorDesc& t = ts[i];
            if (i) o << ",";
            o << "{\"index\":" << t.index << ",\"name\":\"" << json_escape(t.name) << "\",";
            o << "\"n_dims\":" << t.n_dims << ",\"dims\":[";
            for (size_t j = 0; j < t.dims.size(); ++j) {
                if (j) o << ",";
                o << t.dims[j];
            }
            o << "],\"type\":\"" << to_string(t.type) << "\",";
            o << "\"fmt\":\"" << to_string(t.fmt) << "\",";
            o << "\"qnt\":\"" << to_string(t.quant) << "\",";
            o << "\"zp\":" << t.zp << ",\"scale\":" << num_f(t.scale) << ",";
            o << "\"n_elems\":" << t.n_elems << ",\"size\":" << t.size << "}";
        }
        o << "]";
    };
    o << "\"inputs\":";
    tensors_json(d.inputs);
    o << ",\"outputs\":";
    tensors_json(d.outputs);
    o << "}";
    return o.str();
}

std::string capability_check_json(const CapabilityCheck& c, const ModelDescriptor& d) {
    std::ostringstream o;
    o << "{";
    o << "\"ready\":" << (c.ready ? "true" : "false") << ",";
    o << "\"reason\":\"" << json_escape(c.reason) << "\",";
    o << "\"kind\":\"" << to_string(c.kind) << "\",";
    o << "\"detail\":\"" << json_escape(c.detail) << "\",";
    o << "\"matched\":[";
    for (size_t i = 0; i < c.matched.size(); ++i) {
        if (i) o << ",";
        o << "\"" << json_escape(c.matched[i]) << "\"";
    }
    o << "],\"mismatched\":[";
    for (size_t i = 0; i < c.mismatched.size(); ++i) {
        if (i) o << ",";
        o << "\"" << json_escape(c.mismatched[i]) << "\"";
    }
    o << "],\"descriptor\":" << model_descriptor_json(d);
    o << "}";
    return o.str();
}

std::string model_availability_json(const std::vector<ModelAvailability>& items) {
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < items.size(); ++i) {
        const ModelAvailability& m = items[i];
        if (i) o << ",";
        o << "{\"profile\":\"" << to_string(m.profile) << "\",";
        o << "\"model_name\":\"" << json_escape(m.model_name) << "\",";
        o << "\"model_version\":\"" << json_escape(m.model_version) << "\",";
        o << "\"path\":\"" << json_escape(m.path) << "\",";
        o << "\"file_present\":" << (m.file_present ? "true" : "false") << ",";
        o << "\"available\":" << (m.available ? "true" : "false") << ",";
        o << "\"reason\":\"" << json_escape(m.reason) << "\",";
        o << "\"detail\":\"" << json_escape(m.detail) << "\"}";
    }
    o << "]";
    return o.str();
}

}  // namespace core
}  // namespace railhmi
