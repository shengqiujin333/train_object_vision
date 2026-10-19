// railhmi-vision-duty（视觉推理 ITEM-010）
// 其它职责（pantograph/wheel_rail/cab_door）可用性语义探针：
//   * 无专用模型证据 -> unavailable + reason（PROFILE_UNSUPPORTED/MODEL_MISMATCH）+ 语义 unknown；
//   * 配置登记了模型名不构成能力；unknown 永不映射 normal；
//   * 提供“能力声明 + 真实/夹具描述”并逐项核验后才 ready，状态按声明标签映射。
//
// 用法:
//   railhmi-vision-duty --selftest [--json out.json]
//   railhmi-vision-duty --fixture no_model|config_only|missing|mismatch|ready [--json out.json]
//   railhmi-vision-duty --config ./config [--json out.json]
//   railhmi-vision-duty --descriptor desc.txt --duty pantograph --outputs 4 --h 640 --w 640 \
//       --labels pantograph_normal=normal,pantograph_abnormal=abnormal \
//       [--predictions pantograph_normal=0.8] [--expect available|unavailable]
//
// 退出码: 0 通过/一致；1 判据失败；2 参数错误。

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/vision_duty_capability.h"
#include "railhmi/core/vision_model_capability.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

namespace {

int g_fail = 0;
std::ofstream g_fo;
std::vector<std::string> g_lines;

void emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    g_lines.push_back(line);
    if (g_fo) {
        g_fo << line << "\n";
        g_fo.flush();
    }
}

void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name +
         (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}

// ---- 夹具描述 ----
TensorDesc in_tensor(int h, int w, TensorType type = TensorType::Int8) {
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
    return t;
}

// 职责解码支持的类分数输出：每个输出 1 个元素。
TensorDesc out_tensor() {
    TensorDesc t;
    t.dir = TensorDir::Output;
    t.name = "score";
    t.n_dims = 4;
    t.dims = {1, 1, 1, 1};
    t.type = TensorType::Float32;
    t.fmt = TensorFormat::Nchw;
    t.quant = QuantType::None;
    t.n_elems = 1;
    return t;
}

ModelDescriptor fixture_descriptor(int outputs, int h, int w, TensorType type = TensorType::Int8) {
    ModelDescriptor d;
    d.path = "fixture-duty.rknn";
    d.file_name = "fixture-duty.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(in_tensor(h, w, type));
    for (int i = 0; i < outputs; ++i) d.outputs.push_back(out_tensor());
    return d;
}

// 真实 yolov5s 形态（3 × 255×H×W）——用于“登记/同型号不等于职责能力”。
ModelDescriptor yolov5s_like() {
    ModelDescriptor d = fixture_descriptor(0, 640, 640);
    for (int c : {80, 40, 20}) {
        TensorDesc t = out_tensor();
        t.dims = {1, 255, static_cast<uint32_t>(c), static_cast<uint32_t>(c)};
        t.n_elems = 255u * static_cast<uint32_t>(c) * static_cast<uint32_t>(c);
        d.outputs.push_back(t);
    }
    return d;
}

DutyDeclaration decl_of(DutyKind duty, const std::string& name, int outputs) {
    DutyDeclaration d;
    d.duty = duty;
    d.declared_model_name = name;
    d.declared_by = "probe";
    d.input_h = 640;
    d.input_w = 640;
    d.output_count = outputs;
    if (duty == DutyKind::Pantograph) {
        d.labels = {{"pantograph_not_raised", DutyState::NotRaised},
                    {"pantograph_normal", DutyState::Normal},
                    {"pantograph_abnormal", DutyState::Abnormal}};
    } else if (duty == DutyKind::CabDoor) {
        d.labels = {{"door_closed", DutyState::Closed}, {"door_ajar", DutyState::Ajar}};
    } else {
        d.labels = {{"wheel_normal", DutyState::Normal}, {"wheel_abnormal", DutyState::Abnormal}};
    }
    return d;
}

