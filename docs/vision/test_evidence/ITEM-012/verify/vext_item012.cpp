// ITEM-012 验证侧自有夹具（vext_item012.cpp）——板端 g++ 直编。
//
// 覆盖交付自测（core 65 单测 + selftest 17 + media selftest 30）未覆盖的边界：
//   A. worker 真实掩码快照（VisionAiWorker 真实发帧链路 + 自定义检测器注入真实掩码——
//      交付 media selftest 的 stub 无掩码，只覆盖"无掩码→原因"分支）：
//      抓取使能/内容深拷贝/检测器缓冲独立/第二帧覆盖/关闭不抓取/ready 无掩码/
//      非法尺寸拒绝/检测器不可用原因透传/stride padding 整 plane 拷贝。
//   B. core build_debug_view 掩码门控深边界：stride padding 不计正像素（精确算术）、
//      stride<width 拒绝、plane 差一字节拒绝、恰好可用、null plane 拒绝、
//      kind 透传、全 0 plane 的 positive=0 是数值非占位、全 1 精确值、
//      有 registry 无 channels 的"无掩码数据"原因。
//   C. 字段语义补充：admin 全开关生效、available 覆盖语义锚定、真 0 与占位区分、
//      置信度两位小数、JSON 转义、距离文本透传、fps/latency 0 为未知、
//      陈旧原因透传。
//
// GStreamer 直驱 FrameStore::publish 真租约链路（视频线 ITEM-010/011 形态）。

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/vision_debug_view.h"
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_seg_postprocess.h"
#include "railhmi/media/vision_ai_worker.h"
#include "railhmi/media/frame_store.h"

using namespace railhmi;
using core::DebugChannelInput;
using core::DebugViewInput;
using core::DebugViewModel;
using core::OverlayMaskInput;
using core::OverlaySwitches;
using core::Role;

static int fails = 0;
static int passn = 0;
static void check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("vext %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.empty() ? "" : " :: ",
                detail.empty() ? "" : detail.c_str());
    std::fflush(stdout);
    if (ok) ++passn; else ++fails;
}

static std::string num(long long v) { return std::to_string(v); }

// 真实 GStreamer NV12 sample -> FrameStore::publish -> latest() 真租约。
static media::FrameLease publish_gray_nv12(media::FrameStore& store, core::FrameMeta meta,
                                           int h = 180) {
    const int w = 320, hh = h, stride = 320;
    const size_t y_size = (size_t)stride * hh;
    const size_t buf_size = y_size + (size_t)stride * (hh / 2);
    std::vector<unsigned char> buf(buf_size);
    std::memset(buf.data(), 200, y_size);
    for (size_t i = 0; i < (size_t)stride * (hh / 2); i += 2) {
        buf[y_size + i] = 128;
        buf[y_size + i + 1] = 128;
    }
    GstBuffer* gbuf = gst_buffer_new_wrapped(g_memdup(buf.data(), buf_size), buf_size);
    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, w, "height", G_TYPE_INT, hh,
                                        "framerate", GST_TYPE_FRACTION, 25, 1,
                                        "colorimetry", G_TYPE_STRING, "bt709", nullptr);
    GstSample* sample = gst_sample_new(gbuf, caps, nullptr, nullptr);
    gst_caps_unref(caps);
    gst_buffer_unref(gbuf);
    std::string err;
    core::LayoutStatus st = store.publish(sample, meta, "bt709_full", "simulation", &err);
    gst_sample_unref(sample);
    if (st != core::LayoutStatus::Ok) {
        std::printf("vext INFO publish rejected: status=%s err=%s\n",
                    core::to_string(st), err.c_str());
    }
    return store.latest();
}

static core::SemanticConfig base_semantic() {
    core::SemanticConfig sc;
    sc.profile = core::AiProfile::Front;
    sc.confidence_min = 0.5;
    return sc;
}

