// railhmi-core: 无相机自检与可归档夹具实现（视觉推理 ITEM-013）。
//
// 本文件只使用确定性夹具：合成像素/合成张量/合成描述符，不加载 RKNN、不连接相机。
// 每个判例把关键数值写入 evidence，便于归档后独立比对（README/证据文档引用）。

#include "railhmi/core/vision_selftest.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "railhmi/core/vision_debug_view.h"
#include "railhmi/core/vision_detect_postprocess.h"
#include "railhmi/core/vision_distance.h"
#include "railhmi/core/vision_duty_capability.h"
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_model_capability.h"
#include "railhmi/core/vision_preprocess.h"
#include "railhmi/core/vision_semantic_adapter.h"
#include "railhmi/core/vision_seg_postprocess.h"
#include "railhmi/core/vision_two_stage.h"

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

std::string num(double v, int precision = 4) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
    return buf;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// ---- 夹具构造 ----

TensorDesc input_tensor(int h, int w, TensorType type = TensorType::Int8) {
    TensorDesc t;
    t.dir = TensorDir::Input;
    t.index = 0;
    t.name = "images";
    t.n_dims = 4;
    t.dims = {1, static_cast<uint32_t>(h), static_cast<uint32_t>(w), 3};
    t.type = type;
    t.fmt = TensorFormat::Nhwc;
    t.quant = type == TensorType::Float32 ? QuantType::None : QuantType::Affine;
    t.zp = type == TensorType::Float32 ? 0 : -128;
    t.scale = 0.00392157f;
    t.n_elems = static_cast<uint32_t>(h) * static_cast<uint32_t>(w) * 3u;
    return t;
}

TensorDesc output_tensor(int c, int h, int w, TensorType type = TensorType::Float32) {
    TensorDesc t;
    t.dir = TensorDir::Output;
    t.name = "out";
    t.n_dims = 4;
    t.dims = {1, static_cast<uint32_t>(c), static_cast<uint32_t>(h), static_cast<uint32_t>(w)};
    t.type = type;
    t.fmt = TensorFormat::Nchw;
    t.quant = type == TensorType::Float32 ? QuantType::None : QuantType::Affine;
    t.n_elems = static_cast<uint32_t>(c) * static_cast<uint32_t>(h) * static_cast<uint32_t>(w);
    return t;
}

// 与 yolov5s 声明匹配（1 输入 640×640 int8 NHWC + 3 输出 255×{80,40,20}）。
ModelDescriptor desc_yolov5_ok() {
    ModelDescriptor d;
    d.path = "fixture/yolov5s.rknn";
    d.file_name = "yolov5s.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(input_tensor(640, 640));
    d.outputs.push_back(output_tensor(255, 80, 80));
    d.outputs.push_back(output_tensor(255, 40, 40));
    d.outputs.push_back(output_tensor(255, 20, 20));
    return d;
}

// 与 yolov8s-seg 声明匹配（13 输出，含 proto 32×160×160）。
ModelDescriptor desc_yolov8s_seg_ok() {
    ModelDescriptor d;
    d.path = "fixture/yolov8s-seg.rknn";
    d.file_name = "yolov8s-seg.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(input_tensor(640, 640));
    for (int s = 0; s < 3; ++s) {
        const int g = 80 >> s;
        d.outputs.push_back(output_tensor(64, g, g));   // box(DFL)
        d.outputs.push_back(output_tensor(1, g, g));    // score
        d.outputs.push_back(output_tensor(1, g, g));    // score_sum
        d.outputs.push_back(output_tensor(32, g, g));   // seg 系数
    }
    d.outputs.push_back(output_tensor(32, 160, 160));   // proto
    return d;
}

RawDetectTensor ftensor(const std::vector<float>& v, int c, int h, int w) {
    RawDetectTensor t;
    t.data = reinterpret_cast<const std::uint8_t*>(v.data());
    t.channels = c;
    t.height = h;
    t.width = w;
    t.dtype = TensorType::Float32;
    t.fmt = TensorFormat::Nchw;
    t.is_float = true;
    t.scale = 0.0f;
    t.zero_point = 0;
    return t;
}

LetterboxParams identity_lb(int w, int h) {
    LetterboxParams lb;
    lb.src_w = w;
    lb.src_h = h;
    lb.dst_w = w;
    lb.dst_h = h;
    lb.scale = 1.0;
    lb.resized_w = w;
    lb.resized_h = h;
    return lb;
}

FrameId fid(const char* camera, std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = "fixture-session";
    f.camera_id = camera;
    f.generation = gen;
    f.seq = seq;
    return f;
}

