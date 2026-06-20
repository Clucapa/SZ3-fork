// qoi_encoder core logic -- reusable by both CLI and test harness.
//
// Usage:
//   #include "encode.hpp"
//   auto r = qoi_encode::encode("sqr+abs+cubic");
//   if (r.ok) { printf("qoi=0x%X params=%s\n", r.qoi, r.qoiParams.c_str()); }
#ifndef QOI_ENCODER_ENCODE_HPP
#define QOI_ENCODER_ENCODE_HPP

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace qoi_encode {

struct EncodeResult {
    int qoi = 0;
    std::string qoiParams;
    bool ok = false;
    std::string error;
};

// ============================================================================
//  Function registry
// ============================================================================

struct FuncEntry {
    const char *name;
    int nibble;
    int param_count;
};

inline const FuncEntry *funcs() {
    static const FuncEntry list[] = {
        {"lin",   0x0, 2}, {"sqr",   0x1, 0}, {"cubic", 0x2, 0},
        {"sqrt",  0x3, 0}, {"exp",   0x4, 1}, {"xlogx", 0x5, 0},
        {"log",   0x6, 1}, {"recip", 0x7, 0}, {"abs",   0x8, 0},
        {"sin",   0x9, 0}, {"tanh",  0xA, 0}, {"pow",   0xB, 1},
    };
    return list;
}
inline int kFuncCount() { return 12; }

inline int lookup_nibble(const std::string &name) {
    auto f = funcs();
    for (int i = 0; i < kFuncCount(); ++i)
        if (name == f[i].name) return f[i].nibble;
    return -1;
}

inline int param_count(int nibble) {
    auto f = funcs();
    for (int i = 0; i < kFuncCount(); ++i)
        if (f[i].nibble == nibble) return f[i].param_count;
    return 0;
}

// ============================================================================
//  AST
// ============================================================================

struct AstNode {
    enum Kind { LEAF, SUM, COMPOSE, MULTI };
    Kind kind = LEAF;
    int nibble = 0;
    std::vector<double> params;
    std::vector<AstNode> children;
};

// ============================================================================
//  Lexer
// ============================================================================

enum TokenKind { TOK_EOF = 0, TOK_IDENT, TOK_NUMBER, TOK_PLUS, TOK_BAR, TOK_AT,
                 TOK_LPAREN, TOK_RPAREN, TOK_COMMA };

struct Token { TokenKind kind; std::string text; };

class Lexer {
public:
    Lexer(std::string src) : src_(std::move(src)), pos_(0) {}

    Token next(EncodeResult &err) {
        skip_space();
        if (pos_ >= src_.size()) return {TOK_EOF, ""};
        char c = src_[pos_];
        if (c == '+') { pos_++; return {TOK_PLUS, "+"}; }
        if (c == '|') { pos_++; return {TOK_BAR,  "|"}; }
        if (c == '@') { pos_++; return {TOK_AT,   "@"}; }
        if (c == '(') { pos_++; return {TOK_LPAREN, "("}; }
        if (c == ')') { pos_++; return {TOK_RPAREN, ")"}; }
        if (c == ',') { pos_++; return {TOK_COMMA, ","}; }
        if (std::isalpha(c)) return lex_ident();
        if (std::isdigit(c) || c == '-' || c == '.') return lex_number();
        err.ok = false;
        err.error = std::string("unexpected character '") + c + "'";
        return {TOK_EOF, ""};
    }

private:
    void skip_space() { while (pos_ < src_.size() && std::isspace(src_[pos_])) pos_++; }
    Token lex_ident() {
        size_t s = pos_;
        while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_] == '_')) pos_++;
        return {TOK_IDENT, src_.substr(s, pos_ - s)};
    }
    Token lex_number() {
        size_t s = pos_; bool has_dot = false;
        if (pos_ < src_.size() && src_[pos_] == '-') pos_++;
        while (pos_ < src_.size() && (std::isdigit(src_[pos_]) || src_[pos_] == '.')) {
            if (src_[pos_] == '.') { if (has_dot) break; has_dot = true; }
            pos_++;
        }
        return {TOK_NUMBER, src_.substr(s, pos_ - s)};
    }
    const std::string src_;
    size_t pos_;
};

// ============================================================================
//  Parser
// ============================================================================

class Parser {
public:
    Parser(std::string src) : lex_(std::move(src)) { advance(); }

    AstNode parse_expr() {
        AstNode n; n.kind = AstNode::MULTI;
        n.children.push_back(parse_group());
        while (cur_.kind == TOK_BAR) { advance(); n.children.push_back(parse_group()); }
        if (n.children.size() == 1) return n.children[0];
        return n;
    }

    EncodeResult &error() { return err_; }

private:
    AstNode parse_group() {
        AstNode n; n.kind = AstNode::SUM;
        n.children.push_back(parse_compose_term());
        while (cur_.kind == TOK_PLUS) { advance(); n.children.push_back(parse_compose_term()); }
        if (n.children.size() == 1) return n.children[0];
        return n;
    }

