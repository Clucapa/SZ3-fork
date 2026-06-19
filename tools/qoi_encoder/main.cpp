// qoi_encoder — convert human-readable QOI expressions to nibble-encoded int + base64 params.
//
// Grammar:
//   expr     = group ('|' group)*
//   group    = compose_term ('+' compose_term)*
//   compose_term = factor_term ('@' factor_term)*
//   factor_term = IDENT [ '(' number (',' number)* ')' ]
//
// Operators:  +  SumQoI (group-internal),  |  MultiQoI (group separator),  @  Compose
//
// Output: two lines to stdout, e.g.:
//   qoi        = 0x14E
//   qoiParams  = "AAAAAAAACUA="

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
//  Function registry
// ============================================================================

struct FuncEntry {
    const char *name;
    int nibble;
    int param_count;  // number of double parameters consumed from param list
};

static const FuncEntry kFuncs[] = {
    {"lin",   0x0, 2},  // A, B
    {"sqr",   0x1, 0},
    {"cubic", 0x2, 0},
    {"sqrt",  0x3, 0},
    {"exp",   0x4, 1},  // base
    {"xlogx", 0x5, 0},
    {"log",   0x6, 1},  // base
    {"recip", 0x7, 0},
    {"abs",   0x8, 0},
    {"sin",   0x9, 0},
    {"tanh",  0xA, 0},
    {"pow",   0xB, 1},  // exponent
};
static const int kFuncCount = sizeof(kFuncs) / sizeof(kFuncs[0]);

static int lookup_nibble(const std::string &name) {
    for (int i = 0; i < kFuncCount; ++i)
        if (name == kFuncs[i].name) return kFuncs[i].nibble;
    return -1;
}

static int param_count(int nibble) {
    for (int i = 0; i < kFuncCount; ++i)
        if (kFuncs[i].nibble == nibble) return kFuncs[i].param_count;
    return 0;
}

// ============================================================================
//  AST node types
// ============================================================================

struct AstNode {
    enum Kind { LEAF, SUM, COMPOSE, MULTI };

    Kind kind;
    int nibble;                    // valid when kind == LEAF
    std::vector<double> params;    // valid when kind == LEAF
    std::vector<AstNode> children; // valid for SUM / COMPOSE / MULTI
};

// ============================================================================
//  Lexer
// ============================================================================

enum TokenKind { TOK_EOF = 0, TOK_IDENT, TOK_NUMBER, TOK_PLUS, TOK_BAR, TOK_AT,
                 TOK_LPAREN, TOK_RPAREN, TOK_COMMA };

struct Token {
    TokenKind kind;
    std::string text;
};

class Lexer {
public:
    Lexer(std::string src) : src_(std::move(src)), pos_(0) {}

    Token next() {
        skip_space();
        if (pos_ >= src_.size()) return {TOK_EOF, ""};

        char c = src_[pos_];
        if (c == '+')      { pos_++; return {TOK_PLUS, "+"}; }
        if (c == '|')      { pos_++; return {TOK_BAR,  "|"}; }
        if (c == '@')      { pos_++; return {TOK_AT,   "@"}; }
        if (c == '(')      { pos_++; return {TOK_LPAREN, "("}; }
        if (c == ')')      { pos_++; return {TOK_RPAREN, ")"}; }
        if (c == ',')      { pos_++; return {TOK_COMMA, ","}; }

        if (std::isalpha(c)) return lex_ident();
        if (std::isdigit(c) || c == '-' || c == '.') return lex_number();
        fprintf(stderr, "Error: unexpected character '%c'\n", c);
        exit(1);
    }

private:
    void skip_space() {
        while (pos_ < src_.size() && std::isspace(src_[pos_])) pos_++;
    }

    Token lex_ident() {
        size_t start = pos_;
        while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_] == '_'))
            pos_++;
        return {TOK_IDENT, src_.substr(start, pos_ - start)};
    }

    Token lex_number() {
        size_t start = pos_;
        bool has_dot = false;
        if (pos_ < src_.size() && src_[pos_] == '-') pos_++;
        while (pos_ < src_.size() && (std::isdigit(src_[pos_]) || src_[pos_] == '.')) {
            if (src_[pos_] == '.') {
                if (has_dot) break;
                has_dot = true;
            }
            pos_++;
        }
        return {TOK_NUMBER, src_.substr(start, pos_ - start)};
    }

    const std::string src_;
    size_t pos_;
};

