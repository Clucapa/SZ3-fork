#ifndef SZ3_TEST_CONFIG_HPP
#define SZ3_TEST_CONFIG_HPP

// QOI registry and Config factory for the e2e test suite.
// Every QOI has a hardcoded feval function -- independent of qoi->eval / check_comply.
// For regional QOIs, feval = nullptr; aggregate verification is done separately.

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

#include "SZ3/api/sz.hpp"

namespace sz3_test {

enum QoiDomain { DOM_UNRESTRICTED, DOM_POSITIVE, DOM_NON_NEGATIVE, DOM_NON_ZERO };

constexpr int TALGO_BLOCK          = 0;
constexpr int TALGO_INTERP         = 1;
constexpr int TALGO_INTERP_LORENZO = 2;

// ==========================================================================
//  Hardcoded eval functions
// ==========================================================================

static double h_xlin(double x)         { return x; }
static double h_x2(double x)           { return x * x; }
static double h_xcubic(double x)       { return x * x * x; }
static double h_xsqrt(double x)        { return std::sqrt(x); }
static double h_xexp(double x)         { return std::exp(x); }
static double h_xlogx(double x)        { return x * std::log(x); }
static double h_logx(double x)         { return std::log(x); }
static double h_xrecip(double x)       { return 1.0 / x; }
static double h_xabs(double x)         { return std::fabs(x); }
static double h_xsin(double x)         { return std::sin(x); }
static double h_xtanh(double x)        { return std::tanh(x); }
static double h_xpow(double x)         { return x * x; }       // default expo = 2

static double h_sum_xcubic_x2(double x) { return h_xcubic(x) + h_x2(x); }
static double h_comp_exp_x2(double x)   { return std::exp(x * x); }

// MultiQoI group check helpers -- each group has its own hardcoded eval.
// The compressor must satisfy ALL groups simultaneously.
static double h_multi_sqrt(double x)  { return h_xsqrt(x); }
static double h_multi_x2(double x)    { return h_x2(x); }

struct QoiDef {
    int id;
    const char *name;
    QoiDomain domain;
    bool is_regional;
    double qEB;
    double absErrorBound;
    double (*feval)(double);
    double (*feval2)(double);  // second group for MultiQoI, nullptr otherwise
    double qEB2;               // tolerance for second group (same as qEB usually)
};

inline const QoiDef *all_qois() {
    static const QoiDef list[] = {
        // ---- pointwise base ----
        {0x0,  "XLin",    DOM_UNRESTRICTED, false, 1.0,  10.0, h_xlin,        nullptr,    0},
        {0x1,  "X2",      DOM_UNRESTRICTED, false, 1.0,  10.0, h_x2,          nullptr,    0},
        {0x2,  "XCubic",  DOM_UNRESTRICTED, false, 1.0,  10.0, h_xcubic,      nullptr,    0},
        {0x3,  "XSqrt",   DOM_NON_NEGATIVE, false, 0.1,  5.0,  h_xsqrt,       nullptr,    0},
        {0x4,  "XExp",    DOM_UNRESTRICTED, false, 1.0,  10.0, h_xexp,        nullptr,    0},
        {0x5,  "XLogX",   DOM_POSITIVE,     false, 1.0,  10.0, h_xlogx,       nullptr,    0},
        {0x6,  "LogX",    DOM_POSITIVE,     false, 0.1,  5.0,  h_logx,        nullptr,    0},
        {0x7,  "XRecip",  DOM_NON_ZERO,     false, 1.0,  10.0, h_xrecip,      nullptr,    0},
        {0x8,  "XAbs",    DOM_UNRESTRICTED, false, 1.0,  10.0, h_xabs,        nullptr,    0},
        {0x9,  "XSin",    DOM_UNRESTRICTED, false, 0.1,  5.0,  h_xsin,        nullptr,    0},
        {0xA,  "XTanh",   DOM_UNRESTRICTED, false, 0.1,  5.0,  h_xtanh,       nullptr,    0},
        {0xB,  "XPower",  DOM_UNRESTRICTED, false, 1.0,  10.0, h_xpow,        nullptr,    0},
        // ---- composite pointwise ----
        {0x12, "SumQoI",  DOM_UNRESTRICTED, false, 1.0,  10.0, h_sum_xcubic_x2, nullptr, 0},
        {0x1F3,"MultiQoI",DOM_UNRESTRICTED, false, 1.0,  10.0, h_multi_sqrt,    h_multi_x2, 1.0},
        {0x14E,"Compose", DOM_UNRESTRICTED, false, 1.0,  10.0, h_comp_exp_x2,   nullptr,    0},
        // ---- regional ----
        {~0,   "RegMean",   DOM_UNRESTRICTED, true,  2.0,   5.0,  nullptr, nullptr, 0},
        {~1,   "RegMeanSq", DOM_UNRESTRICTED, true,  200.0, 10.0, nullptr, nullptr, 0},
        {~2,   "RegAvgInt", DOM_UNRESTRICTED, true,  2.0,   5.0,  nullptr, nullptr, 0},
        {~3,   "RegMeanSqI",DOM_UNRESTRICTED, true,  200.0, 10.0, nullptr, nullptr, 0},
    };
    return list;
}

inline int num_qois()         { return 19; }
inline int num_data_patterns() { return 8; }

// Cycle 1D/2D sizes through 500–524 using prime-step increments.
// 3D stays at 18 (block-size boundary already non-power-of-2).
inline size_t dim_size(uint N, size_t idx) {
    if (N == 3) return 18;
    return 500 + ((3 * idx) % 25);
}

inline SZ3::Config make_config(const QoiDef &qd, uint N, int algo,
                                uint8_t interp_algo,
                                const std::array<size_t, 3> &dims) {
    size_t num = 1;
    for (uint i = 0; i < N; ++i) num *= dims[i];

    SZ3::Config conf(num);
    conf.setDims(dims.begin(), dims.begin() + N);
    conf.qoi = qd.id;
    conf.qEB = qd.qEB;
    conf.absErrorBound = qd.absErrorBound;
    conf.quantbinCnt = 65536;
    conf.qR = 32;

    switch (algo) {
        case TALGO_BLOCK:
            conf.cmprAlgo = SZ3::ALGO_LORENZO_REG;
            conf.lorenzo = true; conf.lorenzo2 = false; conf.regression = false;
            break;
        case TALGO_INTERP:
            conf.cmprAlgo = SZ3::ALGO_INTERP;
            break;
        case TALGO_INTERP_LORENZO:
            conf.cmprAlgo = SZ3::ALGO_INTERP_LORENZO;
            break;
    }
    if (algo == TALGO_INTERP || algo == TALGO_INTERP_LORENZO)
        conf.interpAlgo = interp_algo;
    return conf;
}

inline bool data_ok_for_qoi(const QoiDef &qd, const std::vector<double> &data) {
    for (auto v : data) {
        switch (qd.domain) {
            case DOM_NON_NEGATIVE: if (v < 0)    return false; break;
            case DOM_POSITIVE:     if (v <= 0)   return false; break;
            case DOM_NON_ZERO:     if (v == 0.0) return false; break;
            default: break;
        }
    }
    return true;
}

// Regional aggregate constraint (hardcoded, independent of qoi->eval).
inline bool check_regional_aggregate(const QoiDef &qd, size_t n,
                                      const double *orig, const double *dec,
                                      double *agg_val) {
    double sum_err = 0.0;
    int rid = ~qd.id;
    if (rid == 0 || rid == 2) {
        for (size_t i = 0; i < n; ++i) sum_err += orig[i] - dec[i];
        *agg_val = std::fabs(sum_err) / n;
    } else {
        for (size_t i = 0; i < n; ++i) sum_err += orig[i] * orig[i] - dec[i] * dec[i];
        *agg_val = std::fabs(sum_err) / n;
    }
    return *agg_val <= qd.qEB * (1.0 + 1e-8);
}

}  // namespace sz3_test
#endif