InferenceEnvelope ready_env(const FrameId& id) {
    EnvelopeBuildInput in;
    in.frame = id;
    in.profile = AiProfile::Front;
    in.model_version = "fixture-model";
    in.capture_mono_us = 1000;
    in.result_mono_us = 2000;
    in.detect_ready = true;
    in.requires_segmentation = false;
    return build_envelope(in);
}

// ---- 单判例 ----

SelfTestCase mk(const std::string& id, const std::string& area, const std::string& req) {
    SelfTestCase c;
    c.id = id;
    c.area = area;
    c.requirement = req;
    return c;
}

// 模型能力：缺省 / 不匹配 / 就绪（FI-AI-01/02、ITEM-001）。
void case_model_capability(std::vector<SelfTestCase>* out) {
    {
        SelfTestCase c = mk("model.missing", "model", "FI-AI-01/TC-T-CAP-02");
        ModelDescriptor d;
        d.path = "fixture/absent.rknn";
        d.file_name = "absent.rknn";
        d.file_present = false;
        d.load_status = "MODEL_MISSING";
        const CapabilityCheck chk =
            check_capability(d, spec_for_declared("yolov5s", AiProfile::Front));
        // 缺失模型必须给出稳定 reason（MODEL_MISSING），绝不 ready。
        c.pass = !chk.ready && chk.reason == vision_reason::kModelMissing();
        c.detail = "absent model is not ready (MODEL_MISSING)";
        c.evidence["ready"] = chk.ready ? "true" : "false";
        c.evidence["reason"] = chk.reason;
        c.evidence["file_present"] = "false";
        out->push_back(c);
    }
    {
        SelfTestCase c = mk("model.tensor_mismatch", "model", "FI-AI-02/TC-T-CAP-03");
        // seg 结构冒充 yolov5s 声明 -> 张量数量/形状不匹配。
        const ModelDescriptor d = desc_yolov8s_seg_ok();
        const CapabilityCheck chk =
            check_capability(d, spec_for_declared("yolov5s", AiProfile::Front));
        c.pass = !chk.ready && chk.reason == vision_reason::kModelMismatch();
        c.detail = "seg-like descriptor does not satisfy yolov5s spec";
        c.evidence["ready"] = chk.ready ? "true" : "false";
        c.evidence["reason"] = chk.reason;
        c.evidence["mismatched_items"] = std::to_string(chk.mismatched.size());
        out->push_back(c);
    }
    {
        SelfTestCase c = mk("model.ready", "model", "TC-T-CAP-01");
        const ModelDescriptor det = desc_yolov5_ok();
        const ModelDescriptor seg = desc_yolov8s_seg_ok();
        const CapabilityCheck a =
            check_capability(det, spec_for_declared("yolov5s", AiProfile::Front));
        const CapabilityCheck b =
            check_capability(seg, spec_for_declared("yolov8s-seg", AiProfile::Front));
        c.pass = a.ready && b.ready && a.reason == vision_reason::kReady();
        c.detail = "detect + seg descriptors match declared specs";
        c.evidence["detect_ready"] = a.ready ? "true" : "false";
        c.evidence["seg_ready"] = b.ready ? "true" : "false";
        c.evidence["detect_outputs"] = std::to_string(det.outputs.size());
        c.evidence["seg_outputs"] = std::to_string(seg.outputs.size());
        out->push_back(c);
    }
}