struct Scenario {
    std::string name;
    DutyKind duty = DutyKind::Pantograph;
    bool registered = false;
    std::string registered_name;
    bool declaration = false;
    int declared_outputs = 3;
    bool descriptor = false;
    ModelDescriptor descriptor_value;
    bool has_predictions = false;
    std::vector<DutyPrediction> predictions;
    // 期望
    bool expect_available = false;
    std::string expect_reason;
    DutyState expect_state = DutyState::Unknown;
};

DutyAvailability run_scenario(const Scenario& s, DutyAdapterStats* stats = nullptr) {
    DutyAdapter ad(s.duty, "CAM-XX");
    ad.set_config_registration(s.registered, s.registered_name);
    if (s.declaration) ad.set_declaration(decl_of(s.duty, s.registered_name.empty() ? "duty-model" : s.registered_name, s.declared_outputs));
    if (s.descriptor) ad.set_descriptor(s.descriptor_value);
    DutyAvailability a = ad.evaluate();
    if (s.has_predictions) a = ad.apply(s.predictions);
    if (stats) *stats = ad.stats();
    emit("type=scenario name=" + s.name + " json=" + duty_availability_json(a));
    return a;
}

int run_selftest() {
    emit("type=selftest note=纯逻辑夹具；不加载 RKNN/不连相机");

    // 1) 三职责无模型：unavailable + unknown
    for (DutyKind k : {DutyKind::Pantograph, DutyKind::WheelRail, DutyKind::CabDoor}) {
        Scenario s;
        s.name = std::string("no_model_") + to_string(k);
        s.duty = k;
        const DutyAvailability a = run_scenario(s);
        check(!a.available && a.reason == duty_reason::kProfileUnsupported() &&
                  a.state == DutyState::Unknown && a.semantics_unknown(),
              "no_model_" + std::string(to_string(k)) + "_unavailable_unknown", a.reason);
    }

    // 2) 配置登记模型名不构成能力（TC-T-MIS-01/02）
    {
        Scenario s;
        s.name = "config_only";
        s.duty = DutyKind::CabDoor;
        s.registered = true;
        s.registered_name = "yolov5s";
        const DutyAvailability a = run_scenario(s);
        check(!a.available && a.reason == duty_reason::kProfileUnsupported() &&
                  a.config_registered && a.state == DutyState::Unknown,
              "config_registration_not_capability", a.detail);
        check(a.detail.find("not evidence") != std::string::npos, "config_only_detail", a.detail);
    }

    // 3) 声明但无描述 -> MODEL_MISSING
    {
        Scenario s;
        s.name = "missing";
        s.duty = DutyKind::Pantograph;
        s.registered = true;
        s.registered_name = "pantograph-3state";
        s.declaration = true;
        const DutyAvailability a = run_scenario(s);
        check(!a.available && a.reason == duty_reason::kModelMissing() &&
                  a.state == DutyState::Unknown,
              "declaration_without_descriptor_missing", a.reason);
    }

    // 4) 用 yolov5s 形态冒充职责模型 -> MODEL_MISMATCH
    {
        Scenario s;
        s.name = "mismatch_real_yolov5s_shape";
        s.duty = DutyKind::Pantograph;
        s.registered = true;
        s.registered_name = "yolov5s";
        s.declaration = true;
        s.declared_outputs = 3;
        s.descriptor = true;
        s.descriptor_value = yolov5s_like();
        const DutyAvailability a = run_scenario(s);
        check(!a.available && a.reason == duty_reason::kModelMismatch() &&
                  a.state == DutyState::Unknown,
              "yolov5s_shape_is_mismatch", a.detail);
        bool layout = false;
        for (const auto& m : a.mismatched)
            if (m == "unsupported_decode_layout") layout = true;
        check(layout, "mismatch_reports_decode_layout", "无解码器布局 -> MODEL_MISMATCH");

        // 输出个数不符（声明 2 类 vs 描述 3 输出）。
        Scenario c;
        c.name = "mismatch_output_count";
        c.duty = DutyKind::CabDoor;
        c.registered = true;
        c.registered_name = "cab-door-2state";
        c.declaration = true;
        c.declared_outputs = 2;
        c.descriptor = true;
        c.descriptor_value = fixture_descriptor(3, 640, 640);
        const DutyAvailability ca = run_scenario(c);
        bool out_cnt = false;
        for (const auto& m : ca.mismatched)
            if (m.find("output_count") != std::string::npos) out_cnt = true;
        check(!ca.available && ca.reason == duty_reason::kModelMismatch() && out_cnt,
              "mismatch_reports_output_count", "输出个数不匹配逐项可见");
    }

    // 5) 声明 + 匹配夹具描述 -> ready（状态仍 unknown）
    {
        Scenario s;
        s.name = "ready";
        s.duty = DutyKind::Pantograph;
        s.registered = true;
        s.registered_name = "pantograph-3state";
        s.declaration = true;
        s.descriptor = true;
        s.descriptor_value = fixture_descriptor(3, 640, 640);
        const DutyAvailability a = run_scenario(s);
        check(a.available && a.reason == duty_reason::kReady() && a.state == DutyState::Unknown,
              "declared_verified_ready_but_unknown", "READY != normal");
    }

    // 6) 标签映射：未声明/低分 -> unknown；声明标签 -> 状态（unknown 永不 normal）
    {
        Scenario s;
        s.name = "prediction_unknown";
        s.duty = DutyKind::Pantograph;
        s.registered = true;
        s.registered_name = "pantograph-3state";
        s.declaration = true;
        s.descriptor = true;
        s.descriptor_value = fixture_descriptor(3, 640, 640);
        s.has_predictions = true;
        s.predictions = {{"person", 0.99f}, {"pantograph_normal", 0.2f}};
        const DutyAvailability a = run_scenario(s);
        check(a.available && a.state == DutyState::Unknown,
              "undetermined_prediction_stays_unknown", "未声明/低分不产生 normal");

        Scenario b;
        b.name = "prediction_normal";
        b.duty = DutyKind::Pantograph;
        b.registered = true;
        b.registered_name = "pantograph-3state";
        b.declaration = true;
        b.descriptor = true;
        b.descriptor_value = fixture_descriptor(3, 640, 640);
        b.has_predictions = true;
        b.predictions = {{"pantograph_normal", 0.93f}};
        const DutyAvailability ba = run_scenario(b);
        check(ba.available && ba.state == DutyState::Normal,
              "declared_label_sets_normal", "normal 只能来自已声明标签");

        Scenario c;
        c.name = "prediction_abnormal_door";
        c.duty = DutyKind::CabDoor;
        c.registered = true;
        c.registered_name = "cab-door-2state";
        c.declaration = true;
        c.declared_outputs = 2;
        c.descriptor = true;
        c.descriptor_value = fixture_descriptor(2, 640, 640);
        c.has_predictions = true;
        c.predictions = {{"door_ajar", 0.88f}};
        const DutyAvailability ca = run_scenario(c);
        check(ca.available && ca.state == DutyState::Ajar, "declared_door_label_sets_ajar", "ajar");
    }

    // 7) 适配器不可用时拒绝推理结果
    {
        DutyAdapter ad(DutyKind::WheelRail, "CAM-05");
        ad.set_config_registration(true, "yolov8s-seg");
        const DutyAvailability a = ad.evaluate();
        const DutyAvailability b = ad.apply({{"wheel_abnormal", 0.99f}});
        check(!a.available && b.state == DutyState::Unknown && ad.stats().rejected_unavailable == 1,
              "unavailable_rejects_prediction", "unknown 保持、不产判定");
        Detection d;
        check(!ad.to_detection(1, &d), "unavailable_no_detection", "无判定输出");
    }

    // 8) JSON 可观察面：unknown 显示“未知”，不是“正常”
    {
        DutyAdapter ad(DutyKind::Pantograph, "CAM-04");
        const DutyAvailability a = ad.evaluate();
        const std::string j = duty_availability_json(a);
        check(j.find("\"state_display\":\"未知\"") != std::string::npos &&
                  j.find("\"state_display\":\"正常\"") == std::string::npos,
              "json_unknown_not_normal", j.substr(0, 120));
        std::vector<DutyStatusItem> items;
        for (DutyKind k : {DutyKind::Pantograph, DutyKind::WheelRail, DutyKind::CabDoor}) {
            DutyStatusItem it;
            it.duty = k;
            it.profile = profile_of_duty(k);
            it.camera_id = "CAM-XX";
            it.availability = a;
            items.push_back(it);
        }
        const std::string sj = duty_status_json(items);
        check(sj.find("\"pantograph\"") != std::string::npos &&
                  sj.find("\"wheel_rail\"") != std::string::npos &&
                  sj.find("\"cab_door\"") != std::string::npos,
              "duty_status_json_three_duties", "三职责齐全");
    }

    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return ok ? 0 : 1;
}

