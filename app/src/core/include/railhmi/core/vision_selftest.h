#pragma once

// railhmi-core: 无相机自检与可归档夹具（视觉推理 ITEM-013）。
//
// 目标：在**无相机、无 RKNN 运行时、无模型文件**的条件下，用确定性的合成/夹具输入复现
// 各后处理与可用性路径的判定与关键数值，并输出可归档的结构化 JSON。
//
// 覆盖（VISION_TEST_DESIGN G13 / FI-AI-01/02/04/07/10/12）：
//   模型缺省 / 张量不匹配 / 模型就绪；空帧；无检测；合成图像→期望框；分割→掩码；
//   同帧两阶段命中与缺掩码；陈旧/重复/乱序结果；未知 label 拦截（零报警+计数可见）；
//   AI 不可用；职责不可用；未知值占位。
//
// 边界：本自检**不**声称真机精度、检出率或人工视觉确认；这些列入 `pending`，
// 由 ENV-D/用户另行执行。自检通过只代表这些夹具路径可复现。
//
// 纯标准库；不含 Qt/GStreamer/rknn；不读设备/不读网络。

#include <map>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

struct SelfTestCase {
    std::string id;            // 稳定 id（如 "detect.synthetic_box"）
    std::string area;          // model|preprocess|detect|seg|two_stage|envelope|availability|debug
    std::string requirement;   // 覆盖的测试设计编号（TC/FI）
    bool pass = false;
    std::string detail;        // 一句话结果（已脱敏）
    std::map<std::string, std::string> evidence;   // 关键数值/原因（可归档比对）
};

struct SelfTestReport {
    std::string schema = "railhmi.vision.selftest.v1";
    bool no_device = true;              // 无相机、无 RKNN 前向：仅为夹具/纯逻辑
    std::string environment = "ai-host-fixture";
    std::vector<SelfTestCase> cases;
    std::vector<std::string> pending;   // 明确标注待执行项
    int passed = 0;
    int failed = 0;
    long long timestamp_ms = 0;         // 非确定性字段（归档时可排除）
};

// 运行全部自检用例（确定性；同一构建多次运行除 timestamp 外应逐字相同）。
SelfTestReport run_vision_selftest();

// 结构化 JSON。include_timestamp=false 时输出确定性文本（用于两轮 sha256 恒等）。
std::string selftest_json(const SelfTestReport& r, bool include_timestamp = false);

// 覆盖矩阵（area -> 判例 id），供探针 --list 与文档核对。
std::vector<std::string> selftest_case_ids();

}  // namespace core
}  // namespace railhmi