// 预处理：合成 BGR 图 -> letterbox/量化（ITEM-002、TC-T-PRE-01/03/04）。
void case_preprocess(std::vector<SelfTestCase>* out) {
    SelfTestCase c = mk("preprocess.synthetic_letterbox", "preprocess", "TC-T-PRE-01/03/04");
    const int src_w = 1280;
    const int src_h = 720;
    std::vector<std::uint8_t> bgr(static_cast<std::size_t>(src_w) * src_h * 3, 0);
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * src_w + x) * 3;
            bgr[i + 0] = 10;    // B
            bgr[i + 1] = 20;    // G
            bgr[i + 2] = 200;   // R（合成红块：验证 BGR->RGB 显式转换）
        }
    }
    PreprocessConfig cfg;
    cfg.target_w = 640;
    cfg.target_h = 640;
    InputTensorSpec spec;
    spec.dtype = TensorType::Int8;
    spec.scale = 0.00392157f;
    spec.zero_point = -128;
    normalize_input_spec(&spec);
    cfg.input = spec;
    PreprocessedInput pi;
    std::string err;
    const bool ok = prepare_from_packed(bgr.data(), src_w, src_h, src_w * 3, ChannelOrder::Bgr,
                                        cfg, &pi, &err);
    c.pass = ok && pi.has_data() && pi.width == 640 && pi.height == 640 &&
             pi.letterbox.scale > 0.49 && pi.letterbox.scale < 0.51 &&
             pi.letterbox.pad_y == 140 && pi.data.size() == 640u * 640u * 3u &&
             pi.channel_mean[0] > pi.channel_mean[2];   // R 通道均值 > B（已转 RGB）
    c.detail = ok ? "letterbox/quantize produced 640x640 RGB int8" : ("failed: " + err);
    c.evidence["ok"] = ok ? "true" : "false";
    c.evidence["scale"] = num(pi.letterbox.scale, 6);
    c.evidence["pad_x"] = std::to_string(pi.letterbox.pad_x);
    c.evidence["pad_y"] = std::to_string(pi.letterbox.pad_y);
    c.evidence["bytes"] = std::to_string(pi.data.size());
    c.evidence["r_mean"] = num(pi.channel_mean[0], 2);
    c.evidence["b_mean"] = num(pi.channel_mean[2], 2);
    out->push_back(c);

    SelfTestCase e = mk("preprocess.empty_frame", "preprocess", "FI-AI-04/TC-T-PRE-05");
    PreprocessedInput pi2;
    std::string err2;
    const bool ok2 = prepare_from_packed(nullptr, 0, 0, 0, ChannelOrder::Bgr, cfg, &pi2, &err2);
    e.pass = !ok2 && !err2.empty() && !pi2.has_data();
    e.detail = "empty/invalid frame rejected explicitly";
    e.evidence["ok"] = ok2 ? "true" : "false";
    e.evidence["reason"] = err2;
    out->push_back(e);
}

// 检测后处理：无检测 / 合成单框（ITEM-003、TC-T-DET-01/02）。
void case_detect(std::vector<SelfTestCase>* out) {
    // 无检测：三尺度全 0 -> ok + 空框
    std::vector<float> z0(static_cast<std::size_t>(255) * 80 * 80, 0.0f);
    std::vector<float> z1(static_cast<std::size_t>(255) * 40 * 40, 0.0f);
    std::vector<float> z2(static_cast<std::size_t>(255) * 20 * 20, 0.0f);
    std::vector<RawDetectTensor> zeros = {ftensor(z0, 255, 80, 80), ftensor(z1, 255, 40, 40),
                                         ftensor(z2, 255, 20, 20)};
    DetectConfig dcfg;
    dcfg.num_classes = 80;
    dcfg.class_whitelist = {0};
    dcfg.anchors = default_yolov5_anchors();
    DetectResult empty;
    std::string err;
    const bool ok = decode_yolov5(zeros, dcfg, identity_lb(640, 640), 640, 640, &empty, &err);
    {
        SelfTestCase c = mk("detect.no_detection", "detect", "TC-T-DET-04");
        c.pass = ok && empty.ok && empty.boxes.empty() && empty.reason == detect_reason::kEmpty();
        c.detail = "all-zero tensors -> ready with empty boxes (no error)";
        c.evidence["ok"] = ok ? "true" : "false";
        c.evidence["boxes"] = std::to_string(empty.boxes.size());
        c.evidence["reason"] = empty.reason;
        out->push_back(c);
    }

    // 合成单框：scale0/80x80，anchor0，cell(40,40)
    //   cx=cy=(0.5+40)*8=324, bw=(2*0.5)^2*10=10, bh=(2*0.5)^2*13=13
    //   -> x1=319,y1=317.5,x2=329,y2=330.5（模型空间）；640x640 无 padding -> 归一化
    std::vector<float> s0(static_cast<std::size_t>(255) * 80 * 80, 0.0f);
    auto at = [&](int c, int y, int x, float v) {
        s0[static_cast<std::size_t>(c) * 80 * 80 + static_cast<std::size_t>(y) * 80 + x] = v;
    };
    const int cell = 40;
    at(0, cell, cell, 0.5f);   // cx
    at(1, cell, cell, 0.5f);   // cy
    at(2, cell, cell, 0.5f);   // w
    at(3, cell, cell, 0.5f);   // h
    at(4, cell, cell, 1.0f);   // obj
    at(5, cell, cell, 1.0f);   // cls0(person)
    std::vector<RawDetectTensor> one = {ftensor(s0, 255, 80, 80), ftensor(z1, 255, 40, 40),
                                        ftensor(z2, 255, 20, 20)};
    DetectResult det;
    std::string derr;
    const bool ok2 = decode_yolov5(one, dcfg, identity_lb(640, 640), 640, 640, &det, &derr);
    const double exp_x = 319.0 / 640.0;
    const double exp_y = 317.5 / 640.0;
    const double exp_w = 10.0 / 640.0;
    const double exp_h = 13.0 / 640.0;
    {
        SelfTestCase c = mk("detect.synthetic_box", "detect", "TC-T-DET-02/FI-AI-11");
        const bool box_ok = ok2 && det.ok && det.boxes.size() == 1 &&
                            det.boxes[0].label == "person" && det.boxes[0].class_id == 0 &&
                            near(det.boxes[0].x, exp_x, 0.001) &&
                            near(det.boxes[0].y, exp_y, 0.001) &&
                            near(det.boxes[0].w, exp_w, 0.001) &&
                            near(det.boxes[0].h, exp_h, 0.001);
        c.pass = box_ok;
        c.detail = "synthetic person box decoded at expected normalized coordinates";
        c.evidence["boxes"] = std::to_string(det.boxes.size());
        c.evidence["label"] = det.boxes.empty() ? "" : det.boxes[0].label;
        c.evidence["x"] = det.boxes.empty() ? "-" : num(det.boxes[0].x, 6);
        c.evidence["y"] = det.boxes.empty() ? "-" : num(det.boxes[0].y, 6);
        c.evidence["w"] = det.boxes.empty() ? "-" : num(det.boxes[0].w, 6);
        c.evidence["h"] = det.boxes.empty() ? "-" : num(det.boxes[0].h, 6);
        c.evidence["expected_x"] = num(exp_x, 6);
        c.evidence["tolerance"] = "0.001";
        out->push_back(c);
    }
}

