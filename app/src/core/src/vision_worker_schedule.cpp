// railhmi-core: 推理工作线程调度与温度守卫实现（视觉推理 ITEM-009 纯逻辑）。

#include "railhmi/core/vision_worker_schedule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

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
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

const char* token_or(const std::string& s, const char* fallback) {
    return s.empty() ? fallback : s.c_str();
}

}  // namespace

bool parse_thermal_millicelsius(const std::string& text, double* celsius, std::string* reason,
                                const ThermalLimits& limits) {
    if (reason) reason->clear();
    const std::string t = trim(text);
    if (t.empty()) {
        if (reason) *reason = thermal_reason::kMalformed();
        return false;
    }
    // 只接受 [sign]digits（sysfs 为整数毫摄氏度；小数点/多字段一律判非法，不做猜测）。
    std::size_t i = 0;
    if (t[0] == '+' || t[0] == '-') i = 1;
    if (i >= t.size()) {
        if (reason) *reason = thermal_reason::kMalformed();
        return false;
    }
    for (std::size_t k = i; k < t.size(); ++k) {
        if (t[k] < '0' || t[k] > '9') {
            if (reason) *reason = thermal_reason::kMalformed();
            return false;
        }
    }
    const double milli = std::strtod(t.c_str(), nullptr);
    const double c = milli / 1000.0;
    if (!std::isfinite(c) || c < limits.min_c || c > limits.max_c) {
        if (reason) *reason = thermal_reason::kOutOfRange();
        return false;
    }
    if (celsius) *celsius = c;
    return true;
}

ThermalSample pick_thermal_sample(const std::vector<std::pair<std::string, std::string>>& zones,
                                  long long now_mono_ms, const ThermalLimits& limits) {
    ThermalSample best;
    std::string first_failure;
    for (std::size_t i = 0; i < zones.size(); ++i) {
        double c = 0.0;
        std::string why;
        if (!parse_thermal_millicelsius(zones[i].second, &c, &why, limits)) {
            if (first_failure.empty()) first_failure = why;
            continue;
        }
        if (!best.valid || c > best.celsius) {
            best.valid = true;
            best.celsius = c;
            best.injected = false;
            best.source = zones[i].first.empty() ? "thermal_zone" + std::to_string(i) : zones[i].first;
            best.zone_index = static_cast<int>(i);
            best.read_mono_ms = now_mono_ms;
            best.reason = thermal_reason::kOk();
        }
    }
    if (!best.valid) {
        best.reason = first_failure.empty() ? thermal_reason::kMissing() : first_failure;
        best.source = "unavailable";
        best.read_mono_ms = now_mono_ms;
        best.zone_index = -1;
    }
    return best;
}

ThermalSample injected_thermal_sample(double celsius, long long now_mono_ms) {
    ThermalSample s;
    s.celsius = celsius;
    s.read_mono_ms = now_mono_ms;
    s.source = "injected";
    s.injected = true;
    s.valid = std::isfinite(celsius);
    s.reason = s.valid ? thermal_reason::kInjected() : thermal_reason::kOutOfRange();
    return s;
}

bool thermal_sample_fresh(const ThermalSample& s, long long now_mono_ms, long long max_age_ms,
                          std::string* reason) {
    if (reason) reason->clear();
    if (!s.valid) {
        if (reason) *reason = token_or(s.reason, thermal_reason::kMissing());
        return false;
    }
    if (max_age_ms <= 0) return true;
    if (s.read_mono_ms <= 0) {
        if (reason) *reason = thermal_reason::kStale();
        return false;
    }
    if (now_mono_ms - s.read_mono_ms > max_age_ms) {
        if (reason) *reason = thermal_reason::kStale();
        return false;
    }
    return true;
}

const char* thermal_state_token(const ThermalSample& s) {
    if (!s.valid) return "UNAVAILABLE";
    return s.injected ? "INJECTED" : "MEASURED";
}

std::string thermal_sample_json(const ThermalSample& s) {
    std::ostringstream o;
    o << "{";
    o << "\"valid\":" << (s.valid ? "true" : "false");
    o << ",\"state\":\"" << thermal_state_token(s) << "\"";
    o << ",\"measured\":" << (s.measured() ? "true" : "false");
    o << ",\"injected\":" << (s.injected ? "true" : "false");
    o << ",\"celsius\":" << (s.valid ? s.celsius : 0.0);
    o << ",\"source\":\"" << json_escape(s.source) << "\"";
    o << ",\"reason\":\"" << json_escape(s.reason) << "\"";
    o << ",\"zone_index\":" << s.zone_index;
    o << ",\"read_mono_ms\":" << s.read_mono_ms;
    o << "}";
    return o.str();
}

