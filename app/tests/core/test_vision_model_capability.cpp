// railhmi-core 视觉模型能力描述与就绪核验测试（视觉推理 ITEM-001）。
// 覆盖 TC-T-CAP-01/02/03/06 与 FI-AI-01/02/03 的纯逻辑路径。

#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/config.h"
#include "railhmi/core/vision_model_capability.h"

namespace {

using namespace railhmi::core;

TensorDesc mk(TensorDir dir, uint32_t index, const char* name, TensorFormat fmt,
              std::vector<uint32_t> dims, TensorType type = TensorType::Int8,
              QuantType qnt = QuantType::Affine, int32_t zp = -128, float scale = 0.1f) {
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

ModelDescriptor seg_desc() {
    ModelDescriptor d;
    d.path = "<fixture>/yolov8s-seg.rknn";
    d.file_name = "yolov8s-seg.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(mk(TensorDir::Input, 0, "images", TensorFormat::Nchw, {1, 3, 640, 640}));
    uint32_t i = 0;
    for (uint32_t s : {80u, 40u, 20u}) {
        d.outputs.push_back(mk(TensorDir::Output, i++, "box", TensorFormat::Nchw, {1, 64, s, s}));
        d.outputs.push_back(mk(TensorDir::Output, i++, "score", TensorFormat::Nchw, {1, 1, s, s}));
        d.outputs.push_back(mk(TensorDir::Output, i++, "score_sum", TensorFormat::Nchw, {1, 1, s, s}));
        d.outputs.push_back(mk(TensorDir::Output, i++, "seg", TensorFormat::Nchw, {1, 32, s, s}));
    }
    d.outputs.push_back(mk(TensorDir::Output, i++, "proto", TensorFormat::Nchw, {1, 32, 160, 160}));
    return d;
}

ModelDescriptor detect_desc() {
    ModelDescriptor d;
    d.path = "<fixture>/yolov5s.rknn";
    d.file_name = "yolov5s.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(mk(TensorDir::Input, 0, "images", TensorFormat::Nchw, {1, 3, 640, 640}));
    uint32_t i = 0;
    for (uint32_t s : {80u, 40u, 20u}) {
        d.outputs.push_back(mk(TensorDir::Output, i++, "output", TensorFormat::Nchw, {1, 255, s, s}));
    }
    return d;
}

void test_spec_mapping() {
    const ModelSpec seg = spec_for_declared("yolov8s-seg", AiProfile::Front);
    railhmi_test::check(seg.kind == VisionModelKind::YoloV8Seg, "spec yolov8s-seg -> seg");
    railhmi_test::check(seg.output_count == 13 && seg.seg_require_proto, "seg expects 13 + proto");
    const ModelSpec det = spec_for_declared("yolov5s", AiProfile::Front);
    railhmi_test::check(det.kind == VisionModelKind::YoloV5Detect, "spec yolov5s -> detect");
    railhmi_test::check(det.output_count == 3 && det.detect_classes == 80, "detect expects 3 scales/80 cls");
    const ModelSpec unk = spec_for_declared("mystery", AiProfile::Front);
    railhmi_test::check(unk.kind == VisionModelKind::Unknown, "unknown declared -> unknown kind");
}

void test_identify() {
    railhmi_test::check(identify_kind(seg_desc()) == VisionModelKind::YoloV8Seg, "identify seg");
    railhmi_test::check(identify_kind(detect_desc()) == VisionModelKind::YoloV5Detect, "identify detect");
    ModelDescriptor empty;
    railhmi_test::check(identify_kind(empty) == VisionModelKind::Unknown, "identify empty -> unknown");
}

void test_ready_paths() {
    const CapabilityCheck a = check_capability(seg_desc(), spec_for_declared("yolov8s-seg", AiProfile::Front));
    railhmi_test::check(a.ready, "seg ready");
    railhmi_test::check(a.reason == vision_reason::kReady(), "seg reason READY");
    const CapabilityCheck b = check_capability(detect_desc(), spec_for_declared("yolov5s", AiProfile::Front));
    railhmi_test::check(b.ready, "detect ready");
    railhmi_test::check(b.reason == vision_reason::kReady(), "detect reason READY");
}

void test_missing_and_runtime() {
    ModelDescriptor d = seg_desc();
    d.file_present = false;
    d.load_status = vision_reason::kModelMissing();
    const CapabilityCheck r = check_capability(d, spec_for_declared("yolov8s-seg", AiProfile::Front));
    railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMissing(), "FI-AI-01 MODEL_MISSING");

    ModelDescriptor d2 = seg_desc();
    d2.load_status = vision_reason::kRknnRuntimeUnavailable();
    const CapabilityCheck r2 = check_capability(d2, spec_for_declared("yolov8s-seg", AiProfile::Front));
    railhmi_test::check(!r2.ready && r2.reason == vision_reason::kRknnRuntimeUnavailable(),
                        "runtime unavailable reason");
}

void test_mismatches() {
    {
        ModelDescriptor d = seg_desc();
        d.outputs.pop_back();
        const CapabilityCheck r = check_capability(d, spec_for_declared("yolov8s-seg", AiProfile::Front));
        railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(),
                            "FI-AI-02 output count mismatch");
    }
    {
        ModelDescriptor d = detect_desc();
        d.inputs[0].type = TensorType::Float32;
        d.inputs[0].quant = QuantType::None;
        const CapabilityCheck r = check_capability(d, spec_for_declared("yolov5s", AiProfile::Front));
        railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(),
                            "FI-AI-03 unquantized input -> mismatch");
    }
    {
        // 声明 yolov5s 但结构是 seg：不得因文件存在/名称相符而 ready
        const CapabilityCheck r = check_capability(seg_desc(), spec_for_declared("yolov5s", AiProfile::Front));
        railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(),
                            "TC-T-CAP-03 declared vs structure mismatch");
    }
    {
        // 检测通道数不符
        ModelDescriptor d = detect_desc();
        d.outputs[0].dims = {1, 84, 80, 80};
        const CapabilityCheck r = check_capability(d, spec_for_declared("yolov5s", AiProfile::Front));
        railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(), "detect channel mismatch");
    }
    {
        // seg 缺 proto
        ModelDescriptor d = seg_desc();
        d.outputs.back().dims = {1, 32, 80, 80};
        const CapabilityCheck r = check_capability(d, spec_for_declared("yolov8s-seg", AiProfile::Front));
        railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(), "seg proto missing");
    }
    {
        // 输入尺寸不符
        ModelDescriptor d = seg_desc();
        d.inputs[0].dims = {1, 3, 320, 320};
        const CapabilityCheck r = check_capability(d, spec_for_declared("yolov8s-seg", AiProfile::Front));
        railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(), "seg input size mismatch");
    }
}

