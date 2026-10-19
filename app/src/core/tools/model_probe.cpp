// railhmi 视觉模型能力核验探针（视觉推理 ITEM-001）
//
// 用配置登记的模型名/版本（app.ai.models）与由 target 侧 `rknn_query` 产出的张量描述，
// 逐项核验输入/输出数量、形状、布局与量化后才置 ready；文件存在但结构不符/未核验、
// 模型缺失、运行时不可用各自返回稳定 reason。无 Qt/RKNN 依赖，可在最小工具链运行。
//
// 用法:
//   railhmi-model-probe --selftest [--json] [--out FILE]
//   railhmi-model-probe --descriptor FILE --declared NAME [--profile P] [--json] [--out FILE]
//   railhmi-model-probe --config DIR [--models-dir DIR] [--descriptor-map DIR] [--json] [--out FILE]
//
// 退出码:
//   0  验收通过（selftest 全部符合预期 / descriptor ready / config 模式无声明模型核验失败）
//   1  未通过（mismatch/缺失/运行时不可用；config 模式 --strict-profiles 下含缺种类）
//   2  参数或输入错误

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/vision_model_capability.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

namespace {

bool file_exists(const std::string& p) {
    if (p.empty()) return false;
    std::ifstream f(p, std::ios::binary);
    return f.good();
}

bool is_dir(const std::string& p) {
    if (p.empty()) return false;
    std::string probe = p;
    while (!probe.empty() && (probe.back() == '/' || probe.back() == '\\')) probe.pop_back();
    if (probe.empty()) return false;
    std::ifstream f(probe + "/.", std::ios::binary);
    return f.good();
}

std::string base_name(const std::string& p) {
    const size_t s = p.find_last_of("/\\");
    return (s == std::string::npos) ? p : p.substr(s + 1);
}

bool read_file(const std::string& p, std::string* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool parse_profile(const std::string& s, AiProfile* out) { return parse_ai_profile(s, out); }

// ---- 夹具构造（selftest；不读文件、不调用 rknn） ----

TensorDesc mk_tensor(TensorDir dir, uint32_t index, const char* name, TensorFormat fmt,
                     std::vector<uint32_t> dims, TensorType type, QuantType qnt, int32_t zp,
                     float scale) {
    TensorDesc t;
    t.dir = dir;
    t.index = index;
    t.name = name;
    t.n_dims = static_cast<uint32_t>(dims.size());
    t.dims = dims;
    t.type = type;
    t.fmt = fmt;
    t.quant = qnt;
    t.zp = zp;
    t.scale = scale;
    uint32_t n = 1;
    for (uint32_t d : dims) n *= d;
    t.n_elems = n;
    t.size = n;
    return t;
}

ModelDescriptor fixture_seg() {
    ModelDescriptor d;
    d.path = "<fixture>/yolov8s-seg.rknn";
    d.file_name = "yolov8s-seg.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(mk_tensor(TensorDir::Input, 0, "images", TensorFormat::Nchw, {1, 3, 640, 640},
                                 TensorType::Int8, QuantType::Affine, -128, 0.0078125f));
    uint32_t idx = 0;
    const uint32_t scales[3] = {80, 40, 20};
    for (uint32_t s : scales) {
        d.outputs.push_back(mk_tensor(TensorDir::Output, idx++, "box", TensorFormat::Nchw,
                                      {1, 64, s, s}, TensorType::Int8, QuantType::Affine, -128, 0.1f));
        d.outputs.push_back(mk_tensor(TensorDir::Output, idx++, "score", TensorFormat::Nchw,
                                      {1, 1, s, s}, TensorType::Int8, QuantType::Affine, -128, 0.1f));
        d.outputs.push_back(mk_tensor(TensorDir::Output, idx++, "score_sum", TensorFormat::Nchw,
                                      {1, 1, s, s}, TensorType::Int8, QuantType::Affine, -128, 0.1f));
        d.outputs.push_back(mk_tensor(TensorDir::Output, idx++, "seg", TensorFormat::Nchw,
                                      {1, 32, s, s}, TensorType::Int8, QuantType::Affine, -128, 0.1f));
    }
    d.outputs.push_back(mk_tensor(TensorDir::Output, idx++, "proto", TensorFormat::Nchw,
                                  {1, 32, 160, 160}, TensorType::Int8, QuantType::Affine, -128, 0.1f));
    return d;
}