// 分割后处理：13 张量 -> 1 rail 框 + 原图二值掩码（ITEM-004、TC-T-SEG-01/02）。
struct SegFixture {
    std::vector<std::vector<float>> storage;
    std::vector<RawDetectTensor> tensors;
    SegResult result;
    bool ok = false;
    std::string err;
};

SegFixture build_seg_fixture(int src_w, int src_h) {
    SegFixture f;
    f.storage.reserve(13);
    const int grids[3] = {80, 40, 20};
    for (int s = 0; s < 3; ++s) {
        const int g = grids[s];
        const std::size_t plane = static_cast<std::size_t>(g) * g;
        // box: 64 通道（4×16 DFL）
        std::vector<float> box(64 * plane, 0.0f);
        if (s == 0) {
            const int cell = 40;
            // x1 -> dfl index 0；x2 -> dfl index 15；y 同理
            // DFL 布局：每个 side 16 通道；x1 -> side0 d=0，y1 -> side1 d=0，
            // x2 -> side2 d=15，y2 -> side3 d=15（softmax 峰值决定偏移）
            box[0 * plane + static_cast<std::size_t>(cell) * g + cell] = 10.0f;
            box[16 * plane + static_cast<std::size_t>(cell) * g + cell] = 10.0f;
            box[47 * plane + static_cast<std::size_t>(cell) * g + cell] = 10.0f;
            box[63 * plane + static_cast<std::size_t>(cell) * g + cell] = 10.0f;
        }
        std::vector<float> score(plane, 0.0f);
        std::vector<float> ssum(plane, 0.0f);
        std::vector<float> seg(static_cast<std::size_t>(32) * plane, 0.0f);
        if (s == 0) {
            const int cell = 40;
            score[static_cast<std::size_t>(cell) * g + cell] = 1.0f;
            ssum[static_cast<std::size_t>(cell) * g + cell] = 1.0f;
            seg[0 * plane + static_cast<std::size_t>(cell) * g + cell] = 1.0f;  // coeff[0]=1
        }
        const int chans[4] = {64, 1, 1, 32};
        std::vector<float>* bufs[4] = {&box, &score, &ssum, &seg};
        for (int k = 0; k < 4; ++k) {
            f.storage.push_back(std::move(*bufs[k]));
            const std::vector<float>& v = f.storage.back();
            f.tensors.push_back(ftensor(v, chans[k], g, g));
        }
    }
    // proto 32×160×160：通道 0 全 1（掩码点积为正），其余 0
    std::vector<float> proto(static_cast<std::size_t>(32) * 160 * 160, 0.0f);
    for (std::size_t i = 0; i < static_cast<std::size_t>(160) * 160; ++i) proto[i] = 1.0f;
    f.storage.push_back(std::move(proto));
    f.tensors.push_back(ftensor(f.storage.back(), 32, 160, 160));

    SegConfig scfg;
    scfg.num_classes = 1;
    scfg.class_whitelist = {0};
    scfg.proto_channels = 32;
    scfg.proto_h = 160;
    scfg.proto_w = 160;
    scfg.crop_to_box = true;
    scfg.mask_threshold = 0.0f;
    LetterboxParams lb;
    if (!compute_letterbox(src_w, src_h, 640, 640, &lb, &f.err)) return f;
    f.ok = decode_yolov8_seg(f.tensors, scfg, lb, src_w, src_h, &f.result, &f.err);
    return f;
}

