// Encoder roundtrip test cases.
// Each entry: qoi + qoiParams (expected encoder output) + hardcoded eval function.
// The hardcoded eval is INDEPENDENT of the encoder and compressor — it directly
// implements the mathematical formula so that encoder/decoder bugs cannot hide.
//
// Usage: run_encoder_tests(build_config_fn, data, ...)
//   build_config_fn adds qoi+qoiParams to a Config before testing.

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
    const char *label;          // human-readable test name
    const char *expr;           // original expression (for reference only)
    int qoi;                    // expected qoi nibble encoding
    const char *qoiParams;      // expected base64 params ("" if none)
    double (*feval)(double);    // hardcoded f(x), independent of qoi->eval
    double qEB;                 // QOI tolerance
    double absErrorBound;       // global error bound
    QoiDomain domain;
};

// ============================================================================
//  Hardcoded eval functions
// ============================================================================

static double h_lin_default(double x) { return x; }
static double h_lin_custom(double x) { return 2.0 * x + 0.5; }
static double h_sqr(double x) { return x * x; }
static double h_cubic(double x) { return x * x * x; }
static double h_sqrt(double x) { return std::sqrt(x); }
static double h_exp_default(double x) { return std::exp(x); }
static double h_exp_custom(double x) { return std::pow(3.0, x); }
static double h_xlogx(double x) { return x * std::log(x); }
static double h_log_default(double x) { return std::log(x); }
static double h_log_custom(double x) { return std::log(x) / std::log(2.0); }
static double h_recip(double x) { return 1.0 / x; }
static double h_abs(double x) { return std::fabs(x); }
static double h_sin(double x) { return std::sin(x); }
static double h_tanh(double x) { return std::tanh(x); }
static double h_pow_default(double x) { return x * x; }
static double h_pow_custom(double x) { return std::pow(x, 3.5); }

// SumQoI
static double h_sum_2(double x) { return h_sqr(x) + h_cubic(x); }
static double h_sum_3(double x) { return h_lin_custom(x) + h_exp_custom(x) + h_sqr(x); }
static double h_sum_2b(double x) { return h_lin_custom(x) + h_sqr(x); }
static double h_sum_3b(double x) { return h_abs(x) + h_sin(x) + h_tanh(x); }

// MultiQoI — use the first group's function as the representative check
static double h_multi_and(double x) { return h_sqr(x); }

// Compose
static double h_comp_exp_sqr(double x) { return std::exp(x * x); }
static double h_comp_abs_sin(double x) { return std::fabs(std::sin(x)); }
static double h_comp_sqrt_exp2_sqr(double x) { return std::sqrt(std::pow(2.0, x * x)); }
static double h_comp_cubic_lin(double x) {
    double v = 3.0 * x + 1.0;
    return v * v * v;
}
static double h_comp_exp_cubic(double x) { return std::exp(x * x * x); }

// ============================================================================
//  Test case registry
// ============================================================================