int run_fixture(const std::string& name) {
    Scenario s;
    s.name = name;
    if (name == "no_model") {
        s.duty = DutyKind::Pantograph;
        s.expect_reason = duty_reason::kProfileUnsupported();
    } else if (name == "config_only") {
        s.duty = DutyKind::CabDoor;
        s.registered = true;
        s.registered_name = "yolov5s";
        s.expect_reason = duty_reason::kProfileUnsupported();
    } else if (name == "missing") {
        s.duty = DutyKind::WheelRail;
        s.registered = true;
        s.registered_name = "wheel-rail-2state";
        s.declaration = true;
        s.declared_outputs = 2;
        s.expect_reason = duty_reason::kModelMissing();
    } else if (name == "mismatch") {
        s.duty = DutyKind::Pantograph;
        s.registered = true;
        s.registered_name = "yolov5s";
        s.declaration = true;
        s.descriptor = true;
        s.descriptor_value = yolov5s_like();
        s.expect_reason = duty_reason::kModelMismatch();
    } else if (name == "ready") {
        s.duty = DutyKind::Pantograph;
        s.registered = true;
        s.registered_name = "pantograph-3state";
        s.declaration = true;
        s.descriptor = true;
        s.descriptor_value = fixture_descriptor(3, 640, 640);
        s.expect_available = true;
        s.expect_reason = duty_reason::kReady();
    } else {
        std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
        return 2;
    }
    const DutyAvailability a = run_scenario(s);
    const bool ok = a.available == s.expect_available && a.reason == s.expect_reason &&
                    a.state == s.expect_state;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(ok ? 0 : 1));
    return ok ? 0 : 1;
}