void case_seg(std::vector<SelfTestCase>* out) {
    SelfTestCase c = mk("seg.synthetic_mask", "seg", "TC-T-SEG-01/02/03");
    SegFixture f = build_seg_fixture(640, 640);
    const bool mask_ok = f.ok && f.result.ok && f.result.boxes.size() == 1 &&
                         f.result.masks.size() == f.result.boxes.size() &&
                         f.result.masks[0].width == 640 && f.result.masks[0].height == 640 &&
                         f.result.masks[0].stride_bytes == 640 && f.result.masks[0].positive > 0;
    c.pass = mask_ok;
    c.detail = "13-tensor seg fixture -> 1 rail box with full-size binary plane";
    if (!f.result.boxes.empty()) {
        c.evidence["boxes"] = std::to_string(f.result.boxes.size());
        c.evidence["score"] = num(f.result.boxes[0].score, 4);
        c.evidence["x"] = num(f.result.boxes[0].x, 4);
        c.evidence["w"] = num(f.result.boxes[0].w, 4);
    }
    if (!f.result.masks.empty()) {
        c.evidence["mask_w"] = std::to_string(f.result.masks[0].width);
        c.evidence["mask_h"] = std::to_string(f.result.masks[0].height);
        c.evidence["positive"] = std::to_string(f.result.masks[0].positive);
    }
    c.evidence["reason"] = f.result.reason;
    out->push_back(c);

    // 同帧两阶段：命中 / 缺掩码不可用（ITEM-005、TC-T-TWOPHASE-01/03）
    if (f.ok && f.result.ok && !f.result.boxes.empty()) {
        DetectBox person;
        person.x = 0.58f;
        person.y = 0.58f;
        person.w = 0.04f;
        person.h = 0.06f;
        person.score = 0.9f;
        person.class_id = 0;
        person.label = "person";
        std::vector<DetectBox> persons = {person};

        TwoStageConfig tcfg;
        tcfg.require_same_frame = true;
        TwoStageInput in;
        in.frame = fid("CAM-01", 1, 1);
        in.detect_frame = in.frame;
        in.seg_frame = in.frame;
        in.persons = &persons;
        in.rails = &f.result.boxes;
        in.rail_masks = &f.result.masks;
        in.image_w = 640;
        in.image_h = 640;
        TwoStageResult ts;
        std::string terr;
        const bool tok = evaluate_two_stage(in, tcfg, &ts, &terr);

        SelfTestCase hit = mk("two_stage.person_on_mask", "two_stage", "TC-T-TWOPHASE-01");
        hit.pass = tok && ts.available && ts.person_total == 1 && ts.mask_hit == 1 &&
                   ts.alarm_hits == 1;
        hit.detail = "person center inside rail mask -> alarm hit";
        hit.evidence["person_total"] = std::to_string(ts.person_total);
        hit.evidence["mask_hit"] = std::to_string(ts.mask_hit);
        hit.evidence["alarm_hits"] = std::to_string(ts.alarm_hits);
        out->push_back(hit);

        std::vector<SegMask> no_masks;
        TwoStageInput in2 = in;
        in2.rail_masks = &no_masks;
        TwoStageResult ts2;
        std::string terr2;
        const bool tok2 = evaluate_two_stage(in2, tcfg, &ts2, &terr2);
        SelfTestCase no_mask = mk("two_stage.mask_missing_unavailable", "two_stage",
                                  "FI-AI-07/TC-T-TWOPHASE-03");
        no_mask.pass = tok2 && !ts2.available && ts2.alarm_hits == 0 &&
                       ts2.reason == two_stage_reason::kUnavailable();
        no_mask.detail = "missing mask -> unavailable, never 'rail safe'";
        no_mask.evidence["available"] = ts2.available ? "true" : "false";
        no_mask.evidence["reason"] = ts2.reason;
        no_mask.evidence["alarm_hits"] = std::to_string(ts2.alarm_hits);
        out->push_back(no_mask);
    }
}

