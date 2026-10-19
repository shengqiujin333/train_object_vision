// railhmi 真模型张量描述探针（视觉推理 ITEM-001）
//
// 用 dlopen 绑定 librknnrt.so，对指定 .rknn 文件执行 rknn_query，输出张量描述（文本或 JSON）。
// 文本格式可被 `railhmi-model-probe --descriptor` 直接解析核验。target 侧使用；无库时显式返回
// RKNN_RUNTIME_UNAVAILABLE，不伪造 ready。
//
// 用法: railhmi-rknn-probe --model PATH [--json] [--out FILE] [--json-out FILE]
// 退出码: 0 读取成功；1 读取失败（仍输出带 reason 的描述）；2 参数错误。

#include <cstdio>
#include <fstream>
#include <string>

#include "railhmi/core/vision_model_capability.h"
#include "railhmi/media/rknn_model_probe.h"

using namespace railhmi;

int main(int argc, char** argv) {
    std::string model;
    std::string out;
    std::string json_out;
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) {
            if (i + 1 < argc) *d = argv[++i];
        };
        if (a == "--model") next(&model);
        else if (a == "--out") next(&out);
        else if (a == "--json-out") next(&json_out);
        else if (a == "--json") json = true;
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-rknn-probe --model PATH [--json] [--out FILE] [--json-out FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }
    if (model.empty()) {
        std::fprintf(stderr, "missing --model PATH\n");
        return 2;
    }

    std::string ver;
    const bool runtime_ok = media::rknn_runtime_available(&ver);
    const media::RknnProbeResult r = media::probe_rknn_model(model);

    const std::string text = core::model_descriptor_text(r.descriptor);
    const std::string j = core::model_descriptor_json(r.descriptor);
    const std::string body = json ? j : text;

    std::printf("%s\n", body.c_str());
    if (!runtime_ok) std::fprintf(stderr, "note: rknn runtime unavailable: %s\n", ver.c_str());
    if (!r.ok && !r.error.empty()) std::fprintf(stderr, "probe error: %s\n", r.error.c_str());
    if (!out.empty()) {
        std::ofstream f(out, std::ios::trunc);
        if (f) f << body;
        else std::fprintf(stderr, "cannot write %s\n", out.c_str());
    }
    if (!json_out.empty()) {
        std::ofstream f(json_out, std::ios::trunc);
        if (f) f << j;
        else std::fprintf(stderr, "cannot write %s\n", json_out.c_str());
    }
    return r.ok ? 0 : 1;
}
