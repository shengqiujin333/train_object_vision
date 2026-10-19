#pragma once

// railhmi-media: 最小 RKNN 推理会话（视觉推理 ITEM-009）。
//
// 通过 dlopen 绑定 librknnrt.so（**无编译期 rknn 依赖**），完成 rknn_init / rknn_query /
// rknn_inputs_set / rknn_run / rknn_outputs_get。张量描述与 ITEM-001 的探针同源（rknn_query），
// 输出以 want_float=1 取回反量化后的 float32（布局与 attr 声明的 fmt 一致，本项目为 NCHW）。
//
// 若构建环境存在 `rknn_api.h`，用 static_assert 校验本文件自声明 ABI 的结构体尺寸/字段偏移
// 与官方头一致（防止 ABI 漂移导致内存破坏）；不存在时仍可编译（宿主无 rknn 头）。
//
// 单会话单线程使用（一个工作线程串行推理），不做内部加锁。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "railhmi/core/vision_model_capability.h"

namespace railhmi {
namespace media {

struct RknnOutputTensor {
    uint32_t index = 0;
    const void* buf = nullptr;   // float32 数据（会话内缓冲，有效期至下一次 run/close）
    std::size_t size = 0;        // 字节数
    const railhmi::core::TensorDesc* desc = nullptr;
};

class RknnSession {
public:
    RknnSession();
    ~RknnSession();

    RknnSession(const RknnSession&) = delete;
    RknnSession& operator=(const RknnSession&) = delete;

    // 打开 .rknn 并读取张量描述。失败时 error() 给出稳定说明（不含凭据）。
    bool open(const std::string& path, railhmi::core::ModelDescriptor* descriptor,
              std::string* err);

    bool valid() const;
    void close();

    // 写入输入（data 为 NHWC 连续缓冲，字节数必须等于输入张量字节数）。
    // type 取 rknn_tensor_type 数值（0=float32, 2=int8, 3=uint8）。
    bool set_input(const void* data, std::size_t size, uint32_t type, std::string* err);

    bool run(std::string* err);

    // 取输出（want_float=1）。data 复制到会话内缓冲，desc 指向会话的描述表。
    bool get_outputs_float(std::vector<RknnOutputTensor>* outs, std::string* err);
    void release_outputs();

    const std::vector<railhmi::core::TensorDesc>& inputs() const { return inputs_; }
    const std::vector<railhmi::core::TensorDesc>& outputs() const { return outputs_; }
    const std::string& api_version() const { return api_version_; }
    const std::string& drv_version() const { return drv_version_; }
    const std::string& error() const { return error_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<railhmi::core::TensorDesc> inputs_;
    std::vector<railhmi::core::TensorDesc> outputs_;
    std::string api_version_;
    std::string drv_version_;
    std::string error_;
};

}  // namespace media
}  // namespace railhmi