// 结果关联：陈旧 generation / 重复 seq / 乱序 seq（ITEM-006、FI-AI-10、TC-T-PUB-02）。
void case_envelope(std::vector<SelfTestCase>* out) {
    SelfTestCase c = mk("envelope.stale_duplicate_out_of_order", "envelope",
                        "FI-AI-10/TC-T-PUB-02");
    EnvelopePublisher pub("fixture-session", 2);
    std::string rej;
    const bool a = pub.submit(ready_env(fid("CAM-01", 1, 1)), &rej);      // 采纳
    const bool b = pub.submit(ready_env(fid("CAM-01", 2, 1)), &rej);      // 换代
    const bool s = pub.submit(ready_env(fid("CAM-01", 1, 2)), &rej);      // 旧代
    const std::string stale_reason = rej;
    const bool d = pub.submit(ready_env(fid("CAM-01", 2, 1)), &rej);      // 重复 seq
    const std::string dup_reason = rej;
    const bool o = pub.submit(ready_env(fid("CAM-01", 2, 0)), &rej);      // 乱序
    const std::string ooo_reason = rej;
    const EnvelopeStats st = pub.stats();
    c.pass = a && b && !s && !d && !o && st.accepted == 2 && st.stale_generation == 1 &&
             st.duplicate_seq == 1 && st.out_of_order_seq == 1;
    c.detail = "stale generation / duplicate seq / out-of-order each rejected and counted";
    c.evidence["accepted"] = std::to_string(st.accepted);
    c.evidence["stale_generation"] = std::to_string(st.stale_generation);
    c.evidence["duplicate_seq"] = std::to_string(st.duplicate_seq);
    c.evidence["out_of_order_seq"] = std::to_string(st.out_of_order_seq);
    c.evidence["stale_reason"] = stale_reason;
    c.evidence["duplicate_reason"] = dup_reason;
    c.evidence["out_of_order_reason"] = ooo_reason;
    out->push_back(c);

    // unavailable 与 ready+空框可区分（TC-T-PUB-03）
    SelfTestCase u = mk("envelope.unavailable_vs_empty", "envelope", "TC-T-PUB-03/FI-AI-07");
    const InferenceEnvelope un =
        make_unavailable_envelope(fid("CAM-01", 1, 9), AiProfile::Front, "m", 1000, 2000,
                                  envelope_reason::kSegUnavailable());
    EnvelopeBuildInput bin;
    bin.frame = fid("CAM-01", 1, 10);
    bin.profile = AiProfile::Front;
    bin.model_version = "m";
    bin.capture_mono_us = 1000;
    bin.result_mono_us = 2000;
    bin.detect_ready = true;
    bin.requires_segmentation = false;
    const InferenceEnvelope ready = build_envelope(bin);
    u.pass = !un.ready() && un.boxes.empty() && un.reason == envelope_reason::kSegUnavailable() &&
             ready.ready() && ready.boxes.empty() && ready.reason == envelope_reason::kReady();
    u.detail = "unavailable+reason vs ready+empty boxes are distinguishable";
    u.evidence["unavailable_reason"] = un.reason;
    u.evidence["ready_reason"] = ready.reason;
    out->push_back(u);
}

