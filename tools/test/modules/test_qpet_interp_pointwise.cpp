#include <cmath>
#include <vector>
#include <memory>

#include "SZ3/api/sz.hpp"
#include "SZ3/qoi/QoIIf.hpp"
#include "gtest/gtest.h"

using namespace SZ3;

namespace {

template <class T>
void gen_ramp(std::vector<T> &d, size_t n, T lo, T hi) {
    d.resize(n);
    T step = (hi - lo) / static_cast<T>(n - 1);
    for (size_t i = 0; i < n; ++i) d[i] = lo + step * static_cast<T>(i);
}

double max_abs_err(const std::vector<double> &a, const std::vector<double> &b) {
    double me = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        double e = std::fabs(a[i] - b[i]);
        if (e > me) me = e;
    }
    return me;
}

double max_qoi_err(int qoi, const std::vector<double> &a,
                   const std::vector<double> &b) {
    Config c(1);
    c.qoi = qoi;
    auto q = GetQOI<double, 1>(c);
    double me = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        double e = std::fabs(q->eval(a[i]) - q->eval(b[i]));
        if (e > me) me = e;
    }
    return me;
}

}  // namespace

TEST(InterpPointwise, X2Qoi1D) {
    size_t n = 1000;
    std::vector<double> orig;
    gen_ramp(orig, n, 0.0, 100.0);

    Config conf(n);
    conf.cmprAlgo = ALGO_INTERP;
    conf.absErrorBound = 10.0;
    conf.qoi = 1;
    conf.qEB = 1.0;
    conf.qR = 32;

    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    size_t cmpSize = SZ_compress<double>(conf, orig.data(),
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    ASSERT_GT(cmpSize, 0u);

    Config conf2;
    double *dec = SZ_decompress<double>(conf2,
                    reinterpret_cast<const char *>(cmp.data()), cmpSize);
    ASSERT_NE(dec, nullptr);
    ASSERT_EQ(conf2.num, n);

    std::vector<double> dec_vec(dec, dec + n);
    delete[] dec;

    double me_qoi = max_qoi_err(1, orig, dec_vec);
    double me_abs = max_abs_err(orig, dec_vec);

    EXPECT_LE(me_qoi, 1.0 + 1e-8)
        << "QOI constraint violated: max |x_i^2 - y_i^2| = "
        << me_qoi << " > tau = 1.0";
    EXPECT_LE(me_abs, 10.0 + 1e-8)
        << "Absolute error bound violated: max |x_i - y_i| = "
        << me_abs << " > 10.0";
}

TEST(InterpPointwise, IdentityQoi1D) {
    size_t n = 1000;
    std::vector<double> orig;
    gen_ramp(orig, n, -500.0, 500.0);

    Config conf(n);
    conf.cmprAlgo = ALGO_INTERP;
    conf.absErrorBound = 1e-3;
    conf.qoi = 0;
    conf.qEB = 1e-3;
    conf.qR = 32;

    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    size_t cmpSize = SZ_compress<double>(conf, orig.data(),
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    ASSERT_GT(cmpSize, 0u);

    Config conf2;
    double *dec = SZ_decompress<double>(conf2,
                    reinterpret_cast<const char *>(cmp.data()), cmpSize);
    ASSERT_NE(dec, nullptr);
    ASSERT_EQ(conf2.num, n);

    std::vector<double> dec_vec(dec, dec + n);
    delete[] dec;

    double me_abs = max_abs_err(orig, dec_vec);
    EXPECT_LE(me_abs, 1e-3 + 1e-8)
        << "Absolute error bound violated: max |x_i - y_i| = "
        << me_abs << " > 1e-3";
}

TEST(InterpPointwise, X2Qoi1DWithAnchor) {
    size_t n = 256;
    std::vector<double> orig;
    gen_ramp(orig, n, 0.0, 100.0);

    Config conf(n);
    conf.cmprAlgo = ALGO_INTERP;
    conf.absErrorBound = 10.0;
    conf.qoi = 1;
    conf.qEB = 1.0;
    conf.qR = 32;
    conf.interpAnchorStride = 16;

    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    size_t cmpSize = SZ_compress<double>(conf, orig.data(),
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    ASSERT_GT(cmpSize, 0u);

    Config conf2;
    double *dec = SZ_decompress<double>(conf2,
                    reinterpret_cast<const char *>(cmp.data()), cmpSize);
    ASSERT_NE(dec, nullptr);
    ASSERT_EQ(conf2.num, n);

    std::vector<double> dec_vec(dec, dec + n);
    delete[] dec;

    double me_qoi = max_qoi_err(1, orig, dec_vec);
    EXPECT_LE(me_qoi, 1.0 + 1e-8)
        << "QOI constraint violated with anchor: max |x_i^2 - y_i^2| = "
        << me_qoi << " > tau = 1.0";
}

TEST(InterpPointwise, RoundtripConfig) {
    size_t n = 100;
    std::vector<double> orig;
    gen_ramp(orig, n, 0.0, 1.0);

    Config conf(n);
    conf.cmprAlgo = ALGO_INTERP;
    conf.absErrorBound = 1e-2;
    conf.qoi = 1;
    conf.qEB = 1e-2;
    conf.qR = 16;
    conf.interpAlgo = INTERP_ALGO_LINEAR;
    conf.interpAnchorStride = 8;

    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    size_t cmpSize = SZ_compress<double>(conf, orig.data(),
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    ASSERT_GT(cmpSize, 0u);

    Config conf2;
    double *dec = SZ_decompress<double>(conf2,
                    reinterpret_cast<const char *>(cmp.data()), cmpSize);
    ASSERT_NE(dec, nullptr);
    delete[] dec;

    EXPECT_EQ(conf2.cmprAlgo, ALGO_INTERP);
    EXPECT_EQ(conf2.qoi, conf.qoi);
    EXPECT_EQ(conf2.num, n);
}

TEST(InterpPointwise, CubicInterpX2Qoi) {
    size_t n = 256;
    std::vector<double> orig;
    gen_ramp(orig, n, 0.0, 50.0);

    Config conf(n);
    conf.cmprAlgo = ALGO_INTERP;
    conf.interpAlgo = INTERP_ALGO_CUBIC;
    conf.absErrorBound = 10.0;
    conf.qoi = 1;
    conf.qEB = 1.0;
    conf.qR = 32;

    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    size_t cmpSize = SZ_compress<double>(conf, orig.data(),
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    ASSERT_GT(cmpSize, 0u);

    Config conf2;
    double *dec = SZ_decompress<double>(conf2,
                    reinterpret_cast<const char *>(cmp.data()), cmpSize);
    ASSERT_NE(dec, nullptr);

    std::vector<double> dec_vec(dec, dec + n);
    delete[] dec;

    double me_qoi = max_qoi_err(1, orig, dec_vec);
    EXPECT_LE(me_qoi, 1.0 + 1e-8)
        << "QOI constraint violated with cubic interp: max |x_i^2 - y_i^2| = "
        << me_qoi << " > tau = 1.0";
}