AiLaneScheduler::AiLaneScheduler(AiSchedulerConfig cfg) : cfg_(cfg) {}

AiScheduleRound AiLaneScheduler::plan(const std::vector<AiLaneDemand>& demand,
                                      const ThermalSample& thermal) {
    AiScheduleRound r;
    ++round_;
    r.round = round_;
    r.thermal = thermal;
    r.thermal_known = thermal.valid;
    r.budget_configured = cfg_.npu_budget;

    // 1) 预算来源：core::plan_inference（优先级 + 温度守卫，不在此重写预算数学）。
    SchedInput in;
    in.worker_alive = true;
    in.temp_c = thermal.valid ? thermal.celsius : 0.0;
    for (const auto& d : demand) {
        AiChannelRuntime c;
        c.camera_id = d.camera_id;
        c.profile = d.profile;
        in.channels.push_back(c);
    }
    const SchedDecision decided = plan_inference(in, cfg_);
    r.priority_order = decided.run;
    r.temperature_guard = decided.temperature_guard;
    int budget = static_cast<int>(decided.run.size());
    if (budget < 0) budget = 0;
    r.budget_effective = budget;

    // 2) 候选 = 有帧且 AI 职责有效；其余为 idle（不参与等待计算）。
    //    职责不可用（无核验模型，ITEM-010）的路不占预算，单独归入 unavailable。
    std::vector<const AiLaneDemand*> hungry;
    for (const auto& d : demand) {
        if (!ai_eligible(d.profile) || !d.has_frame) {
            r.idle.push_back(d.camera_id);
            continue;
        }
        if (!d.duty_ready) {
            r.unavailable.push_back(d.camera_id);
            continue;
        }
        hungry.push_back(&d);
        if (seen_order_.find(d.camera_id) == seen_order_.end())
            seen_order_[d.camera_id] = order_seq_++;
    }

    // 3) 无饥饿排序：等待轮数降序 -> 优先级升序 -> 注册顺序。
    auto wait_of = [&](const std::string& cam) -> long long {
        auto it = last_run_round_.find(cam);
        return it == last_run_round_.end() ? round_ : (round_ - it->second);
    };
    std::stable_sort(hungry.begin(), hungry.end(),
                     [&](const AiLaneDemand* a, const AiLaneDemand* b) {
                         const long long wa = wait_of(a->camera_id);
                         const long long wb = wait_of(b->camera_id);
                         if (wa != wb) return wa > wb;
                         const int pa = ai_priority(a->profile);
                         const int pb = ai_priority(b->profile);
                         if (pa != pb) return pa < pb;
                         return seen_order_[a->camera_id] < seen_order_[b->camera_id];
                     });

    for (std::size_t i = 0; i < hungry.size(); ++i) {
        if (static_cast<int>(i) < budget) {
            r.run.push_back(hungry[i]->camera_id);
            last_run_round_[hungry[i]->camera_id] = round_;
        } else {
            r.skipped.push_back(hungry[i]->camera_id);
        }
    }

    long long max_wait = 0;
    for (const auto& kv : seen_order_) max_wait = std::max(max_wait, wait_of(kv.first));
    max_wait_rounds_ = max_wait;
    return r;
}

long long AiLaneScheduler::wait_rounds(const std::string& camera_id) const {
    if (seen_order_.find(camera_id) == seen_order_.end()) return 0;
    auto it = last_run_round_.find(camera_id);
    return it == last_run_round_.end() ? round_ : (round_ - it->second);
}

std::map<std::string, long long> AiLaneScheduler::waits() const {
    std::map<std::string, long long> out;
    for (const auto& kv : seen_order_) out[kv.first] = wait_rounds(kv.first);
    return out;
}

std::string schedule_round_json(const AiScheduleRound& r) {
    auto arr = [](const std::vector<std::string>& v) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) s += ",";
            s += "\"" + json_escape(v[i]) + "\"";
        }
        s += "]";
        return s;
    };
    std::ostringstream o;
    o << "{";
    o << "\"round\":" << r.round;
    o << ",\"run\":" << arr(r.run);
    o << ",\"skipped\":" << arr(r.skipped);
    o << ",\"unavailable\":" << arr(r.unavailable);
    o << ",\"idle\":" << arr(r.idle);
    o << ",\"priority_order\":" << arr(r.priority_order);
    o << ",\"temperature_guard\":" << (r.temperature_guard ? "true" : "false");
    o << ",\"thermal_known\":" << (r.thermal_known ? "true" : "false");
    o << ",\"budget_configured\":" << r.budget_configured;
    o << ",\"budget_effective\":" << r.budget_effective;
    o << ",\"thermal\":" << thermal_sample_json(r.thermal);
    o << "}";
    return o.str();
}

}  // namespace core
}  // namespace railhmi
