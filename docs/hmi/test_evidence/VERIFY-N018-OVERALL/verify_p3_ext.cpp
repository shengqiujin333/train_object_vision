// VERIFY-REPAIR-n008-v2 — 验证方 ext 夹具（R4b：F-01/F-02 修复所触碰的 P3 数据域）。
//
// 范围（验证方独立判域，不重复 core-tests 已断言内容的具体分支表述）：
//  S1 F-01 数据域：build_registry（contract: camera_registry.cpp:26-37 name=CameraConfig.name）
//     对 CAM-01/02/03 透传 cameras.yaml 的中文名（车头前方/车辆左侧/车辆右侧）——QML chipName
//     的数据源（P3Replay.qml:65-70 经 railhmiCameras/allCams 读取）；未知 id 回退语义的数据前提
//     = registry 不含该 id。
//  S2 F-02 数据域：build_timeline keyframe 标记携带非空 thumb 引用（P3Replay.qml:324
//     thumbSource(modelData) 的输入 contract），且 thumb 非 qrc:/ 基址（replay.h:44 媒体层引用）；
//     thumbSource 语义镜像：page-relative 丢弃 -> 占位方框（F-02 修复的判读行为）。
//  S3 同步提示域（P3 三联的同步偏差提示行）：sync_snapshot 以 main.cpp:327-330 的 seed
//     同参数形（CAM-03=900，阈值 300）→ 复现"同步偏差 900 ms（阈值 300 ms）"可达。
//
// 教训沿用：check(ok, name) 参数序（ITEM-005/017/021）；EventRow/聚合初始化以契约头
// 字段序为准，CameraConfig 用指定成员赋值而非位置初始化（config.h:49-62 12 字段）。
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/config.h"
#include "railhmi/core/replay.h"

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const std::string& name) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", name.c_str()); }
    else { ++g_fail; std::printf("[FAIL] %s\n", name.c_str()); }
}

int main() {
    // ---- S1: registry name 数据源（F-01）----
    railhmi::core::AppSettings st;
    const char* rows[3][2] = {{"CAM-01", "车头前方"}, {"CAM-02", "车辆左侧"}, {"CAM-03", "车辆右侧"}};
    const railhmi::core::CameraPosition pos[3] = {
        railhmi::core::CameraPosition::Primary,
        railhmi::core::CameraPosition::Left,
        railhmi::core::CameraPosition::Right};
    for (int i = 0; i < 3; ++i) {
        railhmi::core::CameraConfig c;
        c.id = rows[i][0];
        c.name = rows[i][1];
        c.position = pos[i];
        st.cameras.push_back(c);
    }
    auto reg = railhmi::core::build_registry(st);
    check(reg.size() == 3, "registry 3 slots from config");
    bool f1 = false, f2 = false, f3 = false, order_kept = true;
    for (std::size_t i = 0; i < reg.size(); ++i) {
        const auto& c = reg[i];
        if (c.id == "CAM-01") f1 = (c.name == "车头前方");
        if (c.id == "CAM-02") f2 = (c.name == "车辆左侧");
        if (c.id == "CAM-03") f3 = (c.name == "车辆右侧");
        if (static_cast<int>(i) != c.order) order_kept = false;
    }
    check(f1, "F-01 data: registry CAM-01 name=车头前方");
    check(f2, "F-01 data: registry CAM-02 name=车辆左侧");
    check(f3, "F-01 data: registry CAM-03 name=车辆右侧");
    check(order_kept, "registry order kept (config order -> railhmiCameras index)");

    // ---- S2: keyframe thumb 数据域（F-02）----
    std::vector<railhmi::core::RecordingSegmentRow> segs;
    railhmi::core::RecordingSegmentRow r;
    r.id = 1; r.cam = "CAM-01"; r.t0_ms = 0; r.t1_ms = 300000;
    r.kind = railhmi::core::SegmentKind::Continuous; r.complete = true;
    segs.push_back(r);
    std::vector<railhmi::core::RecordingGapRow> gaps;
    std::vector<railhmi::core::AlarmMark> alarms;   // 无报警 -> 仅关键帧
    auto tl = railhmi::core::build_timeline("CAM-01", 0, segs, gaps, alarms, 30000);
    check(!tl.keyframes.empty(), "timeline keyframes generated");
    int with_thumb = 0, qrc_base = 0;
    for (const auto& k : tl.keyframes) {
        if (!k.thumb.empty()) ++with_thumb;
        if (k.thumb.rfind("qrc:", 0) == 0) ++qrc_base;
    }
    check(with_thumb == static_cast<int>(tl.keyframes.size()),
          "F-02 data: every keyframe carries a thumb reference");
    check(qrc_base == 0, "F-02 data: no keyframe thumb uses qrc: base (media-layer refs)");

    // thumbSource 语义镜像（P3Replay.qml:72-80 同条件）：
    auto thumb_source_like = [](const std::string& in) {
        if (in.empty()) return std::string();
        if (in.rfind("image://", 0) == 0 || in.rfind("file://", 0) == 0 ||
            in.rfind("qrc:/", 0) == 0) return in;
        if (in[0] == '/') return std::string("file://") + in;
        return std::string();
    };
    check(thumb_source_like(tl.keyframes[0].thumb).empty(),
          "thumbSource mirror: media-relative thumb -> placeholder (F-02: 不再报 Image 打不开)");
    check(thumb_source_like("/opt/railhmi/media/thumb/x.bmp") ==
              "file:///opt/railhmi/media/thumb/x.bmp",
          "thumbSource mirror: absolute path prefixed file://");
    check(thumb_source_like("image://thumbs/x").compare("image://thumbs/x") == 0,
          "thumbSource mirror: image:// provider preserved");
    check(thumb_source_like("thumb/CAM-01/1.bmp").empty(),
          "thumbSource mirror: page-relative path dropped (qrc:/qml/pages/thumb 不可达基址)");

    // ---- S3: 同步提示（P3 三联的同步偏差提示行）----
    std::vector<std::string> locked = {"CAM-02", "CAM-01", "CAM-03"};
    std::map<std::string, long long> offs{{"CAM-02", 0}, {"CAM-01", 120}, {"CAM-03", 900}};
    auto snap = railhmi::core::sync_snapshot(locked, 1000, offs, 300);
    check(snap.available, "sync snapshot available with all three clocks");
    check(!snap.in_sync, "sync deviation detected (900 > 300)");
    check(!snap.prompt.empty(), "sync prompt non-empty on P3 tri-view");
    check(snap.max_deviation_ms == 900, "sync max deviation = 900");

    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