    AstNode parse_compose_term() {
        AstNode left = parse_factor_term();
        while (cur_.kind == TOK_AT) {
            advance(); AstNode right = parse_factor_term();
            AstNode comp; comp.kind = AstNode::COMPOSE;
            comp.children.push_back(left); comp.children.push_back(right);
            left = comp;
        }
        return left;
    }

    AstNode parse_factor_term() {
        if (cur_.kind != TOK_IDENT) {
            err_.ok = false;
            err_.error = "expected function name, got '" + cur_.text + "'";
            return {};
        }
        int nib = lookup_nibble(cur_.text);
        if (nib < 0) { err_.ok = false; err_.error = "unknown function '" + cur_.text + "'"; return {}; }
        advance();
        AstNode n; n.kind = AstNode::LEAF; n.nibble = nib;
        if (cur_.kind == TOK_LPAREN) {
            advance();
            while (cur_.kind != TOK_RPAREN) {
                if (cur_.kind != TOK_NUMBER) {
                    err_.ok = false; err_.error = "expected number in params"; return n;
                }
                n.params.push_back(std::stod(cur_.text));
                advance();
                if (cur_.kind == TOK_COMMA) advance();
            }
            advance();
        }
        return n;
    }

    void advance() { cur_ = lex_.next(err_); if (!err_.ok) cur_.kind = TOK_EOF; }

    Lexer lex_;
    Token cur_;
    EncodeResult err_{0, "", true, ""};
};

// ============================================================================
//  Emitter
// ============================================================================

inline void emit(const AstNode &node, std::vector<int> &nibbles,
                  std::vector<double> &params) {
    switch (node.kind) {
        case AstNode::LEAF:
            nibbles.push_back(node.nibble);
            for (double v : node.params) params.push_back(v);
            for (int i = static_cast<int>(node.params.size()); i < param_count(node.nibble); ++i)
                params.push_back(std::nan(""));
            break;
        case AstNode::SUM:
            for (const auto &c : node.children) emit(c, nibbles, params); break;
        case AstNode::COMPOSE:
            nibbles.push_back(0xE);
            emit(node.children[0], nibbles, params);
            emit(node.children[1], nibbles, params);
            break;
        case AstNode::MULTI:
            for (size_t i = 0; i < node.children.size(); ++i) {
                emit(node.children[i], nibbles, params);
                if (i + 1 < node.children.size()) nibbles.push_back(0xF);
            }
            break;
    }
}

// ============================================================================
//  Post-processing
// ============================================================================

inline void reorder_sumqoi_lin(std::vector<int> &nibs) {
    for (int n : nibs) if (n == 0xF || n == 0xE) return;
    if (!nibs.empty() && nibs.back() == 0x0) {
        nibs.pop_back();
        nibs.insert(nibs.begin(), 0x0);
    }
}

inline void check_compose_lin_inner(const AstNode &node, bool in_compose,
                                     EncodeResult &err) {
    if (node.kind == AstNode::COMPOSE) {
        check_compose_lin_inner(node.children[0], true, err);
        check_compose_lin_inner(node.children[1], true, err);
        return;
    }
    if (node.kind == AstNode::LEAF && in_compose && node.nibble == 0x0) {
        double A = (node.params.size() > 0 && !std::isnan(node.params[0]))
                       ? node.params[0] : 1.0;
        double B = (node.params.size() > 1 && !std::isnan(node.params[1]))
                       ? node.params[1] : 0.0;
        if (A != 1.0 || B != 0.0) {
            err.ok = false;
            char buf[256];
            snprintf(buf, sizeof(buf),
                "Compose with non-identity lin(%.4g,%.4g) is not supported", A, B);
            err.error = buf;
        }
    }
    for (const auto &c : node.children) check_compose_lin_inner(c, in_compose, err);
}

// ============================================================================
//  Base64 encode
// ============================================================================

static const char kBase64Tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode(const void *data, size_t len) {
    const auto *p = static_cast<const unsigned char *>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned b0 = p[i], b1 = (i+1 < len) ? p[i+1] : 0, b2 = (i+2 < len) ? p[i+2] : 0;
        out += kBase64Tbl[b0 >> 2];
        out += kBase64Tbl[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i+1 < len) ? kBase64Tbl[((b1 & 0xF) << 2) | (b2 >> 6)] : '=';
        out += (i+2 < len) ? kBase64Tbl[b2 & 0x3F] : '=';
    }
    return out;
}

inline int nibbles_to_qoi(const std::vector<int> &nibs) {
    int v = 0;
    for (size_t i = 0; i < nibs.size(); ++i)
        v |= (nibs[i] & 0xF) << (static_cast<int>(i) * 4);
    return v;
}

// ============================================================================
//  Top-level encode function
// ============================================================================