ModelDescriptor fixture_detect() {
    ModelDescriptor d;
    d.path = "<fixture>/yolov5s.rknn";
    d.file_name = "yolov5s.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(mk_tensor(TensorDir::Input, 0, "images", TensorFormat::Nchw, {1, 3, 640, 640},
                                 TensorType::Int8, QuantType::Affine, -128, 0.0078125f));
    const uint32_t scales[3] = {80, 40, 20};
    uint32_t idx = 0;
    for (uint32_t s : scales) {
        d.outputs.push_back(mk_tensor(TensorDir::Output, idx++, "output", TensorFormat::Nchw,
                                      {1, 255, s, s}, TensorType::Int8, QuantType::Affine, -128, 0.1f));
    }
    return d;
}

struct SelfCase {
    const char* name;
    ModelDescriptor desc;
    std::string declared;
    AiProfile profile;
    bool expect_ready;
    const char* expect_reason;
    std::string note;
};

std::vector<SelfCase> build_self_cases() {
    std::vector<SelfCase> v;

    {
        SelfCase c;
        c.name = "TC-T-CAP-01/seg structure match -> ready";
        c.desc = fixture_seg();
        c.declared = "yolov8s-seg";
        c.profile = AiProfile::Front;
        c.expect_ready = true;
        c.expect_reason = vision_reason::kReady();
        v.push_back(c);
    }
    {
        SelfCase c;
        c.name = "TC-T-CAP-01/detect structure match -> ready";
        c.desc = fixture_detect();
        c.declared = "yolov5s";
        c.profile = AiProfile::Front;
        c.expect_ready = true;
        c.expect_reason = vision_reason::kReady();
        v.push_back(c);
    }
    {
        // FI-AI-01: 模型文件缺失
        SelfCase c;
        c.name = "FI-AI-01 model missing -> MODEL_MISSING";
        c.desc = fixture_seg();
        c.desc.file_present = false;
        c.desc.load_status = vision_reason::kModelMissing();
        c.declared = "yolov8s-seg";
        c.profile = AiProfile::Front;
        c.expect_ready = false;
        c.expect_reason = vision_reason::kModelMissing();
        v.push_back(c);
    }
    {
        // FI-AI-02: 张量数量/形状不匹配
        SelfCase c;
        c.name = "FI-AI-02 output count mismatch -> MODEL_MISMATCH";
        c.desc = fixture_seg();
        c.desc.outputs.pop_back();  // 12 outputs
        c.declared = "yolov8s-seg";
        c.profile = AiProfile::Front;
        c.expect_ready = false;
        c.expect_reason = vision_reason::kModelMismatch();
        v.push_back(c);
    }
    {
        // FI-AI-03: 量化参数异常（float 输入）
        SelfCase c;
        c.name = "FI-AI-03 unquantized input -> MODEL_MISMATCH";
        c.desc = fixture_detect();
        c.desc.inputs[0].type = TensorType::Float32;
        c.desc.inputs[0].quant = QuantType::None;
        c.declared = "yolov5s";
        c.profile = AiProfile::Front;
        c.expect_ready = false;
        c.expect_reason = vision_reason::kModelMismatch();
        v.push_back(c);
    }
    {
        // TC-T-CAP-03: 声明 yolov5s 但结构是 seg -> 不按文件名判 ready
        SelfCase c;
        c.name = "TC-T-CAP-03 declared name vs structure -> MODEL_MISMATCH";
        c.desc = fixture_seg();
        c.declared = "yolov5s";
        c.profile = AiProfile::Front;
        c.expect_ready = false;
        c.expect_reason = vision_reason::kModelMismatch();
        v.push_back(c);
    }
    {
        // 运行时不可用
        SelfCase c;
        c.name = "runtime unavailable -> RKNN_RUNTIME_UNAVAILABLE";
        c.desc = fixture_seg();
        c.desc.file_present = true;
        c.desc.load_status = vision_reason::kRknnRuntimeUnavailable();
        c.declared = "yolov8s-seg";
        c.profile = AiProfile::Front;
        c.expect_ready = false;
        c.expect_reason = vision_reason::kRknnRuntimeUnavailable();
        v.push_back(c);
    }
    {
        // 声明名未知
        SelfCase c;
        c.name = "unknown declared name -> MODEL_MISMATCH";
        c.desc = fixture_detect();
        c.declared = "mystery-model";
        c.profile = AiProfile::Front;
        c.expect_ready = false;
        c.expect_reason = vision_reason::kModelMismatch();
        v.push_back(c);
    }
    return v;
}

