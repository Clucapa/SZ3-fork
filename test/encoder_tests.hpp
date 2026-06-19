// Encoder roundtrip test cases.
// Each entry: an expression string. At runtime the test calls encode() to get
// qoi + qoiParams, feeds that into the compressor, then verifies compliance
// using a hardcoded eval function (independent of encoder and compressor).

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
    const char *expr;             // expression string, parsed by encode()
    double (*feval)(double);      // hardcoded f(x)
    double qEB;
    double absErrorBound;
    QoiDomain domain;
};

// ============================================================================
//  Hardcoded eval functions
// ============================================================================

static double h_lin_default(double x)  { return x; }
static double h_lin_custom(double x)   { return 2.0 * x + 0.5; }
static double h_sqr(double x)          { return x * x; }
static double h_cubic(double x)         { return x * x * x; }
static double h_sqrt(double x)          { return std::sqrt(x); }
static double h_exp_default(double x)   { return std::exp(x); }
static double h_exp_custom(double x)    { return std::pow(3.0, x); }
static double h_xlogx(double x)         { return x * std::log(x); }
static double h_log_default(double x)   { return std::log(x); }
static double h_log_custom(double x)    { return std::log(x) / std::log(2.0); }
static double h_recip(double x)         { return 1.0 / x; }
static double h_abs(double x)           { return std::fabs(x); }
static double h_sin(double x)           { return std::sin(x); }
static double h_tanh(double x)          { return std::tanh(x); }
static double h_pow_default(double x)   { return x * x; }
static double h_pow_custom(double x)    { return std::pow(x, 3.5); }
static double h_sum_2(double x)         { return h_sqr(x) + h_cubic(x); }
static double h_sum_2b(double x)        { return h_lin_custom(x) + h_sqr(x); }
static double h_sum_3(double x)         { return h_lin_custom(x) + h_exp_custom(x) + h_sqr(x); }
static double h_sum_3b(double x)        { return h_abs(x) + h_sin(x) + h_tanh(x); }
static double h_multi_and(double x)     { return h_sqr(x); }
static double h_comp_exp_sqr(double x)  { return std::exp(x * x); }
static double h_comp_abs_sin(double x)  { return std::fabs(std::sin(x)); }
static double h_comp_sqrt_exp2_sqr(double x) { return std::sqrt(std::pow(2.0, x * x)); }
static double h_comp_exp_cubic(double x)     { return std::exp(x * x * x); }

// ============================================================================
//  Test case registry (expressions only — qoi is computed at runtime by encode())
// ============================================================================

inline const EncoderTestCase *all_encoder_tests(int &count) {
    static const EncoderTestCase list[] = {
        {"LinDefault",   "lin",        h_lin_default,      1.0,  10.0, DOM_UNRESTRICTED},
        {"Sqr",          "sqr",        h_sqr,              1.0,  10.0, DOM_UNRESTRICTED},
        {"Cubic",        "cubic",      h_cubic,            1.0,  10.0, DOM_UNRESTRICTED},
        {"Sqrt",         "sqrt",       h_sqrt,             0.1,  5.0,  DOM_NON_NEGATIVE},
        {"ExpDefault",   "exp",        h_exp_default,      1.0,  10.0, DOM_UNRESTRICTED},
        {"XLogX",        "xlogx",      h_xlogx,            1.0,  10.0, DOM_POSITIVE},
        {"LogDefault",   "log",        h_log_default,      0.1,  5.0,  DOM_POSITIVE},
        {"Recip",        "recip",      h_recip,            1.0,  10.0, DOM_NON_ZERO},
        {"Abs",          "abs",        h_abs,              1.0,  10.0, DOM_UNRESTRICTED},
        {"Sin",          "sin",        h_sin,              0.1,  5.0,  DOM_UNRESTRICTED},
        {"Tanh",         "tanh",       h_tanh,             0.1,  5.0,  DOM_UNRESTRICTED},
        {"PowDefault",   "pow",        h_pow_default,      1.0,  10.0, DOM_UNRESTRICTED},
        {"LinCustom",    "lin(2,0.5)", h_lin_custom,       3.0,  10.0, DOM_UNRESTRICTED},
        {"ExpCustom",    "exp(3)",     h_exp_custom,       1.0,  10.0, DOM_UNRESTRICTED},
        {"LogCustom",    "log(2)",     h_log_custom,       0.1,  5.0,  DOM_POSITIVE},
        {"PowCustom",    "pow(3.5)",   h_pow_custom,       1.0,  10.0, DOM_UNRESTRICTED},
        {"Sum2",         "sqr+cubic",  h_sum_2,            1.0,  10.0, DOM_UNRESTRICTED},
        {"Sum3Param",    "lin(2,0.5)+exp(3)+sqr", h_sum_3,          1.0,  10.0, DOM_UNRESTRICTED},
        {"Sum3NoParam",  "abs+sin+tanh",            h_sum_3b,        1.0,  10.0, DOM_UNRESTRICTED},
        {"SumLinReorder","sqr+lin(2,0.5)",           h_sum_2b,        3.0,  10.0, DOM_UNRESTRICTED},
        {"Multi2",       "sqr|abs",                  h_multi_and,     1.0,  10.0, DOM_UNRESTRICTED},
        {"MultiSum",     "sqr+cubic|exp(3)+sin",     h_multi_and,     1.0,  10.0, DOM_UNRESTRICTED},
        {"CompExpSqr",   "exp@sqr",                  h_comp_exp_sqr,  1.0,  10.0, DOM_UNRESTRICTED},
        {"CompAbsSin",   "abs@sin",                  h_comp_abs_sin,  0.1,  5.0,  DOM_UNRESTRICTED},
        {"CompNested",   "sqrt@exp(2)@sqr",           h_comp_sqrt_exp2_sqr, 1.0,  10.0, DOM_UNRESTRICTED},
        {"CompExpCubic", "exp@cubic",                 h_comp_exp_cubic,     1.0,  10.0, DOM_UNRESTRICTED},
    };
    count = sizeof(list) / sizeof(list[0]);
    return list;
}

}  // namespace sz3_test

#endif
