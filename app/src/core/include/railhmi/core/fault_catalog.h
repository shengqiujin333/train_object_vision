#pragma once

// railhmi-core: 故障自检目录（ITEM-025 / DESKTOP §15、TEST_DESIGN FI 目录）。
// 为每种故障注入项（FI-01..FI-05/FI-10）给出用户可见的**显式提示**与**恢复路径**，
// 供设备维护页“故障自检”卡与板端自检清单使用。纯逻辑，无 Qt。

#include <string>
#include <vector>

#include "railhmi/core/simulation.h"

namespace railhmi {
namespace core {

struct FaultPrompt {
    FaultKind kind = FaultKind::StreamCut;
    std::string id;        // "FI-01"
    std::string label;     // 断流
    std::string prompt;    // 界面显式提示
    std::string recovery;  // 恢复路径
};
std::vector<FaultPrompt> fault_catalog();

// 自检清单：注入动作 -> 期望显式提示 -> 恢复路径。
struct SelfCheckStep {
    std::string id;
    std::string inject;          // 注入命令/开关（模拟输入）
    std::string expected_prompt; // 期望界面提示
    std::string recovery_path;   // 恢复路径
};
std::vector<SelfCheckStep> self_check_list();

}  // namespace core
}  // namespace railhmi