// ============================================================================
//  Parser — recursive descent
// ============================================================================

class Parser {
public:
    Parser(std::string src) : lex_(std::move(src)) { advance(); }

    // expr = group ('|' group)*
    AstNode parse_expr() {
        AstNode n;
        n.kind = AstNode::MULTI;
        n.children.push_back(parse_group());
        while (cur_.kind == TOK_BAR) {
            advance();
            n.children.push_back(parse_group());
        }
        if (n.children.size() == 1) return n.children[0];  // collapse single group
        return n;
    }

private:
    // group = compose_term ('+' compose_term)*
    AstNode parse_group() {
        AstNode n;
        n.kind = AstNode::SUM;
        n.children.push_back(parse_compose_term());
        while (cur_.kind == TOK_PLUS) {
            advance();
            n.children.push_back(parse_compose_term());
        }
        if (n.children.size() == 1) return n.children[0];  // collapse single term
        return n;
    }

    // compose_term = factor_term ('@' factor_term)*  (right-associative)
    AstNode parse_compose_term() {
        AstNode left = parse_factor_term();
        while (cur_.kind == TOK_AT) {
            advance();
            AstNode right = parse_factor_term();
            AstNode comp;
            comp.kind = AstNode::COMPOSE;
            comp.children.push_back(left);
            comp.children.push_back(right);
            left = comp;
        }
        return left;
    }

    // factor_term = IDENT [ '(' number (',' number)* ')' ]
    AstNode parse_factor_term() {
        if (cur_.kind != TOK_IDENT) {
            fprintf(stderr, "Error: expected function name, got '%s'\n", cur_.text.c_str());
            exit(1);
        }
        std::string name = cur_.text;
        int nib = lookup_nibble(name);
        if (nib < 0) {
            fprintf(stderr, "Error: unknown function '%s'\n", name.c_str());
            exit(1);
        }
        advance();

        AstNode n;
        n.kind = AstNode::LEAF;
        n.nibble = nib;

        if (cur_.kind == TOK_LPAREN) {
            advance();
            while (cur_.kind != TOK_RPAREN) {
                if (cur_.kind != TOK_NUMBER) {
                    fprintf(stderr, "Error: expected number in params, got '%s'\n",
                            cur_.text.c_str());
                    exit(1);
                }
                n.params.push_back(std::stod(cur_.text));
                advance();
                if (cur_.kind == TOK_COMMA) advance();
            }
            advance();  // ')'
        }
        return n;
    }

    void advance() { cur_ = lex_.next(); }

    Lexer lex_;
    Token cur_;
};

// ============================================================================
//  Emitter — AST → nibble sequence + params vector
// ============================================================================

// Collect nibbles and params via left-to-right depth-first traversal.
// For COMPOSE: nibbles = [0xE, outer_nibs..., inner_nibs...]
// For SUM:     nibbles = [fn1, fn2, ...] (left to right)
// For MULTI:   nibbles = [...group1..., 0xF, ...group2...]
static void emit(const AstNode &node, std::vector<int> &nibbles,
                  std::vector<double> &params) {
    switch (node.kind) {
        case AstNode::LEAF:
            nibbles.push_back(node.nibble);
            for (double v : node.params)
                params.push_back(v);
            // Fill missing params with nan (signals "use default")
            for (int i = node.params.size(); i < param_count(node.nibble); ++i)
                params.push_back(std::nan(""));
            break;

        case AstNode::SUM:
            for (const auto &c : node.children)
                emit(c, nibbles, params);
            break;

        case AstNode::COMPOSE:
            nibbles.push_back(0xE);
            emit(node.children[0], nibbles, params);  // outer
            emit(node.children[1], nibbles, params);  // inner
            break;

        case AstNode::MULTI:
            for (size_t i = 0; i < node.children.size(); ++i) {
                emit(node.children[i], nibbles, params);
                if (i + 1 < node.children.size())
                    nibbles.push_back(0xF);
            }
            break;
    }
}

