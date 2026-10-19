#pragma once

// railhmi-media: 真实 .rknn 模型的张量描述读取（视觉推理 ITEM-001）。
//
// 通过 dlopen 绑定 librknnrt.so，用 `rknn_query` 读取输入/输出张量数量、属性（形状/布局/
// 量化）与 SDK/驱动版本，产出 `railhmi::core::ModelDescriptor`；不把"文件存在"当能力证据。
// target 专用，但**不引入编译期 rknn 依赖**：宿主无 librknnrt 时也能编译，运行时返回
// `RKNN_RUNTIME_UNAVAILABLE`（不伪造 ready）。
//
// 说明：头文件为最小自声明 ABI，已对照 rknpu2 2.3.2 的 rknn_api.h（/usr/include/rknn_api.h）
// 核验字段顺序与枚举值；rknn_tensor_attr 前缀一致（该版本导出 rknn_destroy 而非 rknn_release）。
// 所有字段读取后都做合理性校验，异常时返回显式失败而非猜测。

#include <string>

#include "railhmi/core/vision_model_capability.h"

namespace railhmi {
namespace media {

struct RknnProbeResult {
    bool ok = false;
    railhmi::core::ModelDescriptor descriptor;
    std::string error;  // 已脱敏说明（失败时）
};

// 读取单个 .rknn 文件的张量描述。path 由配置/CLI 提供；不访问任何设备/相机。
RknnProbeResult probe_rknn_model(const std::string& path);

// librknnrt 是否可在当前环境加载（不加载模型）。version_out 为探测到的库路径/版本标记。
bool rknn_runtime_available(std::string* version_out);

}  // namespace media
}  // namespace railhmi
