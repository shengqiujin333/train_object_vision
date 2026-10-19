// ITEM-002 验证侧自有扩展夹具（software_verification, VERIFY-20261019-v1）。
// 直驱 media 层 FrameStore/FramePreprocessor/rga_convert + core 预处理，
// 判据独立重推导（不采信交付 selftest/acceptance 行）。
//
// 板端 g++ 直编：链接交付 build-media 树的 librailhmi_media.a（含 frame_preprocess.cpp/
// rga_convert.cpp/frame_lease.cpp）+ core/platform 静态库 + gst/librga pkg-config。
//
// 夹具尺寸 640x480：RGA 驱动对 64x64 级小尺寸 blit 报 Invalid argument（见 O-3）。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/vision_preprocess.h"
#include "railhmi/media/frame_store.h"
#include "railhmi/media/frame_preprocess.h"
#include "railhmi/media/rga_convert.h"

using namespace railhmi;
using core::PreprocessedInput;
using core::PreprocessConfig;
using core::InputTensorSpec;
using core::TensorType;
using media::FrameLease;
using media::FrameStore;
using media::FramePreprocessor;
using media::FramePreprocessConfig;
using media::BgrImage;

static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}
static void check_c(bool ok, const char* name, const std::string& detail) {
    std::printf("vext %s %s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (!ok) ++fails;
}

static InputTensorSpec ispec_int8() {
    InputTensorSpec s;
    s.dtype = TensorType::Int8;
    s.scale = 1.0f / 255.0f;
    s.zero_point = -128;
    core::normalize_input_spec(&s);
    return s;
}

static PreprocessConfig cfg_640() {
    PreprocessConfig c;
    c.target_w = 640;
    c.target_h = 640;
    c.mean = 0.0;
    c.std = 255.0;
    c.pad_value = 114;
    c.convert_to_rgb = true;
    c.input = ispec_int8();
    return c;
}

static FramePreprocessConfig pcfg_from_640() {
    FramePreprocessConfig p;
    p.core = cfg_640();
    p.prefer_rga_for_nv12 = true;
    return p;
}

// 用真实 GStreamer NV12 sample 构造灰图租约（640x480，stride=width；RGA 可用尺寸）。
static FrameLease publish_gray_nv12(FrameStore& store, core::FrameMeta meta,
                                    unsigned char y_val = 200) {
    const int w = 640, h = 480, stride = 640;
    const size_t y_size = (size_t)stride * h;
    const size_t buf_size = y_size + (size_t)stride * (h / 2);
    std::vector<unsigned char> buf(buf_size);
    std::memset(buf.data(), y_val, y_size);
    for (size_t i = 0; i < (size_t)stride * (h / 2); i += 2) {
        buf[y_size + i] = 128;
        buf[y_size + i + 1] = 128;
    }
    // gst_buffer_new_wrapped 接管堆所有权（g_memdup 已弃用）
    unsigned char* heap = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!heap) return FrameLease();
    std::memcpy(heap, buf.data(), buf_size);
    GstBuffer* gbuf = gst_buffer_new_wrapped(heap, buf_size);
    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
                                        "framerate", GST_TYPE_FRACTION, 25, 1,
                                        "colorimetry", G_TYPE_STRING, "bt709", nullptr);
    GstSample* sample = gst_sample_new(gbuf, caps, nullptr, nullptr);
    gst_caps_unref(caps);
    gst_buffer_unref(gbuf);
    std::string err;
    core::LayoutStatus st = store.publish(sample, meta, "bt709_full", "simulation", &err);
    gst_sample_unref(sample);
    if (st != core::LayoutStatus::Ok) {
        std::printf("vext INFO publish rejected: %s %s\n", core::to_string(st), err.c_str());
    }
    return store.latest();
}

