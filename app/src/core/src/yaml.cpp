#include "railhmi/core/yaml.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace railhmi {
namespace core {
namespace yaml {

const Node* Node::get(const std::string& key) const {
    for (const auto& kv : map) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

const Node* Node::at(std::size_t index) const {
    if (index >= seq.size()) return nullptr;
    return &seq[index];
}

std::size_t Node::size() const {
    if (type == Type::Map) return map.size();
    if (type == Type::Seq) return seq.size();
    return 0;
}

namespace {

struct Line {
    int indent = 0;
    std::string text;
    int number = 0;
};

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

bool starts_with(const std::string& s, const char* p) {
    std::size_t n = 0;
    while (p[n]) ++n;
    return s.size() >= n && s.compare(0, n, p) == 0;
}

// 去掉行内注释（引号外的、前面是空白或行首的 '#'）
std::string strip_comment(const std::string& s) {
    bool in_s = false, in_d = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !in_d) in_s = !in_s;
        else if (c == '"' && !in_s) in_d = !in_d;
        else if (c == '#' && !in_s && !in_d && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            return s.substr(0, i);
        }
    }
    return s;
}

bool tokenize(const std::string& text, std::vector<Line>* out, std::string* err) {
    std::istringstream in(text);
    std::string raw;
    int lineno = 0;
    while (std::getline(in, raw)) {
        ++lineno;
        std::string line = strip_comment(raw);
        if (line.empty()) continue;
        // 跳过纯空白
        std::size_t k = 0;
        while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
        if (k == line.size()) continue;
        if (line[k] == '\t') {
            *err = "line " + std::to_string(lineno) + ": tab indentation is not supported";
            return false;
        }
        if (line[k] == '-' && k + 1 < line.size() && line[k + 1] != ' ') {
            // 可能是否定/负数标量，但作为块首不合法；交由后续处理
        }
        // 文档分隔符/指令不支持
        std::string t = trim(line);
        if (starts_with(t, "---") || starts_with(t, "...")) {
            *err = "line " + std::to_string(lineno) + ": multi-document not supported";
            return false;
        }
        Line ln;
        ln.indent = static_cast<int>(k);
        ln.text = trim(line);
        ln.number = lineno;
        out->push_back(ln);
    }
    return true;
}

std::string unquote(const std::string& s, bool* quoted) {
    *quoted = false;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        *quoted = true;
        std::string r;
        for (std::size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\' && i + 2 < s.size()) {
                char n = s[i + 1];
                if (n == 'n') r += '\n';
                else if (n == 't') r += '\t';
                else r += n;
                ++i;
            } else {
                r += s[i];
            }
        }
        return r;
    }
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        *quoted = true;
        std::string r;
        for (std::size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\'' && i + 2 < s.size() && s[i + 1] == '\'') { r += '\''; ++i; }
            else r += s[i];
        }
        return r;
    }
    return s;
}

// 判断形如 "key: value" 或 "key:"；成功返回 true 并填充 key/rest/hasRest。
bool looks_like_kv(const std::string& text, std::string* key, std::string* rest, bool* hasRest) {
    bool in_s = false, in_d = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\'' && !in_d) in_s = !in_s;
        else if (c == '"' && !in_s) in_d = !in_d;
        else if (c == ':' && !in_s && !in_d) {
            std::string after = text.substr(i + 1);
            if (after.empty()) {
                *key = trim(text.substr(0, i));
                rest->clear();
                *hasRest = false;
                return !key->empty();
            }
            if (after[0] == ' ') {
                *key = trim(text.substr(0, i));
                *rest = trim(after);
                *hasRest = true;
                return !key->empty();
            }
            return false;  // ':' 后无空格 → 视为标量（如 16:9、rtsp://...）
        }
    }
    return false;
}