// 未知 label 拦截（FI-AI-12 / TC-T-SEM-01）：未知类零报警 + 拦截计数可见，person 照常映射。
void case_semantic(std::vector<SelfTestCase>* out) {
    SelfTestCase c = mk("semantic.unknown_label_intercepted", "semantic", "FI-AI-12/TC-T-SEM-01");
    std::vector<DetectBox> boxes;
    DetectBox person;
    person.x = 0.40f;
    person.y = 0.50f;
    person.w = 0.10f;
    person.h = 0.20f;
    person.score = 0.90f;
    person.class_id = 0;
    person.label = "person";
    boxes.push_back(person);
    DetectBox unknown = person;
    unknown.class_id = 77;
    unknown.label = "weird_class";   // 既非白名单也非已知标签
    boxes.push_back(unknown);
    std::vector<char> in_roi = {1, 1};

    EnvelopeBuildInput bin;
    bin.frame = fid("CAM-01", 1, 7);
    bin.profile = AiProfile::Front;
    bin.model_version = "fixture-model";
    bin.capture_mono_us = 1000;
    bin.result_mono_us = 2000;
    bin.detect_ready = true;
    bin.boxes = &boxes;
    bin.in_roi_flags = in_roi;
    const InferenceEnvelope env = build_envelope(bin);

    SemanticConfig sc;
    sc.profile = AiProfile::Front;
    sc.allowed_labels = {"person", "obstacle"};
    sc.known_labels = {"person", "obstacle"};
    sc.confidence_min = 0.5;
    sc.require_track = true;
    sc.use_envelope_in_roi = true;
    sc.track_policy = TrackPolicy::Lightweight;
    SemanticAdapter adapter(sc);
    RuleConfig rc;
    rc.confirm_frames = 1;   // 单帧即可确认：person 产生 1 个事件，未知类必须 0 个
    rc.confirm_window_ms = 500;
    rc.confidence_min = 0.5;
    rc.suppress_window_ms = 3000;
    RuleEngine rules(rc);
    SemanticResult res;
    const std::vector<AlarmEvent> events = adapter.adapt_and_dispatch(env, 1000, &rules, &res);

    bool only_person = true;
    for (const auto& d : res.detections)
        if (d.label != "person") only_person = false;
    c.pass = res.available && res.stats.blocked_unknown_label == 1 && res.stats.mapped == 1 &&
             res.detections.size() == 1 && only_person && events.size() == 1 &&
             events[0].type == AlarmType::PersonOnRail;
    c.detail = "unknown label intercepted (no alarm); person still mapped and confirmed";
    c.evidence["input_boxes"] = std::to_string(boxes.size());
    c.evidence["mapped"] = std::to_string(res.stats.mapped);
    c.evidence["blocked_unknown_label"] = std::to_string(res.stats.blocked_unknown_label);
    c.evidence["detections"] = std::to_string(res.detections.size());
    c.evidence["events"] = std::to_string(events.size());
    c.evidence["event_type"] = events.empty() ? "-" : to_string(events[0].type);
    out->push_back(c);
}

// 可用性：AI 停止 / 三职责无模型 / 距离未标定 / 调试占位（ITEM-009/010/011/012）。
void case_availability(std::vector<SelfTestCase>* out) {
    {
        SelfTestCase c = mk("availability.ai_stopped", "availability", "TC-T-DISP-02");
        AiRuntime rt;
        rt.worker_alive = false;
        AiChannelRuntime ch;
        ch.camera_id = "CAM-01";
        ch.profile = AiProfile::Front;
        rt.channels.push_back(ch);
        const std::string text = ai_status_text(rt, 5000);
        c.pass = text == "AI 不可用" && ai_status_level(rt, 5000) == StatusLevel::Unavailable;
        c.detail = "worker stopped -> AI unavailable";
        c.evidence["status_text"] = text;
        out->push_back(c);
    }
    {
        SelfTestCase c = mk("availability.duty_unsupported", "availability", "TC-T-MIS-01/02");
        bool ok = true;
        std::string states;
        for (DutyKind k : {DutyKind::Pantograph, DutyKind::WheelRail, DutyKind::CabDoor}) {
            DutyEvidenceInput in;
            in.duty = k;
            in.registered_in_config = true;
            in.registered_model_name = "yolov5s";   // 登记不构成能力
            const DutyAvailability a = evaluate_duty(in);
            if (a.available || a.reason != duty_reason::kProfileUnsupported() ||
                a.state != DutyState::Unknown)
                ok = false;
            states += std::string(to_string(k)) + "=" + to_string(a.state) + ";";
        }
        c.pass = ok;
        c.detail = "three duties unavailable + unknown despite config registration";
        c.evidence["states"] = states;
        c.evidence["reason"] = duty_reason::kProfileUnsupported();
        out->push_back(c);
    }
    {
        SelfTestCase c = mk("availability.distance_uncalibrated", "availability", "TC-T-DIST-01");
        DistanceCalibrationSet empty;
        DistanceRequest r;
        r.camera_id = "CAM-01";
        r.label = "person";
        r.box_height_px = 89.0;
        r.zoom_known = false;
        const DistanceEstimate e = estimate_distance(empty, r);
        c.pass = !e.valid && e.meters == -1.0 && distance_text(e) == "—";
        c.detail = "no calibration -> distance -1 with unknown placeholder";
        c.evidence["distance_m"] = num(e.meters, 2);
        c.evidence["text"] = distance_text(e);
        c.evidence["reason"] = e.reason;
        out->push_back(c);
    }
    {
        SelfTestCase c = mk("availability.debug_unknown_not_zero", "debug", "TC-T-DBG-03");
        DebugViewInput in;
        CameraSlot slot;
        slot.id = "CAM-04";
        slot.name = "roof";
        slot.ai_enabled = true;
        slot.ai_profile = AiProfile::Pantograph;
        in.registry.push_back(slot);
        AiRuntime rt;
        rt.worker_alive = true;
        AiChannelRuntime ch;
        ch.camera_id = "CAM-04";
        ch.profile = AiProfile::Pantograph;
        in.runtime = rt;
        DebugChannelInput d;
        d.row.camera_id = "CAM-04";
        d.row.profile = AiProfile::Pantograph;
        d.row.available = false;
        d.row.unavailable_reason = duty_reason::kProfileUnsupported();
        d.row.distance_text = "—";
        d.row.last_result_text = "—";
        d.boxes_available = false;
        in.channels.push_back(d);
        in.role = Role::Developer;
        const DebugViewModel vm = build_debug_view(in);
        bool ok = vm.channels.size() == 1;
        if (ok) {
            const DebugChannelView& v = vm.channels[0];
            ok = v.model_version_text == "—" && v.fps_text == "—" && v.latency_text == "—" &&
                 v.confidence_text == "—" && v.box_count_text == "—" &&
                 v.distance_text == "—" && v.unavailable_reason == duty_reason::kProfileUnsupported();
        }
        c.pass = ok;
        c.detail = "unavailable channel shows reason and '—' placeholders (never 0)";
        c.evidence["unavailable_reason"] =
            vm.channels.empty() ? "" : vm.channels[0].unavailable_reason;
        c.evidence["fps_text"] = vm.channels.empty() ? "" : vm.channels[0].fps_text;
        c.evidence["confidence_text"] = vm.channels.empty() ? "" : vm.channels[0].confidence_text;
        out->push_back(c);
    }
}

}  // namespace