// ---------------------------------------------------------------------------
// 自定义检测器：ready + 可选真实掩码（SegResult 所有权经 out.seg 保活）。
// pattern_buffer 是成员（验证 A03：快照必须独立于检测器缓冲）。
// ---------------------------------------------------------------------------
class MaskedDetector : public media::VisionDetector {
public:
    MaskedDetector(bool available, std::string reason, bool with_mask,
                   int mw, int mh, int mstride, int pattern_phase)
        : available_(available), reason_(std::move(reason)), with_mask_(with_mask),
          mw_(mw), mh_(mh), mstride_(mstride), phase_(pattern_phase) {
        if (with_mask_ && mw_ > 0 && mh_ > 0 && mstride_ > 0)
            pattern_buffer_.assign((size_t)mstride_ * (size_t)mh_, 0);
        for (size_t i = 0; with_mask_ && i < pattern_buffer_.size(); ++i)
            if ((long long)(i % 2) == phase_) pattern_buffer_[i] = 1;
    }

    void corrupt_buffer(unsigned char v) {
        for (auto& b : pattern_buffer_) b = v;
    }
    void set_phase(int p) {
        phase_ = p;
        for (size_t i = 0; i < pattern_buffer_.size(); ++i)
            pattern_buffer_[i] = ((long long)(i % 2) == phase_) ? 1 : 0;
    }

    std::string model_version(core::AiProfile) const override { return "vext-mask-det"; }
    bool ready(core::AiProfile) const override { return available_; }
    std::string unavailable_reason(core::AiProfile) const override { return reason_; }
    std::string engine() const override { return "vext"; }

    media::VisionInferOutcome infer(const media::AiFrame& frame, core::AiProfile p) override {
        ++calls_;
        media::VisionInferOutcome out;
        out.envelope.frame.session_id = frame.session_id;
        out.envelope.frame.camera_id = frame.camera_id;
        out.envelope.frame.generation = frame.generation;
        out.envelope.frame.seq = frame.seq;
        out.envelope.profile = p;
        out.envelope.capture_mono_us = frame.capture_mono_us;
        out.envelope.model_version = "vext-mask-det";

        if (!available_) {
            out.reason = reason_;
            out.envelope.availability = core::AiAvailability::Unavailable;
            out.envelope.reason = reason_;
            out.envelope.result_mono_us = frame.capture_mono_us + 1000;
            return out;
        }

        auto seg = std::make_shared<core::SegResult>();
        seg->ok = true;
        std::vector<core::MaskRef> refs;
        if (with_mask_ && !pattern_buffer_.empty()) {
            core::SegMask m;
            m.box_index = -1;
            m.width = mw_;
            m.height = mh_;
            m.stride_bytes = mstride_;
            m.plane = pattern_buffer_;
            seg->masks.push_back(std::move(m));
            core::MaskRef r;
            r.box_index = -1;
            r.width = mw_;
            r.height = mh_;
            r.stride_bytes = mstride_;
            r.plane = &seg->masks[0].plane;
            r.kind = "rail";
            refs.push_back(r);
        }
        std::vector<core::DetectBox> boxes;
        core::EnvelopeBuildInput in;
        in.frame = out.envelope.frame;
        in.profile = p;
        in.model_version = "vext-mask-det";
        in.capture_mono_us = frame.capture_mono_us;
        in.result_mono_us = frame.capture_mono_us + 1000;
        in.detect_ready = true;
        in.requires_segmentation = false;
        in.boxes = &boxes;
        if (!refs.empty()) in.masks = &refs;
        out.envelope = core::build_envelope(in);
        out.available = out.envelope.ready();
        out.reason = out.envelope.reason;
        out.seg = seg;   // 保活 plane（worker 快照复制后共享安全）
        return out;
    }

    long long calls() const { return calls_; }

private:
    bool available_;
    std::string reason_;
    bool with_mask_;
    int mw_, mh_, mstride_;
    int phase_;
    std::vector<std::uint8_t> pattern_buffer_;
    long long calls_ = 0;
};

