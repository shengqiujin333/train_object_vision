// railhmi-media: .rknn 张量描述读取实现（视觉推理 ITEM-001）。
//
// 运行库通过 dlopen 绑定，避免编译期依赖；rknn_query 的结构体为最小自声明 ABI，
// 查询缓冲区额外预留空间，读到的字段全部做合理性校验，异常显式失败。

#include "railhmi/media/rknn_model_probe.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace railhmi {
namespace media {

namespace {

// ---- rknpu2 runtime C API 最小自声明（稳定前缀） ----
using rknn_context = uint64_t;

struct rknn_input_output_num {
    uint32_t n_input;
    uint32_t n_output;
};

struct rknn_sdk_version {
    char api_version[256];
    char drv_version[256];
};

// rknn_tensor_attr 前缀（字段顺序与 rknpu2 2.x 一致）；额外预留缓冲防止库写入更多字段。
struct rknn_tensor_attr_min {
    uint32_t index;
    uint32_t n_dims;
    uint32_t dims[16];
    char name[256];
    uint32_t n_elems;
    uint32_t size;
    uint32_t fmt;   // rknn_tensor_format
    uint32_t type;  // rknn_tensor_type
    uint32_t qnt_type;
    int8_t fl;
    int32_t zp;
    float scale;
    uint32_t w_stride;
    uint32_t size_with_stride;
    uint8_t pass_through;
    uint32_t h_stride;
};
static constexpr size_t kAttrHeadroom = 1024;

// 查询命令
constexpr uint32_t kQueryInOutNum = 0;
constexpr uint32_t kQueryInputAttr = 1;
constexpr uint32_t kQueryOutputAttr = 2;
constexpr uint32_t kQuerySdkVersion = 5;
constexpr int kRknnSucc = 0;

using fn_init = int (*)(rknn_context*, void*, uint32_t, uint32_t, void*);
using fn_query = int (*)(rknn_context, uint32_t, void*, uint32_t);
using fn_release = int (*)(rknn_context);
// rknpu2 2.3.x 导出 rknn_destroy；部分版本改名为 rknn_release。两者都尝试。
const char* kReleaseSymbols[] = {"rknn_release", "rknn_destroy"};

struct Runtime {
    void* handle = nullptr;
    fn_init init = nullptr;
    fn_query query = nullptr;
    fn_release release = nullptr;
    std::string error;
};

Runtime& runtime() {
    static Runtime rt;
    static bool tried = false;
    if (tried) return rt;
    tried = true;
    rt.handle = ::dlopen("librknnrt.so", RTLD_LAZY | RTLD_LOCAL);
    if (!rt.handle) rt.handle = ::dlopen("librknnrt.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!rt.handle) {
        rt.error = "librknnrt.so not loadable: ";
        const char* e = ::dlerror();
        rt.error += e ? e : "unknown";
        return rt;
    }
    rt.init = reinterpret_cast<fn_init>(::dlsym(rt.handle, "rknn_init"));
    rt.query = reinterpret_cast<fn_query>(::dlsym(rt.handle, "rknn_query"));
    for (const char* sym : kReleaseSymbols) {
        rt.release = reinterpret_cast<fn_release>(::dlsym(rt.handle, sym));
        if (rt.release) break;
    }
    if (!rt.init || !rt.query || !rt.release) {
        rt.error = "librknnrt.so missing rknn_init/rknn_query/rknn_release(rknn_destroy)";
    }
    return rt;
}

railhmi::core::TensorType map_type(uint32_t v) {
    using T = railhmi::core::TensorType;
    switch (v) {
        case 0: return T::Float32;
        case 1: return T::Float16;
        case 2: return T::Int8;
        case 3: return T::Uint8;
        case 4: return T::Int16;
        case 5: return T::Uint16;
        case 6: return T::Int32;
        case 7: return T::Uint32;
        case 8: return T::Int64;
        case 9: return T::Bool;
        default: return T::Unknown;
    }
}

railhmi::core::TensorFormat map_fmt(uint32_t v) {
    using F = railhmi::core::TensorFormat;
    switch (v) {
        case 0: return F::Nchw;
        case 1: return F::Nhwc;
        case 2: return F::Nc1hwc2;
        case 3: return F::Undefined;
        default: return F::Unknown;
    }
}

railhmi::core::QuantType map_qnt(uint32_t v) {
    using Q = railhmi::core::QuantType;
    switch (v) {
        case 0: return Q::None;
        case 1: return Q::Dfp;
        case 2: return Q::Affine;
        case 3: return Q::LayerNorm;
        default: return Q::Unsupported;
    }
}

bool read_file(const std::string& path, std::vector<char>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(n));
    f.read(out->data(), n);
    return f.good() || f.eof();
}

// 单个张量查询：优先用库自身结构尺寸；如返回参数错误再尝试带预留的更大尺寸。
// 任何情况下都只读取经合理性校验的前缀字段。
bool query_tensor(Runtime& rt, rknn_context ctx, uint32_t cmd, uint32_t index,
                  railhmi::core::TensorDesc* out, std::string* err) {
    std::vector<uint8_t> buf(sizeof(rknn_tensor_attr_min) + kAttrHeadroom, 0);
    auto* attr = reinterpret_cast<rknn_tensor_attr_min*>(buf.data());
    attr->index = index;
    int rc = rt.query(ctx, cmd, buf.data(), static_cast<uint32_t>(sizeof(rknn_tensor_attr_min)));
    if (rc != kRknnSucc) {
        std::memset(buf.data(), 0, buf.size());
        attr = reinterpret_cast<rknn_tensor_attr_min*>(buf.data());
        attr->index = index;
        rc = rt.query(ctx, cmd, buf.data(), static_cast<uint32_t>(buf.size()));
    }
    if (rc != kRknnSucc) {
        *err = "rknn_query attr failed rc=" + std::to_string(rc) + " index=" + std::to_string(index);
        return false;
    }
    if (attr->index != index) {
        *err = "rknn_tensor_attr layout mismatch (index echo != requested)";
        return false;
    }
    if (attr->n_dims == 0 || attr->n_dims > 16) {
        *err = "rknn_tensor_attr n_dims implausible = " + std::to_string(attr->n_dims);
        return false;
    }
    railhmi::core::TensorDesc t;
    t.index = index;
    t.name = std::string(attr->name, ::strnlen(attr->name, sizeof(attr->name)));
    t.n_dims = attr->n_dims;
    for (uint32_t i = 0; i < attr->n_dims; ++i) {
        if (i < 8) {
            t.dims.push_back(attr->dims[i]);
        } else {
            t.extra_dims += 1;
        }
    }
    t.type = map_type(attr->type);
    t.fmt = map_fmt(attr->fmt);
    t.quant = map_qnt(attr->qnt_type);
    t.zp = attr->zp;
    t.scale = attr->scale;
    t.n_elems = attr->n_elems;
    t.size = attr->size;
    *out = t;
    return true;
}

}  // namespace