// 真实配置：三职责登记情况 -> 全部 unavailable + unknown（TC-T-MIS-01/02 的产品口径）。
int run_config(const std::string& dir) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid: " + dir);
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }
    emit("type=config dir=" + dir);
    std::vector<DutyStatusItem> items;
    int unavailable = 0;
    int unknown = 0;
    for (DutyKind k : {DutyKind::Pantograph, DutyKind::WheelRail, DutyKind::CabDoor}) {
        const AiProfile p = profile_of_duty(k);
        DutyAdapter ad(k, "");
        std::string registered_name;
        for (const auto& m : cfg.settings.app.ai.models) {
            if (m.profile == p) {
                ad.set_config_registration(true, m.name);
                registered_name = m.name;
                break;
            }
        }
        DutyStatusItem it;
        it.duty = k;
        it.profile = p;
        it.availability = ad.evaluate();
        it.stats = ad.stats();
        emit("type=duty name=" + std::string(to_string(k)) + " registered=" + registered_name +
             " available=" + std::string(it.availability.available ? "true" : "false") +
             " reason=" + it.availability.reason + " state=" +
             to_string(it.availability.state) + " display=" +
             duty_state_display_zh(it.availability.state));
        if (!it.availability.available) ++unavailable;
        if (it.availability.semantics_unknown()) ++unknown;
        items.push_back(it);
    }
    emit("type=duty_status json=" + duty_status_json(items));
    check(unavailable == 3 && unknown == 3, "config_three_duties_unavailable_unknown",
          "unavailable=" + std::to_string(unavailable) + " unknown=" + std::to_string(unknown));
    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return ok ? 0 : 1;
}

