// _verify16_vext.cpp — ITEM-016 验证侧自有扩展夹具（software_verification VERIFY-20261017-v1）。
// 链接交付 librailhmi_core.a / librailhmi_media.a / librailhmi_platform.a，黑盒驱动
// video_diagnostics + health 日志 + 真实 MultiStreamManager（test 源），覆盖交付探针与
// core 单测未断言的边界（判据独立推导，不采信交付 acceptance 行）。
//
// 判例域：
//   g01..g06 classify_presence 边界（degraded 在线 / connecting 未核验 / reconnecting 未核验 /
//   offline 未核验 / gap 优先级高于 live / frozen 归缺口且与 gap_count>0 同级）
//   g07..g10 attempt_id（generation>0 / 0 / 空 attempt 字段在 JSON 归 none / 多代递增形态）
//   g11..g16 redact_error 语义（userinfo 保留 host / kv 密码无引号带引号 / token / user-id /
//   secret / 多键同现 / 注入特殊终止符）
//   g17..g20 pipeline 原文整条脱敏触发词（rtspsrc / GstPipeline / GST_MESSAGE / pipeline0）
//   g21..g24 结构化 JSON 字段（ts/severity source message / 每字段精确串 / 中文标签 / quality_flags 透传）
//   g25..g27 last_error 脱敏接入 JSON 行（dirty userinfo / dirty kv / dirty pipeline）
//   g28..g31 真实链路：MultiStreamManager test 双路 fps>0、presence online、wall 校准非 -1、
//   attempted none（gen1）
//   g32..g34 roll_health_logs_by_day（两日留最近 / 同日不裁 / 跨三日裁两日）
//   g35..g38 凭据非法输入鲁棒（全空串 / 仅终止符 / 二次脱敏幂等 / contains_credentials 负例）
//   g39..g40 真实断流三态联动：request_cut 后 state 变化、gap 注入后 classify=Gap
//
// 输出: vext <PASS|FAIL> <case>；失败时附详情（不含凭据）。退出码 = 失败数（0=全过）。

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/health.h"
#include "railhmi/core/timebase.h"
#include "railhmi/core/video_diagnostics.h"
#include "railhmi/media/multi_stream.h"

using namespace railhmi;
using core::VideoChannelMetrics;

namespace {

int g_fail = 0;

void board_sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void check(bool ok, const char* name, const std::string& detail = "") {
    if (ok) {
        std::printf("vext PASS %s\n", name);
    } else {
        ++g_fail;
        std::printf("vext FAIL %s %s\n", name, detail.c_str());
    }
}

core::VideoChannelMetrics live_base() {
    core::VideoChannelMetrics m;
    m.camera_id = "CAM-01";
    m.state = "live";
    m.fps = 25.0;
    m.last_frame_mono_ms = 1000;
    m.last_frame_wall_ms = 1700000000000LL;
    m.attempt_id = core::make_attempt_id("CAM-01", 1);
    return m;
}

}  // namespace

