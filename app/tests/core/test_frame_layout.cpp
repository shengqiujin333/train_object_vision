// railhmi-core 帧平面布局与校验测试（视频 ITEM-004 纯逻辑 / TC-V-RGA 前置）。
// 验证 NV12/packed 布局推导与显式拒绝：奇数宽高、stride 不足、平面过小、重叠、越界、格式不支持。

#include "test_util.h"

#include <string>

#include "railhmi/core/frame_layout.h"

namespace {

using namespace railhmi::core;

void test_nv12_valid_and_padded() {
    FrameLayout l;
    LayoutStatus s = make_nv12_layout(1280, 720, 1280, 1280, 1280ull * 720, &l);
    railhmi_test::check(s == LayoutStatus::Ok, "nv12 contiguous ok");
    railhmi_test::check(l.planes.size() == 2, "nv12 has two planes");
    railhmi_test::check(l.planes[0].stride_bytes == 1280 && l.planes[0].size_bytes == 1280ull * 720,
                        "nv12 Y plane stride/size");
    railhmi_test::check(l.planes[1].offset == 1280ull * 720 &&
                            l.planes[1].size_bytes == 1280ull * 360,
                        "nv12 UV plane offset/size");
    railhmi_test::check(l.buffer_bytes == 1280ull * 720 * 3 / 2, "nv12 buffer bytes");

    FrameLayout p;
    s = make_nv12_layout(1280, 720, 1408, 1408, 1408ull * 720, &p);
    railhmi_test::check(s == LayoutStatus::Ok, "nv12 padded stride ok");
    railhmi_test::check(p.planes[0].stride_bytes == 1408 && p.planes[0].size_bytes == 1408ull * 720,
                        "nv12 padded Y size uses stride*height");
    railhmi_test::check(p.planes[1].size_bytes == 1408ull * 360, "nv12 padded UV size");
}

void test_nv12_rejections() {
    FrameLayout l;
    railhmi_test::check(make_nv12_layout(1281, 720, 1282, 1282, 1282ull * 720, &l) ==
                            LayoutStatus::OddDimensions,
                        "nv12 odd width rejected");
    railhmi_test::check(make_nv12_layout(1280, 721, 1280, 1280, 1280ull * 722, &l) ==
                            LayoutStatus::OddDimensions,
                        "nv12 odd height rejected");
    railhmi_test::check(make_nv12_layout(1280, 720, 1279, 1280, 1280ull * 720, &l) ==
                            LayoutStatus::StrideTooSmall,
                        "nv12 Y stride < width rejected");
    railhmi_test::check(make_nv12_layout(1280, 720, 1280, 1279, 1280ull * 720, &l) ==
                            LayoutStatus::StrideTooSmall,
                        "nv12 UV stride < width rejected");
    railhmi_test::check(make_nv12_layout(1280, 720, 1280, 1280, 1280ull * 719, &l) ==
                            LayoutStatus::OverlappingPlanes,
                        "nv12 uv offset inside Y rejected");
    railhmi_test::check(make_nv12_layout(0, 720, 1280, 1280, 0, &l) ==
                            LayoutStatus::InvalidDimensions,
                        "nv12 zero width rejected");
}

void test_packed_valid_and_rejections() {
    FrameLayout l;
    LayoutStatus s = make_packed_layout(PixelFormat::Bgr888, 640, 480, 640ull * 3, &l);
    railhmi_test::check(s == LayoutStatus::Ok, "bgr888 contiguous ok");
    railhmi_test::check(l.planes.size() == 1 && l.planes[0].stride_bytes == 1920 &&
                            l.buffer_bytes == 1920ull * 480,
                        "bgr888 single plane stride/size");
    s = make_packed_layout(PixelFormat::Rgb888, 640, 480, 2048, &l);
    railhmi_test::check(s == LayoutStatus::Ok && l.planes[0].stride_bytes == 2048,
                        "rgb888 padded stride ok");
    railhmi_test::check(make_packed_layout(PixelFormat::Bgr888, 640, 480, 1919, &l) ==
                            LayoutStatus::StrideTooSmall,
                        "packed stride < width*3 rejected");
    railhmi_test::check(make_packed_layout(PixelFormat::Nv12, 640, 480, 1920, &l) ==
                            LayoutStatus::UnsupportedFormat,
                        "packed factory rejects NV12");
}

void test_validate_layout_edges() {
    FrameLayout l;
    // 手工构造两个重叠平面（NV12 语义）以触发重叠校验。
    l.width = 4;
    l.height = 4;
    l.format = PixelFormat::Nv12;
    l.buffer_bytes = 100;
    l.planes = {PlaneLayout{0, 4, 20}, PlaneLayout{10, 4, 20}};
    railhmi_test::check(validate_layout(l) == LayoutStatus::OverlappingPlanes,
                        "validate: overlapping planes rejected");

    l.planes = {PlaneLayout{0, 4, 20}, PlaneLayout{30, 4, 20}};
    l.buffer_bytes = 40;  // plane1 结束 50 > 40
    railhmi_test::check(validate_layout(l) == LayoutStatus::ExceedsBuffer,
                        "validate: plane beyond buffer rejected");

    l.buffer_bytes = 100;
    l.planes = {PlaneLayout{0, 0, 20}, PlaneLayout{30, 4, 20}};
    railhmi_test::check(validate_layout(l) == LayoutStatus::StrideTooSmall,
                        "validate: zero stride rejected");

    l.planes = {PlaneLayout{0, 4, 20}, PlaneLayout{30, 4, 20}, PlaneLayout{60, 4, 10}};
    railhmi_test::check(validate_layout(l) == LayoutStatus::TooManyPlanes,
                        "validate: >2 planes rejected");

    l.planes = {PlaneLayout{0, 8, 8}};  // NV12 但只有 1 平面
    railhmi_test::check(validate_layout(l) == LayoutStatus::TooManyPlanes,
                        "validate: nv12 requires two planes");
}

void test_format_tokens() {
    PixelFormat f;
    railhmi_test::check(parse_pixel_format("NV12", &f) && f == PixelFormat::Nv12 &&
                            std::string(to_string(f)) == "NV12",
                        "parse/format NV12");
    railhmi_test::check(parse_pixel_format("BGR888", &f) && f == PixelFormat::Bgr888,
                        "parse BGR888");
    railhmi_test::check(parse_pixel_format("RGB888", &f) && f == PixelFormat::Rgb888,
                        "parse RGB888");
    railhmi_test::check(!parse_pixel_format("I420", &f), "parse rejects I420");
}

}  // namespace

void run_frame_layout_tests() {
    test_nv12_valid_and_padded();
    test_nv12_rejections();
    test_packed_valid_and_rejections();
    test_validate_layout_edges();
    test_format_tokens();
}