// worker 一帧直驱（capture 开关/检测器可配），返回 CAM-01 lane 快照。
// 检测器以 shared_ptr 持有：worker 与调用方共享同一实例（相位/缓冲操控对 worker 可见，
// 且 worker 析构后对象仍存活——不用裸 new，避免 use-after-free）。
static media::VisionLaneSnapshot drive_masked(bool capture,
                                              std::shared_ptr<MaskedDetector> det,
                                              core::FrameSequencer* seq, long long ts) {
    media::VisionWorkerConfig wc;
    wc.sched = {1, 2, 4, 85.0};
    wc.cameras = {{"CAM-01", true, "front"}};
    wc.semantic = base_semantic();
    wc.capture_latest_mask = capture;
    media::VisionAiWorker worker(wc, det, nullptr, nullptr);
    media::FrameStore store("CAM-01");
    media::FrameLease lease = publish_gray_nv12(store, seq->next(ts, -1, -1), 180);
    worker.submit(lease);
    worker.step();
    media::VisionWorkerSnapshot s = worker.snapshot();
    for (const auto& l : s.lanes)
        if (l.camera_id == "CAM-01") return l;
    return media::VisionLaneSnapshot();
}

// ---------------------------------------------------------------------------
// core 层基础输入（验证侧独立构造；CAM-01 可用带框，CAM-04 不可用）。
// ---------------------------------------------------------------------------
struct CoreFix {
    DebugViewInput input;
    std::vector<std::uint8_t> plane;
};

static CoreFix base_core_fix(Role role, const OverlaySwitches& sw) {
    CoreFix f;
    core::CameraSlot s1;
    s1.id = "CAM-01";
    s1.name = "camera-CAM-01";
    s1.ai_enabled = true;
    s1.ai_profile = core::AiProfile::Front;
    f.input.registry.push_back(s1);
    core::CameraSlot s2;
    s2.id = "CAM-04";
    s2.name = "camera-CAM-04";
    s2.ai_enabled = true;
    s2.ai_profile = core::AiProfile::Pantograph;
    f.input.registry.push_back(s2);

    core::AiChannelRuntime a;
    a.camera_id = "CAM-01";
    a.profile = core::AiProfile::Front;
    a.last_result_ms = 1000;
    a.fps = 5.0;
    a.latency_ms = 140;
    a.model_version = "rknn-2.3.2/yolov5s";
    f.input.runtime.channels.push_back(a);
    core::AiChannelRuntime b;
    b.camera_id = "CAM-04";
    b.profile = core::AiProfile::Pantograph;
    b.last_result_ms = 1000;
    f.input.runtime.channels.push_back(b);
    f.input.runtime.worker_alive = true;
    f.input.role = role;
    f.input.switches = sw;
    f.input.now_ms = 1200;

    DebugChannelInput ok;
    ok.row.camera_id = "CAM-01";
    ok.row.name = "camera-CAM-01";
    ok.row.profile = core::AiProfile::Front;
    ok.row.profile_label = core::ai_profile_label(core::AiProfile::Front);
    ok.row.available = true;
    ok.row.model_version = "rknn-2.3.2/yolov5s";
    ok.row.last_result_age_ms = 120;
    ok.row.last_result_text = "0 秒前";
    ok.row.distance_text = "";
    ok.boxes_available = true;
    ok.box_count = 1;
    core::DetectionBox box;
    box.x = 0.4f;
    box.y = 0.5f;
    box.w = 0.1f;
    box.h = 0.2f;
    box.confidence = 0.87f;
    box.label = "person";
    box.in_roi = true;
    box.distance_m = -1.0f;
    ok.row.boxes.push_back(box);
    ok.row.max_confidence = 0.87f;
    f.input.channels.push_back(ok);

    DebugChannelInput bad;
    bad.row.camera_id = "CAM-04";
    bad.row.name = "camera-CAM-04";
    bad.row.profile = core::AiProfile::Pantograph;
    bad.row.profile_label = core::ai_profile_label(core::AiProfile::Pantograph);
    bad.row.available = false;
    bad.row.last_result_age_ms = -1;
    bad.row.last_result_text = "";
    bad.row.distance_text = "";
    bad.row.unavailable_reason = "PROFILE_UNSUPPORTED";
    bad.boxes_available = false;
    f.input.channels.push_back(bad);
    return f;
}