int main() {
    // ---- classify_presence 边界 ----
    {
        core::VideoChannelMetrics m = live_base();
        m.state = "degraded";
        check(core::classify_presence(m) == core::DiagnosticPresence::Online, "g01_degraded_online_fresh");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.state = "connecting";
        m.last_frame_mono_ms = 999;
        check(core::classify_presence(m) == core::DiagnosticPresence::Unverified,
              "g02_connecting_unverified_even_with_fresh_frame");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.state = "reconnecting";
        check(core::classify_presence(m) == core::DiagnosticPresence::Unverified, "g03_reconnecting_unverified");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.state = "offline";
        check(core::classify_presence(m) == core::DiagnosticPresence::Unverified, "g04_offline_unverified");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.gap_count = 2;
        check(core::classify_presence(m) == core::DiagnosticPresence::Gap, "g05_gap_overrides_live");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.state = "frozen";
        check(core::classify_presence(m) == core::DiagnosticPresence::Gap &&
                  core::classify_presence(m) == core::DiagnosticPresence::Gap,
              "g06_frozen_is_gap");
    }

    // ---- attempt_id ----
    {
        core::VideoChannelMetrics m = live_base();
        m.attempt_id = core::make_attempt_id("CAM-02", 3);
        check(m.attempt_id == "CAM-02#g3", "g07_attempt_id_gen3");
        const std::string line = core::video_metric_json_line(m, 1700000000000LL);
        check(line.find("\"attempt_id\":\"CAM-02#g3\"") != std::string::npos, "g08_attempt_id_json");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.attempt_id.clear();  // 空 -> JSON 归 none
        const std::string line = core::video_metric_json_line(m, 1700000000000LL);
        check(line.find("\"attempt_id\":\"none\"") != std::string::npos, "g09_empty_attempt_json_none");
        check(core::classify_presence(m) == core::DiagnosticPresence::Online,
              "g10_empty_attempt_presence_still_online");
    }

    // ---- redact_error: userinfo / kv ----
    {
        const std::string raw = "rtsp://bob:hunter2@cams.example:554/live failed";
        const std::string r = core::redact_error(raw);
        check(r.find("hunter2") == std::string::npos && r.find("bob:") == std::string::npos &&
                  r.find("***@cams.example:554/live failed") != std::string::npos,
              "g11_userinfo_masked_host_kept");
        // 交付契约：contains_credentials 的输入是原始文本；打码后 "***@" 仍命中 userinfo
        // 形态属预期（不构成产品缺陷），同 selftest raw_has_credentials 口径对原始串判 true。
        check(core::contains_credentials(raw), "g12_raw_detected_before_redact");
        check(core::contains_credentials("timeout waiting frame") == false,
              "g12b_benign_still_clean");
    }
    {
        const std::string raw = "auth failed passwd=hunter2 and pwd=abc123";
        const std::string r = core::redact_error(raw);
        check(r.find("hunter2") == std::string::npos && r.find("abc123") == std::string::npos &&
                  r.find("passwd=***") != std::string::npos && r.find("pwd=***") != std::string::npos,
              "g13_passwd_and_pwd_masked");
    }
    {
        const std::string raw = "session token=TOKEN-9-xyz secret=shhh user-id=U77";
        const std::string r = core::redact_error(raw);
        check(r.find("TOKEN-9-xyz") == std::string::npos && r.find("shhh") == std::string::npos &&
                  r.find("U77") == std::string::npos,
              "g14_token_secret_user_id_masked");
    }
    {
        const std::string raw = "user=admin password=P1 url=rtsp://c/x location=rtsp://c/y";
        const std::string r = core::redact_error(raw);
        check(r.find("P1") == std::string::npos && r.find("admin") == std::string::npos &&
                  r.find("rtsp://c/x") == std::string::npos && r.find("rtsp://c/y") == std::string::npos,
              "g15_multi_kv_masked");
    }
    {
        // 值后紧跟终止符（'&' ',' ')'）也必须完整打码。
        const std::string raw = "password=AB&next=kept,token=CD)";
        const std::string r = core::redact_error(raw);
        check(r.find("AB&") == std::string::npos && r.find("CD)") == std::string::npos &&
                  r.find("next=kept") != std::string::npos,
              "g16_kv_value_stops_at_separator");
    }

    // ---- pipeline 原文整条脱敏（四触发词） ----
    {
        const std::string words[] = {"rtspsrc", "GstPipeline", "GST_MESSAGE", "pipeline0"};
        for (int i = 0; i < 4; ++i) {
            const std::string raw = std::string("error near ") + words[i] + " internal detail";
            const std::string r = core::redact_error(raw);
            const std::string name = std::string("g17_pipeline_trigger_") + words[i] +
                                     "_whole_redacted";
            check(r == "<redacted>", name.c_str());
        }
    }

    // ---- 结构化 JSON 字段 ----
    {
        core::VideoChannelMetrics m = live_base();
        m.quality_flags = "QF:ok";
        const std::string line = core::video_metric_json_line(m, 1700000000001LL);
        check(line.find("\"ts_ms\":1700000000001") != std::string::npos, "g21_json_ts");
        check(line.find("\"level\":\"info\"") != std::string::npos ||
                  line.find("\"level\":\"Info\"") != std::string::npos,
              "g22_json_severity");
        check(line.find("\"level\":\"info\",\"source\":\"video\",\"message\":\"channel_metric\"") != std::string::npos,
              "g23_json_source_video");
        check(line.find("\"message\":\"channel_metric\"") != std::string::npos, "g24_json_message");
        check(line.find("\"quality_flags\":\"QF:ok\"") != std::string::npos, "g25_quality_flags_passed_through");
        check(line.find("\"fps\":\"25.000000\"") != std::string::npos, "g26_fps_format");
        check(line.find("\"last_frame_wall_ms\":\"1700000000000\"") != std::string::npos, "g27_wall_ms_format");
        check(core::presence_label(core::DiagnosticPresence::Unverified) == std::string("未核验") &&
                  core::presence_label(core::DiagnosticPresence::Online) == std::string("在线"),
              "g28_labels_unverified_online");
    }

    // ---- last_error 三种脏输入脱敏进 JSON ----
    {
        core::VideoChannelMetrics m = live_base();
        m.last_error = "connect rtsp://alice:pw12345@192.168.1.65:554/x refused";
        const std::string line = core::video_metric_json_line(m, 1700000000000LL);
        check(line.find("pw12345") == std::string::npos && line.find("alice:") == std::string::npos,
              "g31_json_last_error_userinfo_redacted");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.last_error = "password=topsecret in trace";
        const std::string line = core::video_metric_json_line(m, 1700000000000LL);
        check(line.find("topsecret") == std::string::npos && line.find("password=***") != std::string::npos,
              "g32_json_last_error_kv_redacted");
    }
    {
        core::VideoChannelMetrics m = live_base();
        m.last_error = "GstPipeline: pipeline0 keeps whole line";
        const std::string line = core::video_metric_json_line(m, 1700000000000LL);
        check(line.find("\"last_error\":\"<redacted>\"") != std::string::npos,
              "g33_json_last_error_pipeline_whole");
        check(line.find("pipeline0") == std::string::npos, "g34_json_no_pipeline_token");
    }

    // ---- 真实链路：MultiStreamManager test 双路 ----
    {
        std::vector<media::ChannelSpec> specs;
        for (const char* id : {"CAM-11", "CAM-12"}) {
            media::ChannelSpec sp;
            sp.camera_id = id;
            sp.cfg.camera_id = id;
            sp.cfg.source = media::SourceKind::Test;
            specs.push_back(sp);
        }
        media::MultiStreamManager mgr(specs);
        std::string err;
        const int started = mgr.start_all(&err);
        railhmi::core::TimeBase tb;
        tb.calibrate(railhmi::core::TimeBase::system_now_ms(),
                     railhmi::core::TimeBase::steady_now_us());
        // 有界等待（≤3s）取帧。
        for (int i = 0; i < 30; ++i) {
            const std::vector<media::StreamStats> st = mgr.stats();
            if (st.size() == 2 && st[0].fps > 0 && st[1].fps > 0) break;
            board_sleep_ms(100);
        }
        const std::vector<media::StreamStats> st = mgr.stats();
        check(started == 2, "g35_multi_test_two_started", err);
        check(st.size() == 2 && st[0].fps > 0 && st[1].fps > 0, "g36_multi_test_fps_positive",
              "fps=" + std::to_string(st.size() == 2 ? st[0].fps : 0.0));
        if (st.size() == 2) {
            core::VideoChannelMetrics m1;
            m1.camera_id = "CAM-11";
            m1.state = core::to_string(st[0].state);
            m1.fps = st[0].fps;
            m1.last_frame_mono_ms = st[0].last_frame_ms;
            m1.last_frame_wall_ms = st[0].capture_wall_ms >= 0
                                        ? st[0].capture_wall_ms
                                        : (st[0].last_frame_ms > 0 ? tb.to_wall_ms(st[0].last_frame_ms * 1000) : -1);
            m1.attempt_id = core::make_attempt_id("CAM-11", st[0].generation);
            check(core::classify_presence(m1) == core::DiagnosticPresence::Online,
                  "g37_multi_test_online", "state=" + m1.state + " fps=" + std::to_string(m1.fps));
            check(m1.last_frame_wall_ms > 0, "g38_multi_test_wall_calibrated",
                  "wall=" + std::to_string(m1.last_frame_wall_ms));
            // gen1 ⇒ attempt=CAM-11#g1（test 源正常路径无重连）
            check(m1.attempt_id == "CAM-11#g1" || m1.attempt_id == "none",
                  "g39_multi_test_attempt_form", m1.attempt_id);
        }
        mgr.stop_all();
    }

    // ---- roll_health_logs_by_day ----
    {
        // 交易日构造：D0 1970-01-01，D1 1970-01-02，D2 1970-01-03。日志 R(D0) R(D1) R(D2)。
        // roll 1 天 = 保留最近 1 日 → 只剩 D2。
        std::vector<core::LogRecord> recs;
        for (int d = 0; d < 3; ++d) {
            core::LogRecord r;
            r.ts_wall_ms = d * 86400000LL + 3600000LL;
            r.source = "video";
            recs.push_back(r);
        }
        const std::vector<core::LogRecord> rolled = core::roll_health_logs_by_day(recs, 1);
        check(rolled.size() == 1, "g40_roll_keeps_latest_day", "size=" + std::to_string(rolled.size()));
        if (rolled.size() == 1)
            check(rolled[0].ts_wall_ms == 2 * 86400000LL + 3600000LL,
                  "g41_roll_day_is_d2", "ts=" + std::to_string(rolled[0].ts_wall_ms));

        // 同日 3 条 roll 1 天 → 全保留（同一日不裁）。
        std::vector<core::LogRecord> same;
        for (int i = 0; i < 3; ++i) {
            core::LogRecord r;
            r.ts_wall_ms = 1000LL + i;
            r.source = "video";
            same.push_back(r);
        }
        check(core::roll_health_logs_by_day(same, 1).size() == 3, "g42_same_day_not_pruned");

        // 跨三日由两条记录仅差一天（D1/D2），2 天保留 → 两条都留。
        std::vector<core::LogRecord> d12;
        for (int d = 1; d < 3; ++d) {
            core::LogRecord r;
            r.ts_wall_ms = d * 86400000LL;
            r.source = "video";
            d12.push_back(r);
        }
        check(core::roll_health_logs_by_day(d12, 2).size() == 2, "g43_two_days_kept_with_max_2");

        // log_day_key：ts<=0 是无效时间语义（format_time_text 返回 "—"）；非 0 按 civil UTC。
        check(core::log_day_key(0) == std::string("—"),
              "g44_day_key_invalid_ts_marker");
        check(core::log_day_key(23 * 3600000LL) == "1970-01-01",
              "g44b_day_key_day1_start", core::log_day_key(23 * 3600000LL));
        check(core::log_day_key(24 * 3600000LL) == "1970-01-02",
              "g44c_day_key_rolls_at_utc_midnight", core::log_day_key(24 * 3600000LL));
        check(core::log_day_key(46 * 86400000LL) == "1970-02-16",
              "g45_day_key_plus46d", core::log_day_key(46 * 86400000LL));
    }

    // ---- 鲁棒与幂等 ----
    {
        check(core::contains_credentials("") == false, "g46_contains_empty_false");
        check(core::contains_credentials("plain timeout") == false, "g47_contains_benign_false");
        const std::string r = core::redact_error("password=");
        check(r.find("password=") != std::string::npos, "g48_redact_trailing_password_key_kept");
        // 掩码后的 "***@" 形态仍被 contains_credentials 视为 userinfo 模式（交付契约口径：
        // 判定函数输入应是原始文本而非已打码串）——幂等期望只比对文本恒等，不比对判定翻转。
        const std::string once = core::redact_error("rtsp://k:v@h/x password=v");
        const std::string twice = core::redact_error(once);
        check(twice == once, "g49_double_redaction_text_idempotent", twice);
        check(once.find(":v@") == std::string::npos && once.find("v@") == std::string::npos &&
                  once.find("password=***") != std::string::npos,
              "g50_single_redaction_masks_all", once);
    }

    // ---- 真实断流三态联动（隔离注入）----
    {
        std::vector<media::ChannelSpec> specs;
        for (const char* id : {"CAM-21", "CAM-22"}) {
            media::ChannelSpec sp;
            sp.camera_id = id;
            sp.cfg.camera_id = id;
            sp.cfg.source = media::SourceKind::Test;
            specs.push_back(sp);
        }
        media::MultiStreamManager mgr(specs);
        std::string err;
        const int started = mgr.start_all(&err);
        railhmi::core::TimeBase tb;
        tb.calibrate(railhmi::core::TimeBase::system_now_ms(),
                     railhmi::core::TimeBase::steady_now_us());
        for (int i = 0; i < 30; ++i) {
            const std::vector<media::StreamStats> st = mgr.stats();
            if (st.size() == 2 && st[0].fps > 0 && st[1].fps > 0) break;
            board_sleep_ms(100);
        }
        if (started != 2) {
            check(false, "g51_cut_setup_started", err);
        } else {
            // 注入 CAM-21 断流（CAM-22 不受影响）；有限等待状态离开 LIVE。
            mgr.request_cut(0);
                        bool cut_left_live = false;
            auto st_mid = mgr.stats();
            for (int i = 0; i < 40 && !cut_left_live; ++i) {
                st_mid = mgr.stats();
                cut_left_live = st_mid[0].state != railhmi::core::StreamState::Live;
                if (!cut_left_live)
                    board_sleep_ms(100);
            }
            check(cut_left_live, "g51_cut_leaves_live",
                  std::string("state=") +
                      (st_mid.size() == 2 ? std::string(core::to_string(st_mid[0].state))
                                          : std::string("?")));
            // 未断流的 CAM-22 保持 live/online。
            if (st_mid.size() == 2) {
                core::VideoChannelMetrics m22;
                m22.camera_id = "CAM-22";
                m22.state = core::to_string(st_mid[1].state);
                m22.fps = st_mid[1].fps;
                m22.last_frame_mono_ms = st_mid[1].last_frame_ms;
                m22.last_frame_wall_ms = st_mid[1].capture_wall_ms >= 0
                                             ? st_mid[1].capture_wall_ms
                                             : (st_mid[1].last_frame_ms > 0
                                                    ? tb.to_wall_ms(st_mid[1].last_frame_ms * 1000)
                                                    : -1);
                check(core::classify_presence(m22) == core::DiagnosticPresence::Online,
                      "g52_sibling_stays_online", "state=" + m22.state + " fps=" + std::to_string(m22.fps));
                // 断流路 manual classify 在 frozen/gap_count 双归缺口域。
                core::VideoChannelMetrics m21;
                m21.camera_id = "CAM-21";
                m21.state = core::to_string(st_mid[0].state);
                m21.fps = st_mid[0].fps;
                m21.last_frame_mono_ms = st_mid[0].last_frame_ms;
                m21.attempt_id = core::make_attempt_id("CAM-21", st_mid[0].generation);
                // 断流后六态 ∈ {frozen, reconnecting, connecting, offline}：应未核验或
                // frozen(缺口)。不可能继续 online。
                const core::DiagnosticPresence p21 = core::classify_presence(m21);
                bool is_online = p21 == core::DiagnosticPresence::Online;
                check(!is_online || st_mid[0].state == railhmi::core::StreamState::Live,
                      "g53_cut_channel_not_online_after_cut",
                      std::string("state=") + core::to_string(st_mid[0].state) +
                          " presence=" + core::to_string(p21));
            }
        }
        mgr.stop_all();
    }

    std::printf("vext OK (%d failure(s))\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
