// Encoder roundtrip test cases — expressions that go beyond the default QoIDef.
// Default base functions are covered by QoiDef entries (which also use the encoder).
// This file covers: custom params, non-default composites, FX, and iso6.

#ifndef SZ3_TEST_ENCODER_TESTS_HPP
#define SZ3_TEST_ENCODER_TESTS_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

#include "SZ3/api/sz.hpp"
#include "test_config.hpp"
#include "verify.hpp"

namespace sz3_test {

struct EncoderTestCase {
    const char *label;
    const char *expr;
    double (*feval)(double);
    double (*feval2)(double);
    double qEB;
    double qEB2;
    double absErrorBound;
    double max_data;
    double min_abs;
    QoiDomain domain;
};

static double h_enc_lin_custom(double x) { return 2.0 * x + 0.5; }
static double h_enc_sqr(double x)        { return x * x; }
static double h_enc_cubic(double x)      { return x * x * x; }
static double h_enc_sqrt(double x)       { return std::sqrt(x); }
static double h_enc_exp(double x)        { return std::exp(x); }
static double h_enc_exp3(double x)       { return std::pow(3.0, x); }
static double h_enc_log2(double x)       { return std::log(x) / std::log(2.0); }
static double h_enc_abs(double x)        { return std::fabs(x); }
static double h_enc_sin(double x)        { return std::sin(x); }
static double h_enc_tanh(double x)       { return std::tanh(x); }
static double h_enc_pow35(double x)      { return std::pow(x, 3.5); }
static double h_enc_sum2(double x)       { return h_enc_sqr(x) + h_enc_cubic(x); }
static double h_enc_sum2b(double x)      { return h_enc_lin_custom(x) + h_enc_sqr(x); }
static double h_enc_sum3(double x)       { return h_enc_lin_custom(x) + h_enc_exp3(x) + h_enc_sqr(x); }
static double h_enc_sum3b(double x)      { return h_enc_abs(x) + h_enc_sin(x) + h_enc_tanh(x); }
static double h_enc_comp_exp_sqr(double x)  { return std::exp(x * x); }
static double h_enc_comp_abs_sin(double x)  { return std::fabs(std::sin(x)); }
static double h_enc_comp_sqrt_exp2_sqr(double x) { return std::sqrt(std::pow(2.0, x * x)); }
static double h_enc_comp_exp_cubic(double x)     { return std::exp(x * x * x); }
static double h_enc_multi_abs(double x)   { return h_enc_abs(x); }
static double h_enc_multi_sum(double x)   { return h_enc_exp3(x) + h_enc_sin(x); }
static double h_fx_sin_x2(double x)    { return std::sin(x) + x*x; }
static double h_fx_sqrt_exp(double x)  { return std::sqrt(x) + std::exp(-x); }
static double h_fx_x3_2x_1(double x)   { return x*x*x + 2.0*x + 1.0; }

inline const EncoderTestCase *all_encoder_tests(int &count) {
    static const EncoderTestCase list[] = {
        // Custom params (different from QoiDef defaults)
        {"LinCustom",    "lin(2,0.5)",     h_enc_lin_custom, nullptr, 3.0, 0, 10.0, 0,   DOM_UNRESTRICTED},
        {"ExpCustom",    "exp(3)",         h_enc_exp3,       nullptr, 1.0, 0, 10.0, 5.0, DOM_UNRESTRICTED},
        {"LogCustom",    "log(2)",         h_enc_log2,       nullptr, 0.1, 0, 5.0,  0,   DOM_POSITIVE},
        {"PowCustom",    "pow(3.5)",       h_enc_pow35,      nullptr, 1.0, 0, 10.0, 50.0,DOM_UNRESTRICTED},
        // SumQoI with params
        {"Sum3Param",    "lin(2,0.5)+exp(3)+sqr", h_enc_sum3,        nullptr, 1.0, 0, 10.0, 5.0, DOM_UNRESTRICTED},
        {"Sum3NoParam",  "abs+sin+tanh",   h_enc_sum3b,      nullptr, 1.0, 0, 10.0, 0,   DOM_UNRESTRICTED},
        {"SumLinReorder","sqr+lin(2,0.5)",  h_enc_sum2b,     nullptr, 3.0, 0, 10.0, 0,   DOM_UNRESTRICTED},
        // MultiQoI
        {"Multi2",       "sqr|abs",         h_enc_sqr,       h_enc_multi_abs, 1.0, 1.0, 10.0, 0,   DOM_UNRESTRICTED},
        {"MultiSum",     "sqr+cubic|exp(3)+sin", h_enc_sqr,  h_enc_multi_sum, 1.0, 1.0, 10.0, 5.0, DOM_UNRESTRICTED},
        // Compose non-default
        {"CompAbsSin",   "abs@sin",         h_enc_comp_abs_sin, nullptr,       0.1, 0, 5.0,  0,   DOM_UNRESTRICTED},
        {"CompNested",   "sqrt@exp(2)@sqr", h_enc_comp_sqrt_exp2_sqr, nullptr, 1.0, 0, 10.0, 3.0, DOM_UNRESTRICTED},
        {"CompExpCubic", "exp@cubic",       h_enc_comp_exp_cubic,  nullptr,    1.0, 0, 10.0, 0,   DOM_UNRESTRICTED},
        // FX fallback (SymEngine arbitrary functions, no wrapper needed)
        {"FX_SinX2",     "sin(x)+x^2",           h_fx_sin_x2,     nullptr, 1.0, 0, 10.0, 0, DOM_UNRESTRICTED},
        {"FX_SqrtExp",   "sqrt(x)+exp(-x)",      h_fx_sqrt_exp,   nullptr, 1.0, 0, 10.0, 0, DOM_NON_NEGATIVE},
        {"FX_X3_2X_1",   "x^3+2*x+1",            h_fx_x3_2x_1,    nullptr, 1.0, 0, 10.0, 0, DOM_UNRESTRICTED},
        // Isoline mode
        {"Iso6Sqr",     "iso6(sqr, -5, 5, 3, 0.01)",         h_enc_sqr,  nullptr, 1.0, 0, 10.0, 0,  DOM_UNRESTRICTED},
        {"Iso6Sqrt",    "iso6(sqrt, 0, 10, 3, 0.001)",       h_enc_sqrt, nullptr, 0.1, 0, 5.0,  0,  DOM_NON_NEGATIVE},
        {"Iso6Abs",     "iso6(abs, -3, 3, 3, 0.001)",        h_enc_abs,  nullptr, 1.0, 0, 10.0, 0,  DOM_UNRESTRICTED},
        {"Iso6Cubic",   "iso6(cubic, -5, 5, 3, 0.01)",       h_enc_cubic,nullptr, 1.0, 0, 10.0, 0,  DOM_UNRESTRICTED},
        {"Iso6Sin",     "iso6(sin, -1, 1, 3, 0.001)",         h_enc_sin,  nullptr, 0.1, 0, 5.0,  0,  DOM_UNRESTRICTED},
        {"Iso6Exp",     "iso6(exp, 0, 5, 3, 0.001)",          h_enc_exp,  nullptr, 1.0, 0, 10.0, 3.0, DOM_UNRESTRICTED},
        {"Iso6Sum",     "iso6(sqr+cubic, -5, 5, 3, 0.01)",   h_enc_sum2, nullptr, 1.0, 0, 10.0, 0,  DOM_UNRESTRICTED},
        {"Iso6Multi",   "iso6(sqr, -5, 5, 3, 0.01 ; abs, -3, 3, 3, 0.001)", h_enc_sqr, h_enc_multi_abs, 1.0, 1.0, 10.0, 0,  DOM_UNRESTRICTED},
    };
    count = sizeof(list) / sizeof(list[0]);
    return list;
}

}  // namespace sz3_test
#endif
