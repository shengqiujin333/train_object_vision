// railhmi-media: 最小 RKNN 推理会话实现（视觉推理 ITEM-009）。
//
// ABI 说明：本文件自声明 rknn_input / rknn_output 前缀结构（与 rknpu2 2.x 头一致）；
// 若构建环境有官方头 rknn_api.h，则用 static_assert 校验尺寸/字段偏移，防止库升级后静默破坏内存。

#include "railhmi/media/rknn_session.h"

#include <dlfcn.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <vector>

#if defined(__has_include)
#if __has_include(<rknn_api.h>)
#include <rknn_api.h>
#define RAILHMI_HAVE_RKNN_HEADER 1
#endif
#endif

namespace railhmi {
namespace media {

namespace {

using rknn_context = uint64_t;

struct rknn_input_abi {
    uint32_t index;
    void* buf;
    uint32_t size;
    uint8_t pass_through;
    uint32_t type;  // rknn_tensor_type
    uint32_t fmt;   // rknn_tensor_format
};

struct rknn_output_abi {
    uint8_t want_float;
    uint8_t is_prealloc;
    uint32_t index;
    void* buf;
    uint32_t size;
};

#ifdef RAILHMI_HAVE_RKNN_HEADER
static_assert(sizeof(rknn_input_abi) == sizeof(::rknn_input), "rknn_input ABI size");
static_assert(offsetof(rknn_input_abi, buf) == offsetof(::rknn_input, buf), "rknn_input.buf");
static_assert(offsetof(rknn_input_abi, size) == offsetof(::rknn_input, size), "rknn_input.size");
static_assert(offsetof(rknn_input_abi, pass_through) == offsetof(::rknn_input, pass_through),
              "rknn_input.pass_through");
static_assert(offsetof(rknn_input_abi, type) == offsetof(::rknn_input, type), "rknn_input.type");
static_assert(offsetof(rknn_input_abi, fmt) == offsetof(::rknn_input, fmt), "rknn_input.fmt");
static_assert(sizeof(rknn_output_abi) == sizeof(::rknn_output), "rknn_output ABI size");
static_assert(offsetof(rknn_output_abi, index) == offsetof(::rknn_output, index),
              "rknn_output.index");
static_assert(offsetof(rknn_output_abi, buf) == offsetof(::rknn_output, buf), "rknn_output.buf");
static_assert(offsetof(rknn_output_abi, size) == offsetof(::rknn_output, size), "rknn_output.size");
#endif

// 与 rknpu2 2.x 一致的查询命令/类型枚举数值。
constexpr uint32_t kQueryInOutNum = 0;
constexpr uint32_t kQueryInputAttr = 1;
constexpr uint32_t kQueryOutputAttr = 2;
constexpr uint32_t kQuerySdkVersion = 5;
constexpr int kRknnSucc = 0;
constexpr uint32_t kTensorFormatNhwc = 1;
constexpr uint32_t kTypeFloat32 = 0;
constexpr uint32_t kTypeInt8 = 2;
constexpr uint32_t kTypeUint8 = 3;

// 张量属性前缀（字段顺序与 rknpu2 2.x 一致；额外预留缓冲）。
struct rknn_tensor_attr_min {
    uint32_t index;
    uint32_t n_dims;
    uint32_t dims[16];
    char name[256];
    uint32_t n_elems;
    uint32_t size;
    uint32_t fmt;
    uint32_t type;
    uint32_t qnt_type;
    int8_t fl;
    int32_t zp;
    float scale;
    uint32_t w_stride;
    uint32_t size_with_stride;
    uint8_t pass_through;
    uint32_t h_stride;
};
constexpr size_t kAttrHeadroom = 1024;

using fn_init = int (*)(rknn_context*, void*, uint32_t, uint32_t, void*);
using fn_query = int (*)(rknn_context, uint32_t, void*, uint32_t);
using fn_destroy = int (*)(rknn_context);
using fn_inputs_set = int (*)(rknn_context, uint32_t, rknn_input_abi*);
using fn_run = int (*)(rknn_context, void*);
using fn_outputs_get = int (*)(rknn_context, uint32_t, rknn_output_abi*, void*);
using fn_outputs_release = int (*)(rknn_context, uint32_t, rknn_output_abi*);

struct Runtime {
    void* handle = nullptr;
    fn_init init = nullptr;
    fn_query query = nullptr;
    fn_destroy destroy = nullptr;
    fn_inputs_set inputs_set = nullptr;
    fn_run run = nullptr;
    fn_outputs_get outputs_get = nullptr;
    fn_outputs_release outputs_release = nullptr;
    std::string error;
    bool ok() const {
        return handle && init && query && destroy && inputs_set && run && outputs_get &&
               outputs_release;
    }
};

Runtime& runtime() {
    static Runtime rt;
    static bool tried = false;
    if (tried) return rt;
    tried = true;
    rt.handle = ::dlopen("librknnrt.so", RTLD_LAZY | RTLD_LOCAL);
    if (!rt.handle) rt.handle = ::dlopen("librknnrt.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!rt.handle) {
        const char* e = ::dlerror();
        rt.error = "librknnrt.so not loadable: ";
        rt.error += e ? e : "unknown";
        return rt;
    }
    rt.init = reinterpret_cast<fn_init>(::dlsym(rt.handle, "rknn_init"));
    rt.query = reinterpret_cast<fn_query>(::dlsym(rt.handle, "rknn_query"));
    rt.destroy = reinterpret_cast<fn_destroy>(::dlsym(rt.handle, "rknn_destroy"));
    if (!rt.destroy) rt.destroy = reinterpret_cast<fn_destroy>(::dlsym(rt.handle, "rknn_release"));
    rt.inputs_set = reinterpret_cast<fn_inputs_set>(::dlsym(rt.handle, "rknn_inputs_set"));
    rt.run = reinterpret_cast<fn_run>(::dlsym(rt.handle, "rknn_run"));
    rt.outputs_get = reinterpret_cast<fn_outputs_get>(::dlsym(rt.handle, "rknn_outputs_get"));
    rt.outputs_release =
        reinterpret_cast<fn_outputs_release>(::dlsym(rt.handle, "rknn_outputs_release"));
    if (!rt.ok()) {
        rt.error =
            "librknnrt.so missing rknn_init/query/destroy/inputs_set/run/outputs_get/outputs_release";
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

bool read_model_file(const std::string& path, std::vector<char>* out) {
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
        *err = "rknn_query attr failed rc=" + std::to_string(rc) + " index=" +
               std::to_string(index);
        return false;
    }
    if (attr->index != index || attr->n_dims == 0 || attr->n_dims > 16) {
        *err = "rknn_tensor_attr implausible (index/dims)";
        return false;
    }
    railhmi::core::TensorDesc t;
    t.index = index;
    t.name = std::string(attr->name, ::strnlen(attr->name, sizeof(attr->name)));
    t.n_dims = attr->n_dims;
    for (uint32_t i = 0; i < attr->n_dims; ++i) {
        if (i < 8) t.dims.push_back(attr->dims[i]);
        else t.extra_dims += 1;
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

struct rknn_sdk_version_min {
    char api_version[256];
    char drv_version[256];
};

}  // namespace

struct RknnSession::Impl {
    rknn_context ctx = 0;
    bool open = false;
    std::vector<std::vector<uint8_t>> out_bufs;
    std::vector<rknn_output_abi> out_abi;
};

RknnSession::RknnSession() : impl_(new Impl()) {}

RknnSession::~RknnSession() { close(); }

bool RknnSession::valid() const { return impl_ && impl_->open; }

void RknnSession::close() {
    if (!impl_) return;
    if (impl_->open) {
        Runtime& rt = runtime();
        release_outputs();
        if (rt.ok()) rt.destroy(impl_->ctx);
        impl_->open = false;
        impl_->ctx = 0;
    }
    inputs_.clear();
    outputs_.clear();
}

bool RknnSession::open(const std::string& path, railhmi::core::ModelDescriptor* descriptor,
                       std::string* err) {
    error_.clear();
    auto fail = [&](const std::string& why) {
        error_ = why;
        if (err) *err = why;
        return false;
    };
    if (descriptor) {
        descriptor->path = path;
        const std::size_t slash = path.find_last_of("/\\");
        descriptor->file_name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        descriptor->file_present = false;
        descriptor->load_status = "ok";
    }
    Runtime& rt = runtime();
    if (!rt.ok()) {
        if (descriptor) descriptor->load_status = "RKNN_RUNTIME_UNAVAILABLE";
        return fail(rt.error.empty() ? "librknnrt unavailable" : rt.error);
    }
    std::vector<char> model;
    if (!read_model_file(path, &model)) {
        if (descriptor) descriptor->load_status = "MODEL_MISSING";
        return fail("model file not readable");
    }
    if (descriptor) descriptor->file_present = true;

    rknn_context ctx = 0;
    const int rc = rt.init(&ctx, model.data(), static_cast<uint32_t>(model.size()), 0, nullptr);
    if (rc != kRknnSucc) {
        if (descriptor) descriptor->load_status = "MODEL_LOAD_FAILED";
        return fail("rknn_init failed rc=" + std::to_string(rc));
    }
    impl_->ctx = ctx;
    impl_->open = true;

    rknn_sdk_version_min ver;
    std::memset(&ver, 0, sizeof(ver));
    if (rt.query(ctx, kQuerySdkVersion, &ver, sizeof(ver)) == kRknnSucc) {
        api_version_ = std::string(ver.api_version, ::strnlen(ver.api_version, sizeof(ver.api_version)));
        drv_version_ = std::string(ver.drv_version, ::strnlen(ver.drv_version, sizeof(ver.drv_version)));
    } else {
        api_version_ = "unknown(api query failed)";
        drv_version_ = "unknown(drv query failed)";
    }

    struct io_num {
        uint32_t n_input;
        uint32_t n_output;
    } io;
    std::memset(&io, 0, sizeof(io));
    if (rt.query(ctx, kQueryInOutNum, &io, sizeof(io)) != kRknnSucc || io.n_input == 0 ||
        io.n_output == 0 || io.n_input > 64 || io.n_output > 256) {
        close();
        if (descriptor) descriptor->load_status = "MODEL_LOAD_FAILED";
        return fail("rknn_query IN_OUT_NUM failed or implausible");
    }

    inputs_.clear();
    outputs_.clear();
    std::string qerr;
    for (uint32_t i = 0; i < io.n_input; ++i) {
        railhmi::core::TensorDesc t;
        if (!query_tensor(rt, ctx, kQueryInputAttr, i, &t, &qerr)) {
            close();
            if (descriptor) descriptor->load_status = "MODEL_LOAD_FAILED";
            return fail(qerr);
        }
        t.dir = railhmi::core::TensorDir::Input;
        inputs_.push_back(t);
    }
    for (uint32_t i = 0; i < io.n_output; ++i) {
        railhmi::core::TensorDesc t;
        if (!query_tensor(rt, ctx, kQueryOutputAttr, i, &t, &qerr)) {
            close();
            if (descriptor) descriptor->load_status = "MODEL_LOAD_FAILED";
            return fail(qerr);
        }
        t.dir = railhmi::core::TensorDir::Output;
        outputs_.push_back(t);
    }

    if (descriptor) {
        descriptor->runtime_api_version = api_version_;
        descriptor->runtime_drv_version = drv_version_;
        descriptor->inputs = inputs_;
        descriptor->outputs = outputs_;
        descriptor->load_status = "ok";
    }
    return true;
}

bool RknnSession::set_input(const void* data, std::size_t size, uint32_t type, std::string* err) {
    auto fail = [&](const std::string& why) {
        error_ = why;
        if (err) *err = why;
        return false;
    };
    Runtime& rt = runtime();
    if (!impl_->open || !rt.ok()) return fail("session not open");
    if (inputs_.empty()) return fail("no input tensor");
    if (!data || size == 0) return fail("null/empty input buffer");
    const railhmi::core::TensorDesc& in = inputs_[0];
    if (size < in.size) {
        return fail("input size " + std::to_string(size) + " < tensor size " +
                    std::to_string(in.size));
    }
    rknn_input_abi ri;
    std::memset(&ri, 0, sizeof(ri));
    ri.index = 0;
    ri.buf = const_cast<void*>(data);
    ri.size = static_cast<uint32_t>(size);
    ri.pass_through = 0;
    ri.type = type;
    ri.fmt = in.fmt == railhmi::core::TensorFormat::Nhwc ? kTensorFormatNhwc : 0;
    const int rc = rt.inputs_set(impl_->ctx, 1, &ri);
    if (rc != kRknnSucc) return fail("rknn_inputs_set failed rc=" + std::to_string(rc));
    return true;
}

bool RknnSession::run(std::string* err) {
    auto fail = [&](const std::string& why) {
        error_ = why;
        if (err) *err = why;
        return false;
    };
    Runtime& rt = runtime();
    if (!impl_->open || !rt.ok()) return fail("session not open");
    const int rc = rt.run(impl_->ctx, nullptr);
    if (rc != kRknnSucc) return fail("rknn_run failed rc=" + std::to_string(rc));
    return true;
}

void RknnSession::release_outputs() {
    if (!impl_ || impl_->out_abi.empty()) return;
    Runtime& rt = runtime();
    if (rt.ok() && impl_->open)
        rt.outputs_release(impl_->ctx, static_cast<uint32_t>(impl_->out_abi.size()),
                           impl_->out_abi.data());
    impl_->out_abi.clear();
    impl_->out_bufs.clear();
}

bool RknnSession::get_outputs_float(std::vector<RknnOutputTensor>* outs, std::string* err) {
    auto fail = [&](const std::string& why) {
        error_ = why;
        if (err) *err = why;
        return false;
    };
    Runtime& rt = runtime();
    if (!impl_->open || !rt.ok()) return fail("session not open");
    if (outputs_.empty()) return fail("no output tensor");
    release_outputs();

    impl_->out_abi.assign(outputs_.size(), rknn_output_abi{});
    for (std::size_t i = 0; i < impl_->out_abi.size(); ++i) {
        impl_->out_abi[i].index = static_cast<uint32_t>(i);
        impl_->out_abi[i].want_float = 1;
        impl_->out_abi[i].is_prealloc = 0;
        impl_->out_abi[i].buf = nullptr;
        impl_->out_abi[i].size = 0;
    }
    const int rc = rt.outputs_get(impl_->ctx, static_cast<uint32_t>(impl_->out_abi.size()),
                                  impl_->out_abi.data(), nullptr);
    if (rc != kRknnSucc) return fail("rknn_outputs_get failed rc=" + std::to_string(rc));

    impl_->out_bufs.assign(outputs_.size(), std::vector<uint8_t>());
    std::vector<RknnOutputTensor> result;
    result.reserve(outputs_.size());
    for (std::size_t i = 0; i < outputs_.size(); ++i) {
        const rknn_output_abi& o = impl_->out_abi[i];
        const railhmi::core::TensorDesc& d = outputs_[i];
        if (!o.buf || o.size == 0) return fail("output " + std::to_string(i) + " empty");
        const std::size_t expect = static_cast<std::size_t>(d.n_elems) * sizeof(float);
        if (o.size != expect) {
            return fail("output " + std::to_string(i) + " size=" + std::to_string(o.size) +
                        " expected float32 bytes=" + std::to_string(expect));
        }
        impl_->out_bufs[i].assign(static_cast<const uint8_t*>(o.buf),
                                  static_cast<const uint8_t*>(o.buf) + o.size);
        RknnOutputTensor t;
        t.index = static_cast<uint32_t>(i);
        t.buf = impl_->out_bufs[i].data();
        t.size = impl_->out_bufs[i].size();
        t.desc = &outputs_[i];
        result.push_back(t);
    }
    if (outs) *outs = result;
    return true;
}

}  // namespace media
}  // namespace railhmi