// ============================================================================
//  Post-processing rules
// ============================================================================

// Rule: SumQoI末尾0x0重排 — move trailing lin to front (commutative, safe).
static void reorder_sumqoi_lin(std::vector<int> &nibs) {
    // Only applies to a simple sum (no F, no E inside).
    bool has_special = false;
    for (int n : nibs) if (n == 0xF || n == 0xE) { has_special = true; break; }
    if (has_special || nibs.empty()) return;
    if (nibs.back() == 0x0) {
        nibs.pop_back();
        nibs.insert(nibs.begin(), 0x0);
    }
}

// Rule: Compose innermost lin with A≠1 or B≠0 → reject.
static void check_compose_lin(const AstNode &node, bool in_compose) {
    if (node.kind == AstNode::COMPOSE) {
        check_compose_lin(node.children[0], true);
        check_compose_lin(node.children[1], true);
        return;
    }
    if (node.kind == AstNode::LEAF && in_compose && node.nibble == 0x0) {
        double A = (node.params.size() > 0 && !std::isnan(node.params[0]))
                       ? node.params[0] : 1.0;
        double B = (node.params.size() > 1 && !std::isnan(node.params[1]))
                       ? node.params[1] : 0.0;
        if (A != 1.0 || B != 0.0) {
            fprintf(stderr,
                "Error: Compose with non-identity lin(%.4g,%.4g) is not supported.\n"
                "  Hint: lin(1,0)∘g is identity and is auto-elided at runtime;\n"
                "        lin(A,B)∘g with A≠1 or B≠0 should be expressed differently.\n",
                A, B);
            exit(1);
        }
    }
    for (const auto &c : node.children)
        check_compose_lin(c, in_compose);
}

// ============================================================================
//  Base64 encode (standard, no padding)
// ============================================================================

static const char kBase64Tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const void *data, size_t len) {
    const auto *p = static_cast<const unsigned char *>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned b0 = p[i];
        unsigned b1 = (i + 1 < len) ? p[i + 1] : 0;
        unsigned b2 = (i + 2 < len) ? p[i + 2] : 0;
        out += kBase64Tbl[b0 >> 2];
        out += kBase64Tbl[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i + 1 < len) ? kBase64Tbl[((b1 & 0xF) << 2) | (b2 >> 6)] : '=';
        out += (i + 2 < len) ? kBase64Tbl[b2 & 0x3F] : '=';
    }
    return out;
}

// ============================================================================
//  qoi int encoding: nibbles[0] + nibbles[1]*16 + nibbles[2]*256 + ...
// ============================================================================

static int nibbles_to_qoi(const std::vector<int> &nibs) {
    int v = 0;
    for (size_t i = 0; i < nibs.size(); ++i)
        v |= (nibs[i] & 0xF) << (static_cast<int>(i) * 4);
    return v;
}

// ============================================================================
//  main
// ============================================================================

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s \"expression\"\n"
        "  expression examples:\n"
        "    sqr                           ->  qoi = 0x1\n"
        "    sqr+abs+cubic                 ->  qoi = 0x128\n"
        "    lin(2,0.5)+exp(10)+sqr        ->  qoi = 0x140\n"
        "    exp(2.718)@sqr                ->  qoi = 0x14E\n"
        "    sqr+abs|exp(3)+sin             ->  multi-group\n"
        "  Operators:  +  SumQoI    @  Compose    |  MultiQoI\n"
        "  Functions:  lin sqr cubic sqrt exp xlogx log recip abs sin tanh pow\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    // Parse
    Parser parser(argv[1]);
    AstNode ast = parser.parse_expr();

    // Validate
    check_compose_lin(ast, false);

    // Emit
    std::vector<int> nibbles;
    std::vector<double> params;
    emit(ast, nibbles, params);

    // Post-process
    reorder_sumqoi_lin(nibbles);

    // Encode
    int qoi = nibbles_to_qoi(nibbles);
    std::string param_b64;
    if (!params.empty()) {
        param_b64 = base64_encode(params.data(), params.size() * sizeof(double));
    }

    // Output
    printf("qoi        = 0x%X\n", qoi);
    printf("qoiParams  = \"%s\"\n", param_b64.c_str());

    return 0;
}
