// Shared verification helpers for the e2e test suite.
// Included by both qoi-matrix and encoder roundtrip test paths.

#ifndef SZ3_TEST_VERIFY_HPP
#define SZ3_TEST_VERIFY_HPP

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "SZ3/api/sz.hpp"

namespace sz3_test {

// Result of a single compress-decompress-verify roundtrip.
struct TestResult {
    bool passed = true;
    const char *fail_reason = nullptr;
    double max_abs = 0;
    double max_qoi = 0;
    size_t cmp_size = 0;
    size_t qoi_fails = 0;
    std::string detail;
};

// Max absolute error between two vectors.
inline double max_abs_err(const double *a, const double *b, size_t n) {
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(a[i] - b[i]);
        if (e > me) me = e;
    }
    return me;
}

// Skip test if QOI eval overflows on the generated data.
inline bool qoi_eval_ok(SZ3::Config &conf, const double *data, size_t n) {
    auto qoi = SZ3::GetQOI<double, 1>(conf);
    if (!qoi) return false;
    for (size_t i : {size_t(0), n / 4, n / 2, 3 * n / 4, n - 1}) {
        if (i >= n) continue;
        if (!std::isfinite(qoi->eval(data[i]))) return false;
    }
    return true;
}

// Per-point QOI compliance check using qoi->check_comply.
inline bool check_qoi_comply_all(SZ3::Config &conf,
                                  const double *orig, const double *dec, size_t n,
                                  size_t *fail_count, size_t *first_fail) {
    auto qoi = SZ3::GetQOI<double, 1>(conf);
    if (!qoi) { *fail_count = 0; return true; }
    size_t fc = 0, ff = 0;
    bool found = false;
    for (size_t i = 0; i < n; ++i) {
        if (!qoi->check_comply(orig[i], dec[i])) {
            fc++;
            if (!found) { ff = i; found = true; }
        }
    }
    *fail_count = fc;
    if (first_fail) *first_fail = ff;
    return fc == 0;
}

// Per-point QOI check using a hardcoded eval function (independent of qoi->eval).
inline bool check_hardcoded_comply(double (*feval)(double), double qEB,
                                    const double *orig, const double *dec, size_t n,
                                    size_t *fail_count) {
    size_t fc = 0;
    for (size_t i = 0; i < n; ++i) {
        if (std::fabs(feval(orig[i]) - feval(dec[i])) > qEB)
            fc++;
    }
    *fail_count = fc;
    return fc == 0;
}

// Max hardcoded QOI error for diagnostics.
inline double max_hardcoded_qoi(double (*feval)(double),
                                 const double *orig, const double *dec, size_t n) {
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(feval(orig[i]) - feval(dec[i]));
        if (e > me) me = e;
    }
    return me;
}

// Max QOI error via qoi->eval (for diagnostics).
inline double max_qoi_err(SZ3::Config &conf,
                           const double *orig, const double *dec, size_t n) {
    auto qoi = SZ3::GetQOI<double, 1>(conf);
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(qoi->eval(orig[i]) - qoi->eval(dec[i]));
        if (e > me) me = e;
    }
    return me;
}

// Generic compress-decompress roundtrip.  Returns raw dec array (caller must delete[]).
inline double *roundtrip_compress(SZ3::Config &conf, const double *data, size_t num,
                                   size_t &cmpSize_out, TestResult &r) {
    using namespace SZ3;
    cmpSize_out = 0;

    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);

    try {
        cmpSize_out = SZ_compress<double>(conf, data,
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    } catch (const std::exception &e) {
        r.passed = false;
        r.fail_reason = "cmp-exception";
        r.detail = e.what();
        return nullptr;
    }
    r.cmp_size = cmpSize_out;
    if (cmpSize_out == 0) { r.passed = false; r.fail_reason = "cmpSize=0"; return nullptr; }

    Config conf2;
    double *dec = nullptr;
    try {
        dec = SZ_decompress<double>(conf2,
                reinterpret_cast<const char *>(cmp.data()), cmpSize_out);
    } catch (const std::exception &e) {
        r.passed = false;
        r.fail_reason = "dec-exception";
        r.detail = e.what();
        return nullptr;
    }
    if (!dec) { r.passed = false; r.fail_reason = "dec-null"; return nullptr; }
    if (conf2.num != num) {
        delete[] dec;
        r.passed = false;
        r.fail_reason = "num-mismatch";
        return nullptr;
    }
    return dec;
}

// Check global absolute error bound.
inline bool check_abs_bound(const TestResult &r, SZ3::Config &conf, SZ3::Config &conf2) {
    using namespace SZ3;
    if (conf2.cmprAlgo == ALGO_LORENZO_REG || conf2.cmprAlgo == ALGO_INTERP ||
        conf2.cmprAlgo == ALGO_INTERP_LORENZO) {
        if (r.max_abs > conf.absErrorBound * (1.0 + 1e-6))
            return false;
    }
    return true;
}

}  // namespace sz3_test

#endif