int run_selftest(bool json, const std::string& out) {
    const std::vector<SelfCase> cases = build_self_cases();
    int passed = 0, failed = 0;
    std::vector<std::string> lines;
    std::ostringstream j;
    j << "{\"mode\":\"selftest\",\"cases\":[";
    for (size_t i = 0; i < cases.size(); ++i) {
        const SelfCase& c = cases[i];
        const ModelSpec spec = spec_for_declared(c.declared, c.profile);
        const CapabilityCheck r = check_capability(c.desc, spec);
        const bool ok = (r.ready == c.expect_ready) && (r.reason == c.expect_reason);
        if (ok) ++passed; else ++failed;
        std::ostringstream line;
        line << (ok ? "[PASS] " : "[FAIL] ") << c.name << " -> ready=" << (r.ready ? "true" : "false")
             << " reason=" << r.reason << " (expect ready=" << (c.expect_ready ? "true" : "false")
             << " reason=" << c.expect_reason << ")";
        lines.push_back(line.str());
        if (i) j << ",";
        j << "{\"case\":\"" << c.name << "\",\"pass\":" << (ok ? "true" : "false")
          << ",\"ready\":" << (r.ready ? "true" : "false") << ",\"reason\":\"" << r.reason
          << "\",\"expected_ready\":" << (c.expect_ready ? "true" : "false")
          << ",\"expected_reason\":\"" << c.expect_reason << "\",\"check\":" << capability_check_json(r, c.desc)
          << "}";
    }
    j << "],\"passed\":" << passed << ",\"failed\":" << failed << "}";

    const std::string body = json ? j.str() : [&]() {
        std::string s = "type=selftest cases=" + std::to_string(cases.size()) + "\n";
        for (const auto& l : lines) s += l + "\n";
        s += "RESULT: " + std::string(failed == 0 ? "PASS" : "FAIL") +
             " passed=" + std::to_string(passed) + " failed=" + std::to_string(failed) + "\n";
        return s;
    }();

    std::printf("%s", body.c_str());
    if (!out.empty()) {
        std::ofstream f(out, std::ios::trunc);
        if (f) f << body;
        else std::fprintf(stderr, "cannot write %s\n", out.c_str());
    }
    return failed == 0 ? 0 : 1;
}

int run_descriptor(const std::string& path, const std::string& declared, const std::string& profile_s,
                   bool json, const std::string& out) {
    std::string text;
    if (!read_file(path, &text)) {
        std::fprintf(stderr, "cannot read descriptor: %s\n", path.c_str());
        return 2;
    }
    ModelDescriptor d;
    std::string err;
    if (!parse_model_descriptor_text(text, &d, &err)) {
        std::fprintf(stderr, "bad descriptor %s: %s\n", path.c_str(), err.c_str());
        return 2;
    }
    if (d.file_name.empty()) d.file_name = base_name(path);
    AiProfile profile = AiProfile::None;
    if (!profile_s.empty() && !parse_profile(profile_s, &profile)) {
        std::fprintf(stderr, "unknown profile: %s\n", profile_s.c_str());
        return 2;
    }
    const ModelSpec spec = spec_for_declared(declared, profile);
    const CapabilityCheck r = check_capability(d, spec);
    const bool accepted = profile_accepts_kind(profile, spec.kind);
    std::string reason = r.reason;
    std::string detail = r.detail;
    if (!accepted && profile != AiProfile::None) {
        reason = vision_reason::kProfileUnsupported();
        detail = "profile has no verified capability for kind " + std::string(to_string(spec.kind));
    }

    std::string body;
    if (json) {
        std::ostringstream j;
        j << "{\"mode\":\"descriptor\",\"declared\":\"" << declared << "\",\"profile\":\""
          << to_string(profile) << "\",\"ready\":" << ((r.ready && accepted) ? "true" : "false")
          << ",\"reason\":\"" << reason << "\",\"detail\":\"" << detail << "\",\"check\":"
          << capability_check_json(r, d) << "}";
        body = j.str();
    } else {
        std::ostringstream s;
        s << "type=descriptor declared=" << declared << " profile=" << to_string(profile)
          << " file=" << d.file_name << " file_present=" << (d.file_present ? 1 : 0)
          << " load_status=" << d.load_status << " runtime_api=" << d.runtime_api_version
          << " runtime_drv=" << d.runtime_drv_version << "\n";
        s << "ready=" << ((r.ready && accepted) ? 1 : 0) << " reason=" << reason
          << " detail=" << detail << " kind=" << to_string(r.kind)
          << " matched=" << r.matched.size() << " mismatched=" << r.mismatched.size() << "\n";
        for (const auto& m : r.matched) s << "  [ok] " << m << "\n";
        for (const auto& m : r.mismatched) s << "  [xx] " << m << "\n";
        body = s.str();
    }
    std::printf("%s", body.c_str());
    if (!out.empty()) {
        std::ofstream f(out, std::ios::trunc);
        if (f) f << body;
    }
    return (r.ready && accepted) ? 0 : 1;
}

