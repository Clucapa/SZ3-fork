#ifndef SZ3_TEST_CONFIG_HPP
#define SZ3_TEST_CONFIG_HPP

#include <string>
#include <vector>
#include <cstdint>

#include "SZ3/api/sz.hpp"

namespace sz3_test {

enum QoiDomain { DOM_UNRESTRICTED, DOM_POSITIVE, DOM_NON_NEGATIVE, DOM_NON_ZERO };

// algo values for the test loop (avoid collision with SZ3::ALGO_*)
constexpr int TALGO_BLOCK          = 0;
constexpr int TALGO_INTERP         = 1;
constexpr int TALGO_INTERP_LORENZO = 2;

struct QoiDef {
    int id;
    const char *name;
    QoiDomain domain;
    bool is_regional;
    double qEB;           // default QOI tolerance
    double absErrorBound; // generous bound, let QOI do the constraining
};

inline const QoiDef *all_qois() {
    static const QoiDef list[] = {
        // ---- pointwise base functions ----
        {0x0, "XLin",       DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x1, "X2",         DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x2, "XCubic",     DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x3, "XSqrt",      DOM_NON_NEGATIVE, false, 0.1,  5.0},
        {0x4, "XExp",       DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x5, "XLogX",      DOM_POSITIVE,     false, 1.0,  10.0},
        {0x6, "LogX",       DOM_POSITIVE,     false, 0.1,  5.0},
        {0x7, "XRecip",     DOM_NON_ZERO,     false, 1.0,  10.0},
        {0x8, "XAbs",       DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x9, "XSin",       DOM_UNRESTRICTED, false, 0.1,  5.0},
        {0xA, "XTanh",      DOM_UNRESTRICTED, false, 0.1,  5.0},
        {0xB, "XPower",     DOM_UNRESTRICTED, false, 1.0,  10.0},
        // ---- composite pointwise ----
        {0x12, "SumQoI",    DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x1F3,"MultiQoI",  DOM_UNRESTRICTED, false, 1.0,  10.0},
        {0x14E,"Compose",   DOM_UNRESTRICTED, false, 1.0,  10.0},
        // ---- regional ----
        {~0,   "RegMean",   DOM_UNRESTRICTED, true,  2.0,  5.0},
        {~1,   "RegMeanSq", DOM_UNRESTRICTED, true,  200.0,  10.0},
        {~2,   "RegAvgInt", DOM_UNRESTRICTED, true,  2.0,  5.0},
        {~3,   "RegMeanSqI",DOM_UNRESTRICTED, true,  200.0,  10.0},
    };
    return list;
}

inline int num_qois()        { return 19; }
inline int num_data_patterns() { return 8; }

inline size_t dim_size(uint N, size_t dft1) {
    switch (N) {
        case 1: return dft1;
        case 2: return 32;
        case 3: return 18;
        default: return dft1;
    }
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
            conf.lorenzo = true;
            conf.lorenzo2 = false;
            conf.regression = false;
            break;
        case TALGO_INTERP:
            conf.cmprAlgo = SZ3::ALGO_INTERP;
            break;
        case TALGO_INTERP_LORENZO:
            conf.cmprAlgo = SZ3::ALGO_INTERP_LORENZO;
            break;
    }

    if (algo == TALGO_INTERP || algo == TALGO_INTERP_LORENZO) {
        conf.interpAlgo = interp_algo;
    }

    return conf;
}

inline bool data_ok_for_qoi(const QoiDef &qd, const std::vector<double> &data) {
    for (auto v : data) {
        switch (qd.domain) {
            case DOM_NON_NEGATIVE: if (v < 0) return false; break;
            case DOM_POSITIVE:     if (v <= 0) return false; break;
            case DOM_NON_ZERO:     if (v == 0.0) return false; break;
            default: break;
        }
    }
    return true;
}

// Aggregate constraint check for regional QOIs.
// Returns true if constraint satisfied, false otherwise.
// *agg_val = computed aggregate (mean or mean-of-square) difference.
inline bool check_regional_aggregate(const QoiDef &qd, size_t n,
                                      const double *orig, const double *dec,
                                      double *agg_val) {
    double sum_err = 0.0;
    int rid = ~qd.id;
    if (rid == 0 || rid == 2) {
        // RegionalMean / RegionalAvgInterp: |mean(orig) - mean(dec)| ≤ qEB
        for (size_t i = 0; i < n; ++i) sum_err += orig[i] - dec[i];
        *agg_val = std::fabs(sum_err) / n;
        return *agg_val <= qd.qEB * (1.0 + 1e-8);
    } else {
        // RegionalMeanSq / RegionalMeanSqInterp: |mean(orig²) - mean(dec²)| ≤ qEB
        for (size_t i = 0; i < n; ++i)
            sum_err += orig[i] * orig[i] - dec[i] * dec[i];
        *agg_val = std::fabs(sum_err) / n;
        return *agg_val <= qd.qEB * (1.0 + 1e-8);
    }
}

}  // namespace sz3_test

#endif
