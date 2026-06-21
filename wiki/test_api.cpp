#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include "SZ3/api/sz.hpp"

static const double PI = 3.14159265358979323846;

// ---------- data generation ----------
static std::vector<float> gen_data(size_t n) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist01(0, 1);
    std::vector<float> data(n);
    for (size_t i = 0; i < n; ++i) {
        double x = i * 0.1;
        float noise = 0.04f * (dist01(rng) * 2.0f - 1.0f);
        data[i] = float(0.32 * sin(1.3*x + 0.5) + 0.32 * sin(2.7*x + 1.2)
                       + 0.32 * sin(5.1*x + 2.8)) + noise;
    }
    return data;
}

// ---------- verification ----------
static double max_pointwise_err(const float *o, const float *d, size_t n,
                                 double (*f)(double)) {
    double m = 0;
    for (size_t i = 0; i < n; ++i)
        m = std::max(m, std::fabs(f(o[i]) - f(d[i])));
    return m;
}
static double mean_signed_err(const float *o, const float *d, size_t n,
                               double (*f)(double)) {
    double s = 0;
    for (size_t i = 0; i < n; ++i)
        s += f(o[i]) - f(d[i]);
    return std::fabs(s) / n;
}
static bool isoline_check(const float *o, const float *d, size_t n,
                          double min_v, double max_v, int n_iso,
                          double (*f)(double)) {
    double step = (max_v - min_v) / n_iso;
    for (size_t i = 0; i < n; ++i) {
        double fo = f(o[i]), fd = f(d[i]);
        for (int j = 0; j <= n_iso; ++j) {
            double t = min_v + j * step;
            if ((fo - t) * (fd - t) < 0) return false;
        }
    }
    return true;
}
static double max_conv_err(const float *o, const float *d, size_t n,
                            const double *kernel, int w) {
    int c = w / 2;
    double m = 0;
    for (size_t i = (size_t)c; i + (w - 1 - c) < n; ++i) {
        double co = 0, cd = 0;
        for (int j = 0; j < w; ++j) {
            co += kernel[j] * o[i - (size_t)c + j];
            cd += kernel[j] * d[i - (size_t)c + j];
        }
        m = std::max(m, std::fabs(co - cd));
    }
    return m;
}

// ---------- helpers ----------
static double sqr_plus_cubic(double x) { return x*x + x*x*x; }
static double sqr(double x) { return x*x; }

struct TestCase {
    const char *name;
    int qoi;
    std::vector<unsigned char> params;
    double qEB;
    double (*feval)(double) = nullptr;
    double conv_kernel[8] = {};
    int conv_w = 0;
    double conv_tol = 0;
    double iso_min = 0, iso_max = 0;
    int iso_n = 0;

    bool run(const float *orig, size_t n) const {
        SZ3::Config conf(n);
        conf.cmprAlgo = SZ3::ALGO_INTERP_LORENZO;
        conf.qoi = qoi;
        conf.qoiParams = params;
        conf.qEB = qEB;
        conf.absErrorBound = 1e-3;
        conf.quantbinCnt = 65536;
        conf.qR = 65536;
        conf.lorenzo = true;
        conf.regression = true;

        size_t cmpSize = 0;
        char *cmp = SZ_compress(conf, orig, cmpSize);
        if (!cmp || cmpSize == 0) { printf("  FAIL (compress failed)\n"); return false; }
        SZ3::Config conf2 = conf;
        float *dec = SZ_decompress<float>(conf2, cmp, cmpSize);
        delete[] cmp;
        if (!dec) { printf("  FAIL (decompress failed)\n"); return false; }

        bool ok = false;
        if (conv_w > 0) {
            double err = max_conv_err(orig, dec, n, conv_kernel, conv_w);
            ok = err <= conv_tol * 1.0001;
            printf("  max conv err = %.6g  (tol=%.4g)  %s\n",
                   err, conv_tol, ok ? "PASS" : "FAIL");
        } else if (iso_n > 0) {
            bool has_cross = !isoline_check(orig, dec, n, iso_min, iso_max, iso_n, sqr);
            ok = !has_cross;
            printf("  isoline crossings: %s  %s\n",
                   has_cross ? "YES" : "0", ok ? "PASS" : "FAIL");
        } else if (feval) {
            double agg = mean_signed_err(orig, dec, n, feval);
            ok = agg <= std::fabs(qEB) * 1.0001;
            printf("  |mean(f(o)-f(d))| = %.6g  (qEB=%.4g)  %s\n",
                   agg, qEB, ok ? "PASS" : "FAIL");
        } else {
            double err = max_pointwise_err(orig, dec, n, sqr_plus_cubic);
            ok = err <= qEB * 1.0001;
            printf("  max |f(o)-f(d)| = %.6g  (qEB=%.4g)  %s\n",
                   err, qEB, ok ? "PASS" : "FAIL");
        }
        delete[] dec;
        return ok;
    }
};

int main() {
    const size_t N = 10000;
    auto data = gen_data(N);
    printf("=== QOI API 校验测试 ===\n\n");
    int total = 0, passed = 0;

    auto run = [&](const TestCase &tc) {
        ++total;
        printf("--- %s ---\n", tc.name);
        if (tc.run(data.data(), N)) ++passed;
        printf("\n");
    };

    // 1) Nibble - SumQoI sqr+cubic
    run({"nibble (sqr+cubic)", 0x21, {}, 0.01});

    // 2) Regional - SumQoI sqr+cubic
    run({"regional (sqr+cubic)", ~0x21, {}, 1e-6, sqr_plus_cubic});

    // 3) Isoline - sqr, iso6(-5,5,3,0.01)
    run({"isoline (sqr, -5~5/3, meb=0.01)",
         0x60000001,
         SZ3::base64_decode("AAAAAAAAFMAAAAAAAAAUQAAAAAAAAAhAexSuR+F6hD8="),
         1.0, nullptr, {}, 0, 0, -5, 5, 3});

    // 4) Conv - Laplacian [1,-2,1]
    run({"conv (Laplacian, tol=0.005)",
         static_cast<int>(0x8EFFFFFC),
         SZ3::base64_decode("AAAAAAAA8D8AAAAAAAAAwAAAAAAAAPA/exSuR+F6dD8="),
         1.0, nullptr, {1, -2, 1}, 3, 0.005});

    printf("%d total, %d passed, %d failed\n", total, passed, total - passed);
    return passed == total ? 0 : 1;
}