std::vector<std::string> split_flow(const std::string& inner) {
    std::vector<std::string> parts;
    std::string cur;
    bool in_s = false, in_d = false;
    for (char c : inner) {
        if (c == '\'' && !in_d) in_s = !in_s;
        else if (c == '"' && !in_s) in_d = !in_d;
        if (c == ',' && !in_s && !in_d) { parts.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty()) parts.push_back(trim(cur));
    return parts;
}

bool parse_value(const std::string& rest, Node* out, std::string* err, int lineno) {
    if (!rest.empty() && rest.front() == '[' && rest.back() == ']') {
        out->type = Node::Type::Seq;
        for (const std::string& p : split_flow(rest.substr(1, rest.size() - 2))) {
            Node s;
            s.type = Node::Type::Scalar;
            s.scalar = unquote(p, &s.quoted);
            out->seq.push_back(s);
        }
        return true;
    }
    if (!rest.empty() && (rest.front() == '{')) {
        *err = "line " + std::to_string(lineno) + ": flow mapping not supported";
        return false;
    }
    out->type = Node::Type::Scalar;
    out->scalar = unquote(rest, &out->quoted);
    if (out->scalar == "~" || out->scalar == "null" || out->scalar == "Null") {
        out->type = Node::Type::Null;
        out->scalar.clear();
    }
    return true;
}

bool parse_block(const std::vector<Line>& L, std::size_t& i, int indent, Node* out, std::string* err);

bool parse_map(const std::vector<Line>& L, std::size_t& i, int indent, Node* out, std::string* err) {
    out->type = Node::Type::Map;
    while (i < L.size() && L[i].indent == indent) {
        const Line& ln = L[i];
        if (starts_with(ln.text, "- ")) {
            *err = "line " + std::to_string(ln.number) + ": unexpected sequence item inside mapping";
            return false;
        }
        std::string key, rest;
        bool hasRest = false;
        if (!looks_like_kv(ln.text, &key, &rest, &hasRest)) {
            *err = "line " + std::to_string(ln.number) + ": expected 'key: value'";
            return false;
        }
        if (i + 1 < L.size() && L[i + 1].indent > indent) {
            if (hasRest) {
                *err = "line " + std::to_string(ln.number) + ": key '" + key +
                       "' has both a value and a nested block";
                return false;
            }
            Node child;
            std::size_t j = i + 1;
            if (!parse_block(L, j, L[j].indent, &child, err)) return false;
            out->map.emplace_back(key, std::move(child));
            i = j;
        } else {
            if (!hasRest) {
                *err = "line " + std::to_string(ln.number) + ": key '" + key + "' has no value";
                return false;
            }
            Node child;
            if (!parse_value(rest, &child, err, ln.number)) return false;
            out->map.emplace_back(key, std::move(child));
            ++i;
        }
    }
    return true;
}

bool parse_seq(const std::vector<Line>& L, std::size_t& i, int indent, Node* out, std::string* err) {
    out->type = Node::Type::Seq;
    while (i < L.size() && L[i].indent == indent && starts_with(L[i].text, "- ")) {
        const Line& ln = L[i];
        std::string rest = trim(ln.text.substr(2));
        if (rest.empty()) {
            if (i + 1 < L.size() && L[i + 1].indent > indent) {
                Node child;
                std::size_t j = i + 1;
                if (!parse_block(L, j, L[j].indent, &child, err)) return false;
                out->seq.push_back(std::move(child));
                i = j;
            } else {
                Node n;  // null item
                out->seq.push_back(std::move(n));
                ++i;
            }
            continue;
        }
        std::string key, rv;
        bool hasRest = false;
        bool is_kv = (rest.front() != '[') && looks_like_kv(rest, &key, &rv, &hasRest);
        if (!is_kv) {
            Node n;
            if (!parse_value(rest, &n, err, ln.number)) return false;
            out->seq.push_back(std::move(n));
            ++i;
            continue;
        }
        Node m;
        m.type = Node::Type::Map;
        if (hasRest) {
            Node child;
            if (!parse_value(rv, &child, err, ln.number)) return false;
            m.map.emplace_back(key, std::move(child));
            ++i;
        } else if (i + 1 < L.size() && L[i + 1].indent > indent) {
            Node child;
            std::size_t j = i + 1;
            if (!parse_block(L, j, L[j].indent, &child, err)) return false;
            m.map.emplace_back(key, std::move(child));
            i = j;
        } else {
            *err = "line " + std::to_string(ln.number) + ": sequence map key '" + key + "' has no value";
            return false;
        }
        // 续行：同一序列项的其它键（缩进 > 序列缩进，且不是新的 "- " 项）
        if (i < L.size() && L[i].indent > indent && !starts_with(L[i].text, "- ")) {
            Node more;
            std::size_t j = i;
            int cindent = L[i].indent;
            if (!parse_map(L, j, cindent, &more, err)) return false;
            for (auto& kv : more.map) m.map.push_back(std::move(kv));
            i = j;
        }
        out->seq.push_back(std::move(m));
    }
    return true;
}

bool parse_block(const std::vector<Line>& L, std::size_t& i, int indent, Node* out, std::string* err) {
    if (i >= L.size() || L[i].indent < indent) {
        out->type = Node::Type::Null;
        return true;
    }
    if (starts_with(L[i].text, "- ")) return parse_seq(L, i, indent, out, err);
    return parse_map(L, i, indent, out, err);
}

}  // namespace

bool parse(const std::string& text, Node* out, std::string* error) {
    std::vector<Line> lines;
    std::string err;
    if (!tokenize(text, &lines, &err)) {
        if (error) *error = err;
        return false;
    }
    if (lines.empty()) {
        out->type = Node::Type::Null;
        return true;
    }
    std::size_t i = 0;
    Node root;
    if (!parse_block(lines, i, lines[0].indent, &root, &err)) {
        if (error) *error = err;
        return false;
    }
    if (i != lines.size()) {
        if (error) *error = "line " + std::to_string(lines[i].number) + ": unexpected trailing content";
        return false;
    }
    *out = std::move(root);
    return true;
}

bool to_bool(const Node& n, bool* out) {
    if (n.type != Node::Type::Scalar || n.quoted) return false;
    if (n.scalar == "true" || n.scalar == "True" || n.scalar == "TRUE") { *out = true; return true; }
    if (n.scalar == "false" || n.scalar == "False" || n.scalar == "FALSE") { *out = false; return true; }
    return false;
}

bool to_int(const Node& n, long long* out) {
    if (n.type != Node::Type::Scalar || n.quoted || n.scalar.empty()) return false;
    char* end = nullptr;
    long long v = std::strtoll(n.scalar.c_str(), &end, 10);
    if (end == n.scalar.c_str() || *end != '\0') return false;
    *out = v;
    return true;
}

bool to_double(const Node& n, double* out) {
    if (n.type != Node::Type::Scalar || n.quoted || n.scalar.empty()) return false;
    char* end = nullptr;
    double v = std::strtod(n.scalar.c_str(), &end);
    if (end == n.scalar.c_str() || *end != '\0') return false;
    *out = v;
    return true;
}

}  // namespace yaml
}  // namespace core
}  // namespace railhmi