SelfTestReport run_vision_selftest() {
    SelfTestReport r;
    r.timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    case_model_capability(&r.cases);
    case_preprocess(&r.cases);
    case_detect(&r.cases);
    case_seg(&r.cases);
    case_envelope(&r.cases);
    case_semantic(&r.cases);
    case_availability(&r.cases);

    for (const auto& c : r.cases) {
        if (c.pass) ++r.passed;
        else ++r.failed;
    }
    r.pending = {
        "真机精度/检出率/误报率：需 ENV-D 现场数据与冻结测试集实测（本自检不声称通过）",
        "人工视觉确认（框/掩码是否合理）：只能由用户完成（本自检不替代）",
        "4/6 路并发、热稳态温度与长跑稳定性：需板端长时实测（本自检仅夹具路径）",
        "真实相机 RTSP 取流与真实目标距离：需现场授权（ENV-D）",
    };
    return r;
}

std::string selftest_json(const SelfTestReport& r, bool include_timestamp) {
    std::ostringstream o;
    o << "{";
    o << "\"schema\":\"" << json_escape(r.schema) << "\"";
    o << ",\"no_device\":" << (r.no_device ? "true" : "false");
    o << ",\"environment\":\"" << json_escape(r.environment) << "\"";
    if (include_timestamp) o << ",\"timestamp_ms\":" << r.timestamp_ms;
    o << ",\"summary\":{\"total\":" << r.cases.size() << ",\"passed\":" << r.passed
      << ",\"failed\":" << r.failed << "}";
    o << ",\"cases\":[";
    for (std::size_t i = 0; i < r.cases.size(); ++i) {
        const SelfTestCase& c = r.cases[i];
        if (i) o << ",";
        o << "{";
        o << "\"id\":\"" << json_escape(c.id) << "\"";
        o << ",\"area\":\"" << json_escape(c.area) << "\"";
        o << ",\"requirement\":\"" << json_escape(c.requirement) << "\"";
        o << ",\"status\":\"" << (c.pass ? "PASS" : "FAIL") << "\"";
        o << ",\"detail\":\"" << json_escape(c.detail) << "\"";
        o << ",\"evidence\":{";
        std::size_t k = 0;
        for (const auto& kv : c.evidence) {
            if (k++) o << ",";
            o << "\"" << json_escape(kv.first) << "\":\"" << json_escape(kv.second) << "\"";
        }
        o << "}}";
    }
    o << "],\"pending\":[";
    for (std::size_t i = 0; i < r.pending.size(); ++i) {
        if (i) o << ",";
        o << "\"" << json_escape(r.pending[i]) << "\"";
    }
    o << "]}";
    return o.str();
}

std::vector<std::string> selftest_case_ids() {
    const SelfTestReport r = run_vision_selftest();
    std::vector<std::string> ids;
    ids.reserve(r.cases.size());
    for (const auto& c : r.cases) ids.push_back(c.id);
    return ids;
}

}  // namespace core
}  // namespace railhmi