inline EncodeResult encode(const std::string &expr) {
    EncodeResult r;
    r.ok = true;

    Parser parser(expr);
    AstNode ast = parser.parse_expr();
    if (!parser.error().ok) return parser.error();

    check_compose_lin_inner(ast, false, r);
    if (!r.ok) return r;

    std::vector<int> nibbles;
    std::vector<double> params;
    emit(ast, nibbles, params);
    reorder_sumqoi_lin(nibbles);

    r.qoi = nibbles_to_qoi(nibbles);
    if (!params.empty())
        r.qoiParams = base64_encode(params.data(), params.size() * sizeof(double));
    return r;
}

static size_t find_nibble_expr_end(const std::string &s, size_t start) {
    int depth = 0;
    for (size_t i = start; i < s.size(); i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (depth == 0 && (s[i] == ',' || s[i] == ';')) return i;
    }
    return s.size();
}

static std::string trim_str(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

inline EncodeResult iso6_encode(const std::string &expr) {
    size_t ps = expr.find('(');
    size_t pe = expr.rfind(')');
    if (ps == std::string::npos || pe == std::string::npos || pe <= ps)
        return {0, "", false, "malformed iso6 expression"};
    std::string inner = expr.substr(ps + 1, pe - ps - 1);

    std::vector<std::string> raw_groups;
    size_t pos = 0;
    while (pos < inner.size()) {
        int depth = 0;
        size_t start = pos;
        while (pos < inner.size()) {
            if (inner[pos] == '(') depth++;
            else if (inner[pos] == ')') depth--;
            else if (depth == 0 && inner[pos] == ';') break;
            pos++;
        }
        raw_groups.push_back(trim_str(inner.substr(start, pos - start)));
        if (pos < inner.size() && inner[pos] == ';') pos++;
    }
    if (raw_groups.empty())
        return {0, "", false, "no groups in iso6 expression"};

    std::vector<int> all_nibbles;
    std::vector<double> all_params;
    std::vector<double> iso_configs;

    for (size_t gi = 0; gi < raw_groups.size(); gi++) {
        const std::string &rg = raw_groups[gi];
        if (rg.empty()) return {0, "", false, "empty group in iso6 expression"};

        size_t comma = find_nibble_expr_end(rg, 0);
        if (comma >= rg.size())
            return {0, "", false, "expected ',' after nibble expr in iso6 group"};

        std::string nibble_part = trim_str(rg.substr(0, comma));
        if (nibble_part.empty())
            return {0, "", false, "empty nibble expression in iso6 group"};

        size_t ppos = comma + 1;
        auto read_double = [&]() -> double {
            while (ppos < rg.size() && std::isspace(static_cast<unsigned char>(rg[ppos]))) ppos++;
            size_t s = ppos;
            while (ppos < rg.size() && rg[ppos] != ',' && rg[ppos] != ';') ppos++;
            std::string token = rg.substr(s, ppos - s);
            if (token.empty()) return std::numeric_limits<double>::quiet_NaN();
            return std::stod(token);
        };
        double min_v = read_double();
        if (ppos >= rg.size() || rg[ppos] != ',')
            return {0, "", false, "expected ',' after min"};
        ppos++;
        double max_v = read_double();
        if (ppos >= rg.size() || rg[ppos] != ',')
            return {0, "", false, "expected ',' after max"};
        ppos++;
        double cnt_d = read_double();
        if (ppos >= rg.size() || rg[ppos] != ',')
            return {0, "", false, "expected ',' after count"};
        ppos++;
        double meb = read_double();

        iso_configs.push_back(min_v);
        iso_configs.push_back(max_v);
        iso_configs.push_back(cnt_d);
        iso_configs.push_back(meb);

        if (gi > 0) all_nibbles.push_back(0xF);

        Parser parser(nibble_part);
        AstNode ast = parser.parse_expr();
        if (!parser.error().ok) return parser.error();
        EncodeResult tmp; tmp.ok = true;
        check_compose_lin_inner(ast, false, tmp);
        if (!tmp.ok) return tmp;

        emit(ast, all_nibbles, all_params);
    }

    all_params.insert(all_params.end(), iso_configs.begin(), iso_configs.end());
    reorder_sumqoi_lin(all_nibbles);

    int nibble_qoi = nibbles_to_qoi(all_nibbles);
    int final_qoi = 0x60000000 | nibble_qoi;

    std::string qp;
    if (!all_params.empty())
        qp = base64_encode(all_params.data(), all_params.size() * sizeof(double));

    return {final_qoi, qp, true, ""};
}

// Wrap a pointwise EncodeResult as Regional (Block or Interp, nibble or FX).
inline EncodeResult make_regional(const EncodeResult &er, bool is_interp, bool is_fx) {
    int raw;
    if (is_fx) {
        raw = 0x30000000;
        if (is_interp) raw = 0x70000000;
    } else {
        raw = er.qoi & 0x0FFFFFFF;
        if (is_interp) raw |= 0x40000000;
    }
    return {~raw, er.qoiParams, er.ok, er.error};
}

}  // namespace qoi_encode

#endif