std::string pick_models_dir(const std::string& config_dir, const std::string& explicit_dir) {
    if (!explicit_dir.empty()) return explicit_dir;
    if (const char* env = std::getenv("RAILHMI_MODELS_DIR")) {
        if (*env) return env;
    }
    const std::string candidates[] = {config_dir + "/../models", config_dir + "/../../models",
                                      "models"};
    for (const auto& c : candidates) {
        if (is_dir(c)) return c;
    }
    return config_dir + "/../models";
}

int run_config(const std::string& config_dir, const std::string& models_dir_arg,
               const std::string& descriptor_map, bool strict, bool json, const std::string& out) {
    const ConfigLoadResult cfg = load_config_dir(config_dir);
    if (!cfg.ok) {
        std::fprintf(stderr, "config invalid: %s\n", config_dir.c_str());
        for (const auto& e : cfg.errors) std::fprintf(stderr, "  %s\n", e.str().c_str());
        return 2;
    }
    const std::string models_dir = pick_models_dir(config_dir, models_dir_arg);
    const std::vector<DeclaredModel> declared = declared_models(cfg.settings.app.ai);

    std::vector<ModelAvailability> avail;
    int declared_failed = 0;   // profile 有证据但模型缺失/加载失败/结构不符
    int unsupported = 0;       // profile 无模型证据（预期态，非失败）
    int unverified = 0;        // 文件存在但未结构核验（无 rknn 描述）
    std::vector<std::string> lines;

    for (const auto& dm : declared) {
        ModelAvailability a;
        a.profile = dm.profile;
        a.model_name = dm.name;
        a.model_version = dm.version;
        a.path = resolve_model_path(models_dir, dm.name);
        a.file_present = file_exists(a.path);

        const ModelSpec spec = spec_for_declared(dm.name, dm.profile);
        // 声明名未知或 profile 无该类证据时，不因文件存在而 ready。
        std::string descriptor_file = descriptor_map.empty()
                                          ? std::string()
                                          : (descriptor_map + "/" + dm.name + ".descriptor.txt");
        ModelDescriptor desc;
        bool have_desc = false;
        if (!descriptor_file.empty() && file_exists(descriptor_file)) {
            std::string text, err;
            if (read_file(descriptor_file, &text) && parse_model_descriptor_text(text, &desc, &err)) {
                have_desc = true;
            } else {
                a.reason = vision_reason::kModelLoadFailed();
                a.detail = "descriptor unreadable: " + base_name(descriptor_file);
            }
        } else {
            desc.path = a.path;
            desc.file_name = base_name(a.path);
            desc.file_present = a.file_present;
            desc.load_status = a.file_present ? "ok" : vision_reason::kModelMissing();
        }

        if (a.reason.empty()) {
            if (!a.file_present && !have_desc) {
                a.reason = vision_reason::kModelMissing();
                a.detail = "model file not present at configured path";
            } else if (!profile_accepts_kind(dm.profile, spec.kind)) {
                a.reason = vision_reason::kProfileUnsupported();
                a.detail = "profile has no verified model evidence for this kind";
            } else if (!have_desc) {
                a.reason = vision_reason::kNotReady();
                a.detail = "file present; structure not verified (no rknn descriptor)";
            } else {
                const CapabilityCheck r = check_capability(desc, spec);
                a.available = r.ready;
                a.reason = r.reason;
                a.detail = r.detail;
            }
        }
        if (a.reason == vision_reason::kProfileUnsupported()) ++unsupported;
        else if (a.reason == vision_reason::kNotReady()) ++unverified;
        else if (!a.reason.empty() && a.reason != vision_reason::kReady()) ++declared_failed;
        avail.push_back(a);

        std::ostringstream l;
        l << "profile=" << to_string(dm.profile) << " model=" << dm.name
          << " version=" << dm.version << " path=" << a.path
          << " file_present=" << (a.file_present ? 1 : 0) << " available=" << (a.available ? 1 : 0)
          << " reason=" << a.reason << " detail=" << a.detail;
        lines.push_back(l.str());
    }

    // profile 汇总：需要哪些 kind，以及本轮是否满足。
    std::vector<std::string> profile_lines;
    bool profile_gap = false;
    const AiProfile profiles[] = {AiProfile::Front, AiProfile::Pantograph, AiProfile::WheelRail,
                                  AiProfile::CabDoor};
    for (AiProfile p : profiles) {
        const ProfileRequirement req = requirement_for_profile(p);
        std::vector<std::string> satisfied;
        for (VisionModelKind k : req.required_kinds) {
            bool ok = false;
            for (const auto& a : avail) {
                if (a.profile == p && a.available && spec_for_declared(a.model_name, p).kind == k) {
                    ok = true;
                    break;
                }
            }
            satisfied.push_back(std::string(to_string(k)) + "=" + (ok ? "ok" : "missing"));
            if (!ok) profile_gap = true;
        }
        std::ostringstream l;
        l << "profile." << to_string(p) << " evidence=" << (req.has_model_evidence ? 1 : 0)
          << " required=" << req.required_kinds.size();
        for (const auto& s : satisfied) l << " " << s;
        profile_lines.push_back(l.str());
    }

    std::string body;
    if (json) {
        std::ostringstream j;
        j << "{\"mode\":\"config\",\"config_dir\":\"" << config_dir << "\",\"models_dir\":\""
          << models_dir << "\",\"declared_count\":" << declared.size()
          << ",\"declared_failed\":" << declared_failed << ",\"unsupported_profiles\":" << unsupported
          << ",\"unverified\":" << unverified << ",\"profile_gap\":"
          << (profile_gap ? "true" : "false") << ",\"models\":" << model_availability_json(avail)
          << "}";
        body = j.str();
    } else {
        std::ostringstream s;
        s << "type=config config_dir=" << config_dir << " models_dir=" << models_dir
          << " declared=" << declared.size() << " declared_failed=" << declared_failed
          << " unsupported_profiles=" << unsupported << " unverified=" << unverified << "\n";
        for (const auto& l : lines) s << l << "\n";
        for (const auto& l : profile_lines) s << l << "\n";
        s << "RESULT: " << ((declared_failed == 0 && (!strict || !profile_gap)) ? "PASS" : "FAIL")
          << " declared_failed=" << declared_failed << " profile_gap=" << (profile_gap ? 1 : 0)
          << " (unsupported/unverified are expected states, see lines)\n";
        body = s.str();
    }
    std::printf("%s", body.c_str());
    if (!out.empty()) {
        std::ofstream f(out, std::ios::trunc);
        if (f) f << body;
    }
    if (declared_failed > 0) return 1;
    if (strict && profile_gap) return 1;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string config = RAILHMI_CONFIG_DIR;
    std::string models_dir;
    std::string descriptor;
    std::string declared;
    std::string profile;
    std::string descriptor_map;
    std::string out;
    bool json = false;
    bool selftest = false;
    bool strict = false;
    bool have_config = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* dst) {
            if (i + 1 < argc) *dst = argv[++i];
        };
        if (a == "--selftest") selftest = true;
        else if (a == "--config") { next(&config); have_config = true; }
        else if (a == "--models-dir") next(&models_dir);
        else if (a == "--descriptor") next(&descriptor);
        else if (a == "--declared") next(&declared);
        else if (a == "--profile") next(&profile);
        else if (a == "--descriptor-map") next(&descriptor_map);
        else if (a == "--out") next(&out);
        else if (a == "--json") json = true;
        else if (a == "--strict-profiles") strict = true;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage:\n"
                "  railhmi-model-probe --selftest [--json] [--out FILE]\n"
                "  railhmi-model-probe --descriptor FILE --declared NAME [--profile P] [--json] "
                "[--out FILE]\n"
                "  railhmi-model-probe --config DIR [--models-dir DIR] [--descriptor-map DIR] "
                "[--strict-profiles] [--json] [--out FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    if (selftest) return run_selftest(json, out);
    if (!descriptor.empty()) return run_descriptor(descriptor, declared, profile, json, out);
    if (have_config) return run_config(config, models_dir, descriptor_map, strict, json, out);
    std::fprintf(stderr, "nothing to do; use --selftest or --descriptor or --config (see --help)\n");
    return 2;
}