inline const EncoderTestCase *all_encoder_tests(int &count) {
    static const EncoderTestCase list[] = {
        // ---- Base functions with default params ----
        {"LinDefault",  "lin",        0x0,   "",   h_lin_default,   1.0,  10.0, DOM_UNRESTRICTED},
        {"Sqr",         "sqr",        0x1,   "",   h_sqr,           1.0,  10.0, DOM_UNRESTRICTED},
        {"Cubic",       "cubic",      0x2,   "",   h_cubic,         1.0,  10.0, DOM_UNRESTRICTED},
        {"Sqrt",        "sqrt",       0x3,   "",   h_sqrt,          0.1,  5.0,  DOM_NON_NEGATIVE},
        {"ExpDefault",  "exp",        0x4,   "",   h_exp_default,   1.0,  10.0, DOM_UNRESTRICTED},
        {"XLogX",       "xlogx",      0x5,   "",   h_xlogx,         1.0,  10.0, DOM_POSITIVE},
        {"LogDefault",  "log",        0x6,   "",   h_log_default,   0.1,  5.0,  DOM_POSITIVE},
        {"Recip",       "recip",      0x7,   "",   h_recip,         1.0,  10.0, DOM_NON_ZERO},
        {"Abs",         "abs",        0x8,   "",   h_abs,           1.0,  10.0, DOM_UNRESTRICTED},
        {"Sin",         "sin",        0x9,   "",   h_sin,           0.1,  5.0,  DOM_UNRESTRICTED},
        {"Tanh",        "tanh",       0xA,   "",   h_tanh,          0.1,  5.0,  DOM_UNRESTRICTED},
        {"PowDefault",  "pow",        0xB,   "",   h_pow_default,   1.0,  10.0, DOM_UNRESTRICTED},

        // ---- Base functions with custom params ----
        {"LinCustom",   "lin(2,0.5)",    0x0,
         "AAAAAAAAAEAAAAAAAADgPw==",      h_lin_custom,   3.0,  10.0, DOM_UNRESTRICTED},
        {"ExpCustom",   "exp(3)",        0x4,
         "AAAAAAAACEA=",                  h_exp_custom,   1.0,  10.0, DOM_UNRESTRICTED},
        {"LogCustom",   "log(2)",        0x6,
         "AAAAAAAAAEA=",                  h_log_custom,   0.1,  5.0,  DOM_POSITIVE},
        {"PowCustom",   "pow(3.5)",      0xB,
         "AAAAAAAADEA=",                 h_pow_custom,   1.0,  10.0, DOM_UNRESTRICTED},

        // ---- SumQoI ----
        {"Sum2",        "sqr+cubic",        0x12,  "",  h_sum_2,       1.0,  10.0, DOM_UNRESTRICTED},
        {"Sum3Param",   "lin(2,0.5)+exp(3)+sqr",  0x140,
         "AAAAAAAAAEAAAAAAAADgPwAAAAAAAAhA",    h_sum_3,       1.0,  10.0, DOM_UNRESTRICTED},
        {"Sum3NoParam", "abs+sin+tanh",     0xA98, "",  h_sum_3b,      1.0,  10.0, DOM_UNRESTRICTED},
        {"SumLinReorder","sqr+lin(2,0.5)",  0x10,
         "AAAAAAAAAEAAAAAAAADgPw==",          h_sum_2b,      3.0,  10.0, DOM_UNRESTRICTED},

        // ---- MultiQoI ----
        {"Multi2",      "sqr|abs",          0x8F1, "",  h_multi_and,   1.0,  10.0, DOM_UNRESTRICTED},
        {"MultiSum",    "sqr+cubic|exp(3)+sin",  0x94F21,
         "AAAAAAAACEA=",                     h_multi_and,   1.0,  10.0, DOM_UNRESTRICTED},

        // ---- Compose ----
        {"CompExpSqr",     "exp@sqr",         0x14E, "",  h_comp_exp_sqr,       1.0,  10.0, DOM_UNRESTRICTED},
        {"CompAbsSin",     "abs@sin",         0x9E8, "",  h_comp_abs_sin,       0.1,  5.0,  DOM_UNRESTRICTED},
        {"CompNested",     "sqrt@exp(2)@sqr", 0x143EE,
         "AAAAAAAAAEA=",                   h_comp_sqrt_exp2_sqr, 1.0,  10.0, DOM_UNRESTRICTED},
        {"CompExpCubic",   "exp@cubic",        0x2E4, "",  h_comp_exp_cubic,     1.0,  10.0, DOM_UNRESTRICTED},
    };

    // Helper lambda for SumLinReorder test:
    // We declared it outside because it needs a lambda capture; instead define inline.
    // Actually we need h_sum_2b = lin(2,0.5)+sqr. Let's add a proper function.
    // Wait - the reorder moves lin to front, so qoi=0x10 = [lin, sqr]. The eval is the same.
    // Let me add a real h_sum_2b function... Actually I forgot to define it above.
    // For now, the test will hit a linker error for SumLinReorder. Let me fix below.

    count = sizeof(list) / sizeof(list[0]);
    return list;
}

}  // namespace sz3_test

#endif