int main() {
    gst_init(nullptr, nullptr);

    // 夹具自检（非判据）：真租约构造成功。
    {
        media::FrameStore st("CAM-X");
        core::FrameSequencer fseq("sess-fix", "CAM-X");
        fseq.begin_generation();
        media::FrameLease l0 = publish_gray_nv12(st, fseq.next(1000, -1, -1), 180);
        if (!l0.valid() || l0.plane_data(0) == nullptr) {
            std::printf("vext FATAL fixture_publish_failed\n");
            return 3;
        }
        std::printf("vext INFO fixture publish ok seq=%llu h=%d\n",
                    (unsigned long long)l0.descriptor().seq,
                    (int)l0.descriptor().layout.height);
    }

    // ===== A. worker 真实掩码快照 =====
    {
        // A1 抓取使能 + 有效掩码 -> READY / 尺寸 / updates=1
        core::FrameSequencer seq("sess-vext-a", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(true, "", true, 64, 48, 64, 0);
        auto lane = drive_masked(true, det, &seq, 1000);
        check(lane.mask_available && lane.mask_unavailable_reason == "READY" &&
                  lane.mask_width == 64 && lane.mask_height == 48 &&
                  lane.mask_stride_bytes == 64 && lane.mask_updates == 1,
              "A01_worker_mask_captured_when_enabled",
              lane.mask_unavailable_reason + " upd=" + num(lane.mask_updates));
        // A2 快照内容 == 注入内容（phase=0 -> 偶数下标为 1）
        const auto& p = lane.mask_plane;
        check(p && p->size() == (size_t)64 * 48 && (*p)[0] == 1 && (*p)[1] == 0 &&
                  (*p)[2] == 1,
              "A02_worker_mask_plane_content_copy",
              p ? "size=" + num((long long)p->size()) : "null");
        // A3 检测器缓冲事后改写 -> 快照不变（深拷贝独立）
        det->corrupt_buffer(0xAA);
        check((*p)[0] == 1 && (*p)[1] == 0 && (*p)[2] == 1,
              "A03_worker_mask_snapshot_independent", "detector buffer corrupted");
    }
    {
        // A4 第二帧掩码覆盖第一帧（最近一帧语义）+ updates=2
        core::FrameSequencer seq("sess-vext-b", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(true, "", true, 64, 48, 64, 0);
        media::VisionWorkerConfig wc;
        wc.sched = {1, 2, 4, 85.0};
        wc.cameras = {{"CAM-01", true, "front"}};
        wc.semantic = base_semantic();
        wc.capture_latest_mask = true;
        media::VisionAiWorker worker(wc, det, nullptr, nullptr);
        media::FrameStore store("CAM-01");
        media::FrameLease l1 = publish_gray_nv12(store, seq.next(1000, -1, -1), 180);
        worker.submit(l1);
        worker.step();
        // worker 与本测试共享同一检测器实例：相位改变下一帧输出图案
        det->set_phase(1);
        media::FrameLease l2 = publish_gray_nv12(store, seq.next(2000, -1, -1), 180);
        worker.submit(l2);
        worker.step();
        media::VisionWorkerSnapshot s = worker.snapshot();
        for (const auto& l : s.lanes) {
            if (l.camera_id != "CAM-01") continue;
            check(l.mask_available && l.mask_updates == 2 && l.mask_plane &&
                      (*l.mask_plane)[0] == 0 && (*l.mask_plane)[1] == 1,
                  "A04_worker_mask_second_frame_overwrites",
                  "upd=" + num(l.mask_updates) + " p0=" +
                      num(l.mask_plane ? (*l.mask_plane)[0] : -1));
        }
    }
    {
        // A5 抓取关闭 + 检测器有掩码 -> NOT_CAPTURED / 无 plane / updates=0
        core::FrameSequencer seq("sess-vext-c", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(true, "", true, 64, 48, 64, 0);
        auto lane = drive_masked(false, det, &seq, 1000);
        check(!lane.mask_available && lane.mask_unavailable_reason == "NOT_CAPTURED" &&
                  lane.mask_plane == nullptr && lane.mask_updates == 0,
              "A05_worker_mask_capture_off_not_captured",
              lane.mask_unavailable_reason);
    }
    {
        // A6 ready 但本帧无掩码 -> SEG_UNAVAILABLE（非 NOT_CAPTURED）
        core::FrameSequencer seq("sess-vext-d", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(true, "", false, 0, 0, 0, 0);
        auto lane = drive_masked(true, det, &seq, 1000);
        check(!lane.mask_available && lane.mask_unavailable_reason == "SEG_UNAVAILABLE",
              "A06_worker_mask_ready_no_masks", lane.mask_unavailable_reason);
    }
    {
        // A7 ready 但掩码非法（stride < width）-> SEG_UNAVAILABLE / 不抓取
        core::FrameSequencer seq("sess-vext-e", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(true, "", true, 64, 48, 32, 0);
        auto lane = drive_masked(true, det, &seq, 1000);
        check(!lane.mask_available && lane.mask_unavailable_reason == "SEG_UNAVAILABLE" &&
                  lane.mask_updates == 0,
              "A07_worker_mask_invalid_dims_rejected", lane.mask_unavailable_reason);
    }
    {
        // A8 检测器不可用 -> 原因透传（非 NOT_CAPTURED/SEG_UNAVAILABLE）
        core::FrameSequencer seq("sess-vext-f", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(false, "STUB_DOWN", false, 0, 0, 0, 0);
        auto lane = drive_masked(true, det, &seq, 1000);
        check(!lane.mask_available && lane.mask_unavailable_reason == "STUB_DOWN",
              "A08_worker_mask_detector_unavailable_reason", lane.mask_unavailable_reason);
    }
    {
        // A9 stride > width -> 整 plane 拷贝（含 padding），尺寸/stride 原样
        core::FrameSequencer seq("sess-vext-g", "CAM-01");
        seq.begin_generation();
        auto det = std::make_shared<MaskedDetector>(true, "", true, 64, 48, 68, 0);
        auto lane = drive_masked(true, det, &seq, 1000);
        check(lane.mask_available && lane.mask_plane &&
                  lane.mask_plane->size() == (size_t)68 * 48 &&
                  lane.mask_stride_bytes == 68,
              "A09_worker_mask_whole_plane_with_padding",
              lane.mask_plane ? "size=" + num((long long)lane.mask_plane->size()) : "null");
    }

    // ===== B. core 掩码门控深边界 =====
    {
        // B1 stride padding 不计正像素：width=4 height=3 stride=6，行内 [1,0,1,0]，
        //    padding 全 0xFF。正像素 = 2*3 = 6（若实现误扫整行 -> 18）。
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign((size_t)6 * 3, 0);
        for (int y = 0; y < 3; ++y) {
            fx.plane[(size_t)y * 6 + 0] = 1;
            fx.plane[(size_t)y * 6 + 2] = 1;
            fx.plane[(size_t)y * 6 + 4] = 0xFF;
            fx.plane[(size_t)y * 6 + 5] = 0xFF;
        }
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 4;
        fx.input.channels[0].mask.height = 3;
        fx.input.channels[0].mask.stride_bytes = 6;
        fx.input.channels[0].mask.plane = &fx.plane;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].overlay_mask && vm.channels[0].mask.positive_pixels == 6,
              "B01_stride_padding_not_counted",
              "positive=" + num(vm.channels[0].mask.positive_pixels));
    }
    {
        // B2 stride < width -> 拒绝
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign(64, 1);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 64;
        fx.input.channels[0].mask.height = 48;
        fx.input.channels[0].mask.stride_bytes = 32;
        fx.input.channels[0].mask.plane = &fx.plane;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(!vm.channels[0].overlay_mask && !vm.channels[0].mask_available &&
                  !vm.channels[0].mask_unavailable_reason.empty(),
              "B02_stride_less_than_width_rejected",
              vm.channels[0].mask_unavailable_reason);
    }
    {
        // B3 plane 差一字节 -> 拒绝（stride*height-1）
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign((size_t)64 * 48 - 1, 1);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 64;
        fx.input.channels[0].mask.height = 48;
        fx.input.channels[0].mask.stride_bytes = 64;
        fx.input.channels[0].mask.plane = &fx.plane;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(!vm.channels[0].overlay_mask && !vm.channels[0].mask_available,
              "B03_plane_one_byte_short_rejected", "");
    }
    {
        // B4 plane 恰好 stride*height -> 可用
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign((size_t)64 * 48, 0);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 64;
        fx.input.channels[0].mask.height = 48;
        fx.input.channels[0].mask.stride_bytes = 64;
        fx.input.channels[0].mask.plane = &fx.plane;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].mask_available, "B04_plane_exact_size_usable", "");
    }
    {
        // B5 available=true 但 plane=null -> 拒绝，reason 缺省回退"掩码不可用"
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "";
        fx.input.channels[0].mask.width = 64;
        fx.input.channels[0].mask.height = 48;
        fx.input.channels[0].mask.stride_bytes = 64;
        fx.input.channels[0].mask.plane = nullptr;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(!vm.channels[0].overlay_mask && !vm.channels[0].mask_available &&
                  vm.channels[0].mask_unavailable_reason == "掩码不可用",
              "B05_null_plane_rejected_fallback_reason",
              vm.channels[0].mask_unavailable_reason);
    }
    {
        // B6 kind 非 rail 透传
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign((size_t)8 * 8, 1);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 8;
        fx.input.channels[0].mask.height = 8;
        fx.input.channels[0].mask.stride_bytes = 8;
        fx.input.channels[0].mask.plane = &fx.plane;
        fx.input.channels[0].mask.kind = "custom";
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].overlay_mask && vm.channels[0].mask.kind == "custom",
              "B06_mask_kind_passthrough", vm.channels[0].mask.kind);
    }
    {
        // B7 全 0 plane -> positive=0（数值 0，不是占位），叠加仍提供
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign((size_t)8 * 8, 0);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 8;
        fx.input.channels[0].mask.height = 8;
        fx.input.channels[0].mask.stride_bytes = 8;
        fx.input.channels[0].mask.plane = &fx.plane;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].overlay_mask && vm.channels[0].mask.positive_pixels == 0,
              "B07_positive_zero_is_numeric_not_placeholder",
              "positive=" + num(vm.channels[0].mask.positive_pixels));
        const std::string j = core::debug_view_json(vm);
        check(j.find("\"positive_pixels\":0") != std::string::npos,
              "B08_json_positive_zero_numeric", "");
    }
    {
        // B9 全 1 plane -> positive 精确 width*height
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.plane.assign((size_t)8 * 8, 1);
        fx.input.channels[0].mask.available = true;
        fx.input.channels[0].mask.reason = "READY";
        fx.input.channels[0].mask.width = 8;
        fx.input.channels[0].mask.height = 8;
        fx.input.channels[0].mask.stride_bytes = 8;
        fx.input.channels[0].mask.plane = &fx.plane;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].mask.positive_pixels == 64,
              "B09_positive_full_plane_exact",
              "positive=" + num(vm.channels[0].mask.positive_pixels));
    }
    {
        // B10 registry 有相机但 channels 为空 + mask 开关 -> 每路 registry 行都给
        //     "无掩码数据"（非崩溃、非 NOT_CAPTURED）。registry 2 路 -> 2 行。
        OverlaySwitches sw;
        sw.mask = true;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.channels.clear();
        DebugViewModel vm = core::build_debug_view(fx.input);
        bool all_reason = vm.channels.size() == 2;
        for (const auto& c : vm.channels)
            if (c.mask_unavailable_reason != "无掩码数据" || c.overlay_mask) all_reason = false;
        check(vm.visible && all_reason,
              "B10_no_channel_input_mask_reason",
              "rows=" + num((long long)vm.channels.size()));
    }

    // ===== C. 字段/角色语义补充 =====
    {
        // C1 admin + 全开关 -> 生效（boxes/text 直接，mask 需可用，roi 需配置）
        OverlaySwitches sw;
        sw.boxes = sw.roi = sw.text = sw.mask = true;
        CoreFix fx = base_core_fix(Role::Admin, sw);
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.visible && vm.overlays_effective && vm.channels[0].overlay_boxes &&
                  vm.channels[0].overlay_text && !vm.channels[0].overlay_roi &&
                  vm.channels[0].roi_unavailable_reason == "未配置 ROI",
              "C01_admin_switches_effective_roi_absent", "");
    }
    {
        // C2 available 覆盖语义锚定：runtime 死但输入路声明可用 -> 以输入为准
        //    （调用方负责一致性；覆盖行为登记为观察项，非缺陷判据）
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.runtime.worker_alive = false;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].row.available && vm.channels[0].unavailable_reason.empty(),
              "C02_available_override_semantics_anchored", "input-authoritative");
    }
    {
        // C3 真 0 与占位区分：envelope ready 但 0 框 -> box_count_text="0"（真 0）
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.channels[0].box_count = 0;
        fx.input.channels[0].row.boxes.clear();
        fx.input.channels[0].row.max_confidence = 0.0f;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].box_count_text == "0" &&
                  vm.channels[0].confidence_text == "—",
              "C03_zero_boxes_real_zero_confidence_dash",
              vm.channels[0].box_count_text + "/" + vm.channels[0].confidence_text);
    }
    {
        // C4 置信度两位小数
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.channels[0].row.max_confidence = 0.5f;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].confidence_text == "0.50",
              "C04_confidence_two_decimals", vm.channels[0].confidence_text);
    }
    {
        // C5 model_version JSON 转义（引号/反斜杠）
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.channels[0].row.model_version = "a\"b\\c";
        DebugViewModel vm = core::build_debug_view(fx.input);
        const std::string j = core::debug_view_json(vm);
        check(j.find("\"model_version\":\"a\\\"b\\\\c\"") != std::string::npos,
              "C05_json_escape_quotes_backslash", "");
    }
    {
        // C6 距离文本透传（调用方负责；空 -> 占位）
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.channels[0].row.distance_text = "123.4 m";
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].distance_text == "123.4 m", "C06_distance_text_passthrough",
              vm.channels[0].distance_text);
    }
    {
        // C7 fps=0 -> "—"（非 "0.0 fps"）
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.runtime.channels[0].fps = 0.0;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].fps_text == "—" && vm.channels[0].row.fps == 0.0,
              "C07_fps_zero_is_unknown", vm.channels[0].fps_text);
    }
    {
        // C8 latency=0（runtime 与输入均 0）-> "—"
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.runtime.channels[0].latency_ms = 0;
        fx.input.channels[0].row.latency_ms = 0;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(vm.channels[0].latency_text == "—", "C08_latency_zero_is_unknown",
              vm.channels[0].latency_text);
    }
    {
        // C9 陈旧原因透传：输入声明不可用 + 原因"推理陈旧" -> 占位 + 原因
        //    （runtime fps/latency 一并清零，验证占位来自"输入声明不可用"路径）
        OverlaySwitches sw;
        CoreFix fx = base_core_fix(Role::Developer, sw);
        fx.input.runtime.channels[0].fps = 0.0;
        fx.input.runtime.channels[0].latency_ms = 0;
        fx.input.channels[0].row.available = false;
        fx.input.channels[0].row.unavailable_reason = "推理陈旧";
        fx.input.channels[0].row.model_version = "";
        fx.input.channels[0].row.last_result_text = "";
        fx.input.channels[0].row.latency_ms = 0;
        fx.input.channels[0].boxes_available = false;
        fx.input.channels[0].box_count = 0;
        fx.input.channels[0].row.boxes.clear();
        fx.input.channels[0].row.max_confidence = 0.0f;
        DebugViewModel vm = core::build_debug_view(fx.input);
        check(!vm.channels[0].row.available &&
                  vm.channels[0].unavailable_reason == "推理陈旧" &&
                  vm.channels[0].fps_text == "—" && vm.channels[0].box_count_text == "—" &&
                  vm.channels[0].latency_text == "—",
              "C09_stale_reason_passthrough",
              vm.channels[0].unavailable_reason);
    }
    {
        // C10 switches_describe 稳定序（boxes,roi,text,mask 子集序）
        OverlaySwitches sw;
        sw.mask = true;
        sw.boxes = true;
        check(sw.describe() == "boxes,mask", "C10_describe_stable_order", sw.describe());
    }

    std::printf("vext RESULT: %s pass=%d fail=%d\n",
                fails == 0 ? "PASS" : "FAIL", passn, fails);
    return fails == 0 ? 0 : 1;
}