bool rknn_runtime_available(std::string* version_out) {
    Runtime& rt = runtime();
    if (!rt.handle || !rt.init || !rt.query || !rt.release) {
        if (version_out) *version_out = rt.error.empty() ? "unavailable" : rt.error;
        return false;
    }
    if (version_out) *version_out = "ok";
    return true;
}

RknnProbeResult probe_rknn_model(const std::string& path) {
    RknnProbeResult result;
    result.descriptor.path = path;
    const size_t slash = path.find_last_of("/\\");
    result.descriptor.file_name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    std::vector<char> model;
    const bool file_ok = read_file(path, &model);
    result.descriptor.file_present = file_ok;

    Runtime& rt = runtime();
    if (!rt.handle || !rt.init || !rt.query || !rt.release) {
        result.descriptor.load_status = railhmi::core::vision_reason::kRknnRuntimeUnavailable();
        result.error = rt.error;
        return result;
    }
    if (!file_ok) {
        result.descriptor.load_status = railhmi::core::vision_reason::kModelMissing();
        result.error = "model file not readable";
        return result;
    }

    rknn_context ctx = 0;
    const int rc = rt.init(&ctx, model.data(), static_cast<uint32_t>(model.size()), 0, nullptr);
    if (rc != kRknnSucc) {
        result.descriptor.load_status = railhmi::core::vision_reason::kModelLoadFailed();
        result.error = "rknn_init failed rc=" + std::to_string(rc);
        return result;
    }

    rknn_sdk_version ver;
    std::memset(&ver, 0, sizeof(ver));
    if (rt.query(ctx, kQuerySdkVersion, &ver, sizeof(ver)) == kRknnSucc) {
        result.descriptor.runtime_api_version =
            std::string(ver.api_version, ::strnlen(ver.api_version, sizeof(ver.api_version)));
        result.descriptor.runtime_drv_version =
            std::string(ver.drv_version, ::strnlen(ver.drv_version, sizeof(ver.drv_version)));
    } else {
        result.descriptor.runtime_api_version = "unknown(api query failed)";
        result.descriptor.runtime_drv_version = "unknown(drv query failed)";
    }

    rknn_input_output_num io;
    std::memset(&io, 0, sizeof(io));
    if (rt.query(ctx, kQueryInOutNum, &io, sizeof(io)) != kRknnSucc || io.n_input == 0 ||
        io.n_output == 0 || io.n_input > 64 || io.n_output > 256) {
        rt.release(ctx);
        result.descriptor.load_status = railhmi::core::vision_reason::kModelLoadFailed();
        result.error = "rknn_query IN_OUT_NUM failed or implausible";
        return result;
    }

    std::string err;
    for (uint32_t i = 0; i < io.n_input; ++i) {
        railhmi::core::TensorDesc t;
        if (!query_tensor(rt, ctx, kQueryInputAttr, i, &t, &err)) {
            rt.release(ctx);
            result.descriptor.load_status = railhmi::core::vision_reason::kModelLoadFailed();
            result.error = err;
            return result;
        }
        t.dir = railhmi::core::TensorDir::Input;
        result.descriptor.inputs.push_back(t);
    }
    for (uint32_t i = 0; i < io.n_output; ++i) {
        railhmi::core::TensorDesc t;
        if (!query_tensor(rt, ctx, kQueryOutputAttr, i, &t, &err)) {
            rt.release(ctx);
            result.descriptor.load_status = railhmi::core::vision_reason::kModelLoadFailed();
            result.error = err;
            return result;
        }
        t.dir = railhmi::core::TensorDir::Output;
        result.descriptor.outputs.push_back(t);
    }

    rt.release(ctx);
    result.descriptor.load_status = "ok";
    result.ok = true;
    return result;
}

}  // namespace media
}  // namespace railhmi