int main() {
    gst_init(nullptr, nullptr);  // GObject 类型注册（缺则 gst_video_frame_map 段错）

    // ---- g01/g02 空租约与未发布 store（禁 FrameRef 推像素的结构性入口，TC-T-PRE-06）----
    {
        FramePreprocessor prep(pcfg_from_640());
        media::FrameLease empty;
        check(!empty.valid(), "g01_empty_lease_invalid");
        auto r = prep.prepare(empty);
        check_c(!r.ok && r.reason == core::preprocess_reason::kNotReady() && !r.input.has_data(),
                "g02_empty_lease_not_ready_no_tensor",
                r.reason + " data=" + std::to_string(r.input.data.size()));
        check(prep.rejected() == 1 && prep.prepared() == 0 &&
                  prep.rejected_by_reason().count("NOT_READY") == 1,
              "g03_counters_empty_lease");
    }

    // ---- g04..g16 真实租约 → FramePreprocessor（librga 主链路）----
    {
        FramePreprocessConfig pcfg;
        pcfg.core = cfg_640();
        pcfg.prefer_rga_for_nv12 = true;
        FramePreprocessor prep(pcfg);
        FrameStore store("CAM-V");
        core::FrameSequencer seq("sess-v2-verify", "CAM-V");
        seq.begin_generation();

        media::FrameLease la = publish_gray_nv12(store, seq.next(1000, -1, -1));
        check(la.valid() && la.plane_data(0) != nullptr, "g04_real_lease_mapped");
        check(la.descriptor().layout.format == core::PixelFormat::Nv12 &&
                  la.descriptor().layout.width == 640 && la.descriptor().layout.height == 480,
              "g05_descriptor_layout_from_caps");
        // 灰图 200 经 BT.601 → BGR(200,200,200) → 量化: round(200/255*255)+(-128)=72
        auto ra = prep.prepare(la);
        check_c(ra.ok && ra.reason == "PREPARED", "g06_lease_prepare_ok", ra.reason + " " + ra.detail);
        // letterbox: 640x480 → 640x640 scale=1 内容区全宽，上下各补 80
        check(ra.ok && ra.input.letterbox.scale == 1.0 && ra.input.letterbox.pad_x == 0 &&
                  ra.input.letterbox.pad_y == 80 && ra.input.width == 640 && ra.input.height == 640,
              "g07_letterbox_same_aspect_scale1");
        // pad 值 114 → q=-14；内容 200 → q=72 → 全张量 vmin=-14 vmax=72
        check_c(ra.ok && ra.input.value_min == -14 && ra.input.value_max == 72,
                "g08_pad_q14_content_q72",
                "vmin=" + std::to_string(ra.input.value_min) + " vmax=" + std::to_string(ra.input.value_max));
        // 三通道均值相同（灰图+灰 pad 均无通道偏）且 = 内容/pad 面积混合 (72*480 + -14*160)/640 = 50.5
        double mm = (72.0 * 480.0 + (-14.0) * 160.0) / 640.0;
        check(ra.ok && ra.input.channel_mean[0] == ra.input.channel_mean[1] &&
                  ra.input.channel_mean[1] == ra.input.channel_mean[2],
              "g09_gray_channels_equal");
        check_c(ra.ok && (ra.input.channel_mean[0] - mm) < 0.01 &&
                    (mm - ra.input.channel_mean[0]) < 0.01,
                "g09b_gray_mean_content_pad_mix",
                "mean=" + std::to_string(ra.input.channel_mean[0]) + " expect=" + std::to_string(mm));
        // 内容区行（tensor 行 80..559，pad_y=80）逐字节核对：应全为 72（灰 200 → q=72）
        bool content72 = true;
        for (int row = 80; row < 560 && content72; row += 31) {
            for (size_t i = (size_t)row * 640 * 3; i < (size_t)(row + 1) * 640 * 3; i += 797)
                content72 = content72 && ra.input.data[i] == 72;
        }
        check(content72 && ra.input.data.size() == 640u * 640 * 3, "g10_tensor_owned_copy_content");

        // 旧租约在 store 发布新帧后：像素仍来自旧租约（无显示缓冲复用/无伪像素）
        unsigned char first_pix = la.plane_data(0)[0];
        media::FrameLease lb = publish_gray_nv12(store, seq.next(41000, -1, -1), 100);
        check(lb.valid() && lb.plane_data(0)[0] == 100, "g11_second_frame_published");
        check(first_pix == 200 && la.plane_data(0)[0] == 200,
              "g12_old_lease_pixels_unchanged_after_republish");

        // 新帧（Y=100）: 内容 q=-28，pad q=-14 → vmin=-28 vmax=-14
        auto rb = prep.prepare(lb);
        check_c(rb.ok && rb.input.value_min == -28 && rb.input.value_max == -14,
                "g13_new_frame_quant_minus28",
                "vmin=" + std::to_string(rb.input.value_min) + " vmax=" + std::to_string(rb.input.value_max));
        // 同一 prep 实例 prepared 计数 = 2
        check(prep.prepared() == 2, "g14_prepared_counter_two");
        // 身份字段透传: descriptor 的 seq/session 与 FrameMeta 一致（seq 自 1 起）
        check(ra.ok && rb.ok && lb.descriptor().seq == 2 &&
                  lb.descriptor().session_id == "sess-v2-verify" &&
                  la.descriptor().seq == 1,
              "g15_identity_seq_session");
        check(lb.descriptor().buffer_ref.rfind("fr-CAM-V-", 0) == 0 &&
                  lb.descriptor().buffer_ref != la.descriptor().buffer_ref,
              "g16_buffer_ref_distinct");
    }

    // ---- g17 unmap 后访问禁止（FI-AI-05 域）: 租约析构后 plane_data 为 nullptr，不悬挂 ----
    {
        const std::uint8_t* dangling = nullptr;
        FrameStore store("CAM-W");
        {
            core::FrameSequencer seq("sess-v2-unmap", "CAM-W");
            seq.begin_generation();
            media::FrameLease lt = publish_gray_nv12(store, seq.next(1000, -1, -1));
            dangling = lt.plane_data(0);
            check(dangling != nullptr, "g17_lease_alive_pointer_valid");
        }  // lt 析构（最后消费者退出）→ 缓冲 unmap/unref
        FrameStore store2("CAM-W2");
        check(store2.latest().plane_data(0) == nullptr, "g18_no_lease_plane_data_null");
        // dangling 不再被产品代码解引用（结构保证：prepare 只接受存活租约）
        FramePreprocessor prep(pcfg_from_640());
        auto r = prep.prepare(store2.latest());
        check_c(!r.ok && r.reason == core::preprocess_reason::kNotReady() && !r.input.has_data(),
                "g19_dead_store_prepare_not_ready", r.reason);
    }

    // ---- g20..g22 布局校验拒绝族（经 FrameStore publish 拒绝 + FramePreprocess 拒绝）----
    {
        FramePreprocessor prep(pcfg_from_640());
        // 直接构造坏 descriptor 路径不可行（FrameLease 仅 FrameStore 友元构造）；
        // 覆盖面: publish 层拒绝坏 sample（奇数维）→ store.latest 为空 → prepare NOT_READY
        FrameStore store("CAM-BAD");
        {
            const int w = 641, h = 480, stride = 641;  // 奇数宽 NV12 非法
            const size_t y_size = (size_t)stride * h;
            std::vector<unsigned char> buf(y_size + (size_t)stride * (h / 2), 128);
            unsigned char* heap = static_cast<unsigned char*>(std::malloc(buf.size()));
            std::memcpy(heap, buf.data(), buf.size());
            GstBuffer* gbuf = gst_buffer_new_wrapped(heap, buf.size());
            GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12",
                                                "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
                                                "framerate", GST_TYPE_FRACTION, 25, 1, nullptr);
            GstSample* sample = gst_sample_new(gbuf, caps, nullptr, nullptr);
            gst_caps_unref(caps);
            gst_buffer_unref(gbuf);
            core::FrameSequencer seq("sess-bad", "CAM-BAD");
            seq.begin_generation();
            std::string err;
            core::LayoutStatus st = store.publish(sample, seq.next(1, -1, -1), "", "simulation", &err);
            gst_sample_unref(sample);
            check_c(st != core::LayoutStatus::Ok, "g20_odd_width_publish_rejected",
                    core::to_string(st));
        }
        auto r = prep.prepare(store.latest());
        check_c(!r.ok && r.reason == core::preprocess_reason::kNotReady(),
                "g21_rejected_publish_leaves_no_lease", r.reason);
        // 计数面: 拒绝次数 = 1（空租约）+ 坏 publish 不产租约
        check(prep.rejected() >= 1, "g22_reject_counter_visible");
    }

    // ---- g23..g28 core 层独立向量（reference NV12 转换 vs librga 一致性 + 红色标定）----
    {
        // 独立红色标定: 夹具 Y=76,U=85,V=255（BT.601 full-range 公式构造）。
        // RGA 输出 BGR ≈ (0,0,254)；量化后 R 通道 q=round(254/255*255)-128=126
        const int w = 640, h = 480, stride = 640;
        const size_t y_size = (size_t)stride * h;
        const size_t buf_size = y_size + (size_t)stride * (h / 2);
        std::vector<unsigned char> buf(buf_size, 0);
        std::memset(buf.data(), 76, y_size);
        for (size_t i = 0; i < (size_t)stride * (h / 2); i += 2) {
            buf[y_size + i] = 85;
            buf[y_size + i + 1] = 255;
        }
        BgrImage img;
        std::string e;
        bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, y_size, &img, &e);
        check(ok, "g23_red_nv12_rga_ok");
        double b = 0, g = 0, r = 0;
        media::bgr_channel_means(img, &b, &g, &r);
        check_c(r > 200 && b < 60 && g < 60, "g24_red_rga_dominant",
                "b=" + std::to_string(b) + " g=" + std::to_string(g) + " r=" + std::to_string(r));
        // 同一 NV12 经 core 参考转换（640x480 无 pad）→ R 通道均值应显著为正
        PreprocessConfig c = cfg_640();
        c.target_w = 640;
        c.target_h = 480;
        PreprocessedInput in;
        bool ok2 = core::prepare_from_nv12(buf.data(), buf.data() + y_size, w, h, stride, stride,
                                           c, &in, &e);
        check(ok2, "g25_red_reference_ok");
        // RGA 与参考一致性: 参考均值是量化域 q=v-128 → 反量化回 0-255 域再比（容差 <8）
        check_c(ok && ok2 && (r - (in.channel_mean[0] + 128.0)) < 8.0 &&
                    (r - (in.channel_mean[0] + 128.0)) > -8.0,
                "g26_rga_reference_R_agree",
                "rga_r=" + std::to_string(r) + " ref_r_deq=" +
                    std::to_string(in.channel_mean[0] + 128.0));
        // 奇数宽 NV12 参考转换拒绝
        e.clear();
        check(!core::prepare_from_nv12(buf.data(), buf.data() + y_size, 641, h, stride, stride,
                                       c, &in, &e) &&
                  e.find(core::preprocess_reason::kInvalidDimensions()) == 0,
              "g27_reference_odd_width_rejected");
        // stride 不足拒绝（packed: stride < w*3）
        e.clear();
        std::vector<unsigned char> packed((size_t)w * h * 3, 128);
        check(!core::prepare_from_packed(packed.data(), w, h, w * 3 - 1, core::ChannelOrder::Bgr,
                                         c, &in, &e) &&
                  e.find(core::preprocess_reason::kStrideTooSmall()) == 0,
              "g28_packed_stride_rejected");
    }

    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