void test_profile_evidence() {
    railhmi_test::check(profile_accepts_kind(AiProfile::Front, VisionModelKind::YoloV8Seg), "front accepts seg");
    railhmi_test::check(profile_accepts_kind(AiProfile::Front, VisionModelKind::YoloV5Detect),
                        "front accepts detect");
    railhmi_test::check(!profile_accepts_kind(AiProfile::Pantograph, VisionModelKind::YoloV5Detect),
                        "pantograph no model evidence");
    railhmi_test::check(!profile_accepts_kind(AiProfile::WheelRail, VisionModelKind::YoloV8Seg),
                        "wheel_rail no model evidence");
    railhmi_test::check(!profile_accepts_kind(AiProfile::CabDoor, VisionModelKind::YoloV5Detect),
                        "cab_door no model evidence");
    railhmi_test::check(!profile_accepts_kind(AiProfile::None, VisionModelKind::YoloV5Detect),
                        "none never accepts");
    const ProfileRequirement req = requirement_for_profile(AiProfile::Front);
    railhmi_test::check(req.has_model_evidence && req.required_kinds.size() == 2, "front requires 2 kinds");
    railhmi_test::check(!requirement_for_profile(AiProfile::CabDoor).has_model_evidence,
                        "cab_door no evidence");
}

void test_declared_and_path() {
    AiConfig ai;
    AiModelConfig m1;
    m1.profile = AiProfile::Front;
    m1.name = "yolov5s";
    m1.version = "rknn-2.3.2/yolov5s";
    ai.models.push_back(m1);
    const std::vector<DeclaredModel> d = declared_models(ai);
    railhmi_test::check(d.size() == 1 && d[0].profile == AiProfile::Front && d[0].name == "yolov5s",
                        "declared_models from app.ai.models");
    railhmi_test::check(d[0].version == "rknn-2.3.2/yolov5s", "declared version preserved");

    railhmi_test::check(resolve_model_path("/opt/railhmi/models", "yolov5s") == "/opt/railhmi/models/yolov5s.rknn",
                        "resolve path appends .rknn");
    railhmi_test::check(resolve_model_path("/opt/railhmi/models/", "a.rknn") == "/opt/railhmi/models/a.rknn",
                        "resolve path keeps extension");
}

void test_text_roundtrip() {
    const ModelDescriptor d = seg_desc();
    const std::string text = model_descriptor_text(d);
    ModelDescriptor back;
    std::string err;
    railhmi_test::check(parse_model_descriptor_text(text, &back, &err), "descriptor text parses");
    railhmi_test::check(back.inputs.size() == d.inputs.size() && back.outputs.size() == d.outputs.size(),
                        "roundtrip tensor counts");
    railhmi_test::check(back.outputs.back().dims.size() == 4 && back.outputs.back().dims[1] == 32 &&
                            back.outputs.back().dims[2] == 160,
                        "roundtrip proto dims");
    railhmi_test::check(back.outputs[0].type == TensorType::Int8 && back.outputs[0].quant == QuantType::Affine,
                        "roundtrip dtype/quant");
    const CapabilityCheck r = check_capability(back, spec_for_declared("yolov8s-seg", AiProfile::Front));
    railhmi_test::check(r.ready, "roundtripped descriptor verifies ready");

    const std::string j = capability_check_json(r, back);
    railhmi_test::check(j.find("\"reason\":\"READY\"") != std::string::npos, "check json has reason");
    railhmi_test::check(j.find("\"ready\":true") != std::string::npos, "check json has ready");
}

void test_unsupported_declared() {
    const CapabilityCheck r = check_capability(detect_desc(), spec_for_declared("mystery", AiProfile::Front));
    railhmi_test::check(!r.ready && r.reason == vision_reason::kModelMismatch(),
                        "unknown declared name -> MODEL_MISMATCH");
}

}  // namespace

void run_vision_model_capability_tests() {
    test_spec_mapping();
    test_identify();
    test_ready_paths();
    test_missing_and_runtime();
    test_mismatches();
    test_profile_evidence();
    test_declared_and_path();
    test_text_roundtrip();
    test_unsupported_declared();
}
