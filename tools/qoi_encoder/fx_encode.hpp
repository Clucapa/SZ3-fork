// FX encoder -- SymEngine parse → differentiate → AST→TinyExpr strings → verify → base64.
// TinyExpr is compiled as C (tinyexpr.o), linked with the encoder only.

#ifndef QOI_ENCODER_FX_ENCODE_HPP
#define QOI_ENCODER_FX_ENCODE_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#include <symengine/basic.h>
#include <symengine/symbol.h>
#include <symengine/parser.h>

extern "C" {
#include "tinyexpr.h"
}

#include "encode.hpp"

namespace qoi_encode {

// Convert SymEngine __str__() to TinyExpr-compatible string.
// SymEngine uses ** for power; TinyExpr uses ^.
static std::string sym_to_te(const std::string &s) {
    std::string r = s;
    for (size_t p = 0; (p = r.find("**", p)) != std::string::npos; )
        r.replace(p, 2, "^");
    return r;
}

// Verify a TinyExpr string compiles (no eval needed).
static bool verify_te_string(const std::string &s) {
    double dummy = 0;
    te_variable vars[] = {{"x", &dummy, TE_VARIABLE, nullptr}};
    int err = 0;
    te_expr *n = te_compile(s.c_str(), vars, 1, &err);
    if (!n) return false;
    te_free(n);
    return true;
}

// Full FX encode: parse → diff → te_strings → verify → base64.
inline EncodeResult fx_encode(const std::string &expr_str) {
    EncodeResult r;
    r.ok = false;

    auto x = SymEngine::symbol("x");
    SymEngine::RCP<const SymEngine::Basic> f_expr, df_expr, ddf_expr;

    try {
        f_expr  = SymEngine::parse(expr_str);
        df_expr  = f_expr->diff(x);
        ddf_expr = df_expr->diff(x);
    } catch (const std::exception &e) {
        r.error = std::string("SymEngine error: ") + e.what();
        return r;
    }

    std::string f_str   = sym_to_te(f_expr->__str__());
    std::string df_str  = sym_to_te(df_expr->__str__());
    std::string ddf_str = sym_to_te(ddf_expr->__str__());

    if (!verify_te_string(f_str))   { r.error = "TinyExpr rejected f(x): " + f_str; return r; }
    if (!verify_te_string(df_str))  { r.error = "TinyExpr rejected f'(x): " + df_str; return r; }
    if (!verify_te_string(ddf_str)) { r.error = "TinyExpr rejected f''(x): " + ddf_str; return r; }

    // Serialize: three strings with uint32 lengths, then base64.
    std::vector<unsigned char> buf;
    auto write_u32 = [&](uint32_t v) {
        buf.push_back(v & 0xFF); buf.push_back((v>>8)&0xFF);
        buf.push_back((v>>16)&0xFF); buf.push_back((v>>24)&0xFF);
    };
    auto write_str = [&](const std::string &s) {
        write_u32(static_cast<uint32_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    };
    write_str(f_str);
    write_str(df_str);
    write_str(ddf_str);

    // Pad to avoid QoIIf.hpp base64 decoder partial-group bug.
    while (buf.size() % 3 != 0)
        buf.push_back(0);

    r.qoi = 0x70000000;
    r.qoiParams = qoi_encode::base64_encode(buf.data(), buf.size());
    r.ok = true;
    return r;
}

}  // namespace qoi_encode

#endif
