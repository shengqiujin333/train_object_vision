#pragma once

// railhmi-core: 极简 YAML 子集解析器（无第三方依赖）。
// 仅支持本项目配置所需的 YAML 子集：块映射、块序列、内联流序列 [a, b]、
// 标量（字符串/整数/浮点/布尔/null）、注释、引号字符串。
// 不支持锚点/别名、多文档、复杂折叠标量。遇到不支持的结构返回明确错误。

#include <string>
#include <utility>
#include <vector>

namespace railhmi {
namespace core {
namespace yaml {

struct Node {
    enum class Type { Null, Scalar, Map, Seq };

    Type type = Type::Null;
    std::string scalar;                    // Scalar 原文（去引号后）
    bool quoted = false;                   // 原值是否带引号（用于区分 "123" 与 123）
    std::vector<std::pair<std::string, Node>> map;
    std::vector<Node> seq;

    bool is_null() const { return type == Type::Null; }
    bool is_scalar() const { return type == Type::Scalar; }
    bool is_map() const { return type == Type::Map; }
    bool is_seq() const { return type == Type::Seq; }

    const Node* get(const std::string& key) const;
    const Node* at(std::size_t index) const;
    std::size_t size() const;
};

// 解析文本为根节点。成功返回 true 且 *out 为根；失败返回 false 并填写 *error（含行号）。
bool parse(const std::string& text, Node* out, std::string* error);

// 标量转换辅助（不做默认值：转换失败返回 false，由调用方决定报错）。
bool to_bool(const Node& n, bool* out);
bool to_int(const Node& n, long long* out);
bool to_double(const Node& n, double* out);

}  // namespace yaml
}  // namespace core
}  // namespace railhmi