// 真实描述核验（板端 rknn_query 产物）：声明 vs 实际结构。
int run_descriptor(const std::string& path, const std::string& duty_str, int outputs, int h, int w,
                   const std::string& labels, const std::string& predictions,
                   const std::string& expect) {
    std::ifstream f(path);
    if (!f) {
        emit("type=error descriptor not readable: " + path);
        return 2;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    ModelDescriptor desc;
    std::string err;
    if (!parse_model_descriptor_text(ss.str(), &desc, &err)) {
        emit("type=error descriptor parse failed: " + err);
        return 2;
    }
    DutyKind duty = DutyKind::Pantograph;
    if (!parse_duty_kind(duty_str, &duty)) {
        std::fprintf(stderr, "unknown --duty: %s\n", duty_str.c_str());
        return 2;
    }
    DutyDeclaration d;
    d.duty = duty;
    d.declared_model_name = desc.file_name.empty() ? "declared" : desc.file_name;
    d.declared_by = "cli";
    d.input_h = h;
    d.input_w = w;
    d.output_count = outputs;

    // --labels label=state,label=state
    {
        std::size_t p0 = 0;
        while (p0 <= labels.size() && !labels.empty()) {
            const std::size_t c = labels.find(',', p0);
            const std::string item =
                labels.substr(p0, c == std::string::npos ? std::string::npos : c - p0);
            if (!item.empty()) {
                const std::size_t eq = item.find('=');
                if (eq == std::string::npos) {
                    std::fprintf(stderr, "bad --labels item: %s\n", item.c_str());
                    return 2;
                }
                DutyState st = DutyState::Unknown;
                if (!parse_duty_state(item.substr(eq + 1), &st)) {
                    std::fprintf(stderr, "bad --labels state: %s\n", item.c_str());
                    return 2;
                }
                d.labels.push_back({item.substr(0, eq), st});
            }
            if (c == std::string::npos) break;
            p0 = c + 1;
        }
    }

    DutyEvidenceInput in;
    in.duty = duty;
    in.declaration_provided = true;
    in.declaration = d;
    in.descriptor_present = true;
    in.descriptor = desc;

    DutyAvailability a = evaluate_duty(in);
    emit("type=declared duty=" + duty_str + " outputs=" + std::to_string(outputs) + " hw=" +
         std::to_string(h) + "x" + std::to_string(w) + " labels=" + labels);
    emit("type=evaluation json=" + duty_availability_json(a));
    emit("type=descriptor api=" + desc.runtime_api_version + " drv=" + desc.runtime_drv_version +
         " in=" + std::to_string(desc.inputs.size()) + " out=" +
         std::to_string(desc.outputs.size()));

    // 可选：标签映射
    if (!predictions.empty()) {
        std::vector<DutyPrediction> preds;
        std::size_t p0 = 0;
        while (p0 <= predictions.size()) {
            const std::size_t c = predictions.find(',', p0);
            const std::string item =
                predictions.substr(p0, c == std::string::npos ? std::string::npos : c - p0);
            if (!item.empty()) {
                const std::size_t eq = item.find('=');
                if (eq == std::string::npos) return 2;
                DutyPrediction pr;
                pr.label = item.substr(0, eq);
                pr.score = static_cast<float>(std::atof(item.substr(eq + 1).c_str()));
                preds.push_back(pr);
            }
            if (c == std::string::npos) break;
            p0 = c + 1;
        }
        const DutyLabelResult r = map_duty_prediction(d, preds);
        a.state = r.state;
        emit("type=prediction state=" + std::string(to_string(r.state)) + " display=" +
             duty_state_display_zh(r.state) + " used=" + r.used_label +
             " blocked_unknown=" + std::to_string(r.blocked_unknown_label) +
             " below_threshold=" + std::to_string(r.below_threshold));
    }

    bool ok = true;
    if (expect == "available") ok = a.available;
    else if (expect == "unavailable") ok = !a.available;
    // 不变量：不可用时状态必须 unknown；可用时未推理也必须 unknown。
    if (!a.available && a.state != DutyState::Unknown) ok = false;
    emit(std::string("type=expect ") + (ok ? "ok" : "mismatch") + " expect=" + expect +
         " available=" + (a.available ? "true" : "false"));
    const bool pass = ok && g_fail == 0;
    emit(std::string("RESULT: ") + (pass ? "PASS" : "FAIL") + " failures=" + std::to_string(pass ? 0 : 1));
    return pass ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string config = RAILHMI_CONFIG_DIR;
    std::string out;
    std::string json;
    std::string fixture;
    std::string descriptor;
    std::string duty = "pantograph";
    std::string labels;
    std::string predictions;
    std::string expect = "unavailable";
    int outputs = 0;
    int h = 640;
    int w = 640;
    bool selftest = false;
    bool do_config = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) {
            if (i + 1 < argc) *d = argv[++i];
        };
        if (a == "--config") {
            next(&config);
            do_config = true;
        } else if (a == "--fixture") next(&fixture);
        else if (a == "--descriptor") next(&descriptor);
        else if (a == "--duty") next(&duty);
        else if (a == "--labels") next(&labels);
        else if (a == "--predictions") next(&predictions);
        else if (a == "--expect") next(&expect);
        else if (a == "--outputs") { std::string v; next(&v); outputs = std::atoi(v.c_str()); }
        else if (a == "--h") { std::string v; next(&v); h = std::atoi(v.c_str()); }
        else if (a == "--w") { std::string v; next(&v); w = std::atoi(v.c_str()); }
        else if (a == "--selftest") selftest = true;
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: railhmi-vision-duty --selftest [--out LOG] [--json JSON]\n"
                "       railhmi-vision-duty --fixture no_model|config_only|missing|mismatch|ready\n"
                "       railhmi-vision-duty --config DIR\n"
                "       railhmi-vision-duty --descriptor FILE --duty KIND --outputs N --h H --w W "
                "--labels label=state,... [--predictions label=score,...] "
                "[--expect available|unavailable]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (selftest) {
        rc = run_selftest();
    } else if (!fixture.empty()) {
        rc = run_fixture(fixture);
    } else if (!descriptor.empty()) {
        rc = run_descriptor(descriptor, duty, outputs, h, w, labels, predictions, expect);
    } else if (do_config) {
        rc = run_config(config);
    } else {
        rc = run_selftest();   // 默认自检（与其它视觉探针一致）
    }
    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        if (j) {
            j << "{\"tool\":\"railhmi-vision-duty\",\"failures\":" << g_fail
              << ",\"result\":\"" << (g_fail == 0 ? "PASS" : "FAIL") << "\",\"lines\":[";
            for (std::size_t i = 0; i < g_lines.size(); ++i) {
                if (i) j << ",";
                std::string esc;
                for (char c : g_lines[i]) {
                    if (c == '"' || c == '\\') esc += '\\';
                    esc += (c == '\n' ? ' ' : c);
                }
                j << "\"" << esc << "\"";
            }
            j << "]}\n";
        }
    }
    return rc;
}
