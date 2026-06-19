// Encoder roundtrip test cases.
// Each entry: an expression string. At runtime encode() produces qoi+qoiParams,
// fed to the compressor. Verification uses hardcoded feval — independent of both.
// For MultiQoI, feval2 checks the second group's constraint.

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
#include "../tools/qoi_encoder/encode.hpp"

namespace sz3_test {

struct EncoderTestCase {
    const char *label;
    const char *expr;
    double (*feval)(double);
    double (*feval2)(double);  // second group for MultiQoI, nullptr otherwise
    double qEB;
    double qEB2;
    double absErrorBound;
    QoiDomain domain;
};

// ============================================================================
//  Hardcoded eval functions
// ============================================================================

static double h_enc_lin(double x)        { return x; }
static double h_enc_lin_custom(double x) { return 2.0 * x + 0.5; }
static double h_enc_sqr(double x)        { return x * x; }
static double h_enc_cubic(double x)      { return x * x * x; }
static double h_enc_sqrt(double x)       { return std::sqrt(x); }
static double h_enc_exp(double x)        { return std::exp(x); }
static double h_enc_exp3(double x)       { return std::pow(3.0, x); }
static double h_enc_xlogx(double x)      { return x * std::log(x); }
static double h_enc_log(double x)        { return std::log(x); }
static double h_enc_log2(double x)       { return std::log(x) / std::log(2.0); }
static double h_enc_recip(double x)      { return 1.0 / x; }
static double h_enc_abs(double x)        { return std::fabs(x); }
static double h_enc_sin(double x)        { return std::sin(x); }
static double h_enc_tanh(double x)       { return std::tanh(x); }
static double h_enc_pow(double x)        { return x * x; }
static double h_enc_pow35(double x)      { return std::pow(x, 3.5); }
static double h_enc_sum2(double x)       { return h_enc_sqr(x) + h_enc_cubic(x); }
static double h_enc_sum2b(double x)      { return h_enc_lin_custom(x) + h_enc_sqr(x); }
static double h_enc_sum3(double x)       { return h_enc_lin_custom(x) + h_enc_exp3(x) + h_enc_sqr(x); }
static double h_enc_sum3b(double x)      { return h_enc_abs(x) + h_enc_sin(x) + h_enc_tanh(x); }
static double h_enc_comp_exp_sqr(double x)  { return std::exp(x * x); }
static double h_enc_comp_abs_sin(double x)  { return std::fabs(std::sin(x)); }
static double h_enc_comp_sqrt_exp2_sqr(double x) { return std::sqrt(std::pow(2.0, x * x)); }
static double h_enc_comp_exp_cubic(double x)     { return std::exp(x * x * x); }

// MultiQoI second-group evals
static double h_enc_multi_abs(double x)   { return h_enc_abs(x); }
static double h_enc_multi_sum(double x)   { return h_enc_exp3(x) + h_enc_sin(x); }

inline const EncoderTestCase *all_encoder_tests(int &count) {
    static const EncoderTestCase list[] = {
        {"LinDefault",   "lin",            h_enc_lin,        nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Sqr",          "sqr",            h_enc_sqr,        nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Cubic",        "cubic",          h_enc_cubic,      nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Sqrt",         "sqrt",           h_enc_sqrt,       nullptr,             0.1, 0, 5.0,  DOM_NON_NEGATIVE},
        {"ExpDefault",   "exp",            h_enc_exp,        nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"XLogX",        "xlogx",          h_enc_xlogx,      nullptr,             1.0, 0, 10.0, DOM_POSITIVE},
        {"LogDefault",   "log",            h_enc_log,        nullptr,             0.1, 0, 5.0,  DOM_POSITIVE},
        {"Recip",        "recip",          h_enc_recip,      nullptr,             1.0, 0, 10.0, DOM_NON_ZERO},
        {"Abs",          "abs",            h_enc_abs,        nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Sin",          "sin",            h_enc_sin,        nullptr,             0.1, 0, 5.0,  DOM_UNRESTRICTED},
        {"Tanh",         "tanh",           h_enc_tanh,       nullptr,             0.1, 0, 5.0,  DOM_UNRESTRICTED},
        {"PowDefault",   "pow",            h_enc_pow,        nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"LinCustom",    "lin(2,0.5)",     h_enc_lin_custom, nullptr,             3.0, 0, 10.0, DOM_UNRESTRICTED},
        {"ExpCustom",    "exp(3)",         h_enc_exp3,       nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"LogCustom",    "log(2)",         h_enc_log2,       nullptr,             0.1, 0, 5.0,  DOM_POSITIVE},
        {"PowCustom",    "pow(3.5)",       h_enc_pow35,      nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Sum2",         "sqr+cubic",      h_enc_sum2,       nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Sum3Param",   "lin(2,0.5)+exp(3)+sqr", h_enc_sum3,        nullptr,      1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"Sum3NoParam",  "abs+sin+tanh",   h_enc_sum3b,      nullptr,             1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"SumLinReorder","sqr+lin(2,0.5)",  h_enc_sum2b,     nullptr,             3.0, 0, 10.0, DOM_UNRESTRICTED},
        // MultiQoI: both groups checked independently
        {"Multi2",       "sqr|abs",         h_enc_sqr,       h_enc_multi_abs,     1.0, 1.0, 10.0, DOM_UNRESTRICTED},
        {"MultiSum",     "sqr+cubic|exp(3)+sin", h_enc_sqr,  h_enc_multi_sum,     1.0, 1.0, 10.0, DOM_UNRESTRICTED},
        {"CompExpSqr",   "exp@sqr",         h_enc_comp_exp_sqr, nullptr,           1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"CompAbsSin",   "abs@sin",         h_enc_comp_abs_sin, nullptr,           0.1, 0, 5.0,  DOM_UNRESTRICTED},
        {"CompNested",   "sqrt@exp(2)@sqr", h_enc_comp_sqrt_exp2_sqr, nullptr,     1.0, 0, 10.0, DOM_UNRESTRICTED},
        {"CompExpCubic", "exp@cubic",       h_enc_comp_exp_cubic,  nullptr,        1.0, 0, 10.0, DOM_UNRESTRICTED},
    };
    count = sizeof(list) / sizeof(list[0]);
    return list;
}

}  // namespace sz3_test
#endif
