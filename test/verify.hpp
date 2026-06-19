// Shared verification helpers -- all QOI checks use hardcoded f(x) only.

#ifndef SZ3_TEST_VERIFY_HPP
#define SZ3_TEST_VERIFY_HPP

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "SZ3/api/sz.hpp"

namespace sz3_test {

struct TestResult {
    bool passed = true;
    const char *fail_reason = nullptr;
    double max_abs = 0;
    double max_qoi = 0;
    double max_qoi2 = 0;   // second group for MultiQoI
    size_t cmp_size = 0;
    size_t qoi_fails = 0;
    std::string detail;
};

inline double max_abs_err(const double *a, const double *b, size_t n) {
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(a[i] - b[i]);
        if (e > me) me = e;
    }
    return me;
}

// Hardcoded per-point QOI check: |f(x_i) - f(x_i')| ≤ tau for all i.
inline bool check_hardcoded_comply(double (*feval)(double), double tau,
                                    const double *orig, const double *dec, size_t n,
                                    size_t *fail_count) {
    size_t fc = 0;
    for (size_t i = 0; i < n; ++i) {
        if (std::fabs(feval(orig[i]) - feval(dec[i])) > tau * (1.0 + 1e-12))
            fc++;
    }
    *fail_count = fc;
    return fc == 0;
}

inline double max_hardcoded_qoi(double (*feval)(double),
                                 const double *orig, const double *dec, size_t n) {
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(feval(orig[i]) - feval(dec[i]));
        if (e > me) me = e;
    }
    return me;
}

// Generic compress-decompress roundtrip.
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
        r.passed = false; r.fail_reason = "cmp-exception"; r.detail = e.what();
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
        r.passed = false; r.fail_reason = "dec-exception"; r.detail = e.what();
        return nullptr;
    }
    if (!dec) { r.passed = false; r.fail_reason = "dec-null"; return nullptr; }
    if (conf2.num != num) { delete[] dec; r.passed = false; r.fail_reason = "num-mismatch"; return nullptr; }
    return dec;
}

}  // namespace sz3_test
#endif
