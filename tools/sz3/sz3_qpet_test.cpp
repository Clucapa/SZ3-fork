#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

#include "SZ3/api/sz.hpp"

static void gen_data(std::vector<double> &d, size_t n, double lo, double hi) {
    d.resize(n);
    double step = (hi - lo) / (n - 1);
    for (size_t i = 0; i < n; ++i) d[i] = lo + step * i;
}

static double max_err(const std::vector<double> &a, const std::vector<double> &b) {
    double me = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        double e = fabs(a[i] - b[i]);
        if (e > me) me = e;
    }
    return me;
}

static double max_qoi_err(const std::vector<double> &a, const std::vector<double> &b) {
    double me = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        double e = fabs(a[i] * a[i] - b[i] * b[i]);
        if (e > me) me = e;
    }
    return me;
}

int main() {
    fprintf(stderr, "[0] start\n"); fflush(stderr);
    size_t n = 100000;
    std::vector<double> orig;
    gen_data(orig, n, 0.0, 100.0);
    fprintf(stderr, "[1] data generated %zu points\n", n); fflush(stderr);

    int pass = 0, fail = 0;

    // ===== Test 1: qoi=1 (x^2), tau=1.0 =====
    {
        fprintf(stderr, "[2] constructing config\n"); fflush(stderr);
        SZ3::Config conf(n);
        conf.cmprAlgo  = SZ3::ALGO_LORENZO_REG;
        conf.absErrorBound = 10.0;
        conf.qoi       = 1;
        conf.qEB       = 1.0;
        conf.qR        = 32;
        conf.lorenzo   = true;
        conf.lorenzo2  = false;
        conf.regression = false;
        fprintf(stderr, "[3] config done, sizing buffer\n"); fflush(stderr);

        size_t cmpSize = 0;
        auto bound = SZ3::SZ_compress_size_bound<double>(conf);
        fprintf(stderr, "[4] bound=%zu, allocating\n", bound); fflush(stderr);
        std::vector<unsigned char> cmp(bound);
        fprintf(stderr, "[5] compressing...\n"); fflush(stderr);
        cmpSize = SZ_compress<double>(conf, orig.data(),
                    reinterpret_cast<char*>(cmp.data()), cmp.size());
        fprintf(stderr, "[6] compressed size=%zu\n", cmpSize); fflush(stderr);

        SZ3::Config conf2;
        fprintf(stderr, "[7] decompressing...\n"); fflush(stderr);
        double *dec = SZ_decompress<double>(conf2,
                        reinterpret_cast<const char*>(cmp.data()), cmpSize);
        fprintf(stderr, "[8] decompressed, dec=%p, conf2.num=%zu\n",
                                static_cast<void*>(dec), conf2.num); fflush(stderr);

        if (!dec) { printf("FAIL: dec is null\n"); fail++; delete[] dec; goto next_test_1; }
        if (conf2.num != n) { printf("FAIL: num mismatch %zu vs %zu\n", conf2.num, n); fail++; delete[] dec; goto next_test_1; }

        double me_qoi = max_qoi_err(orig, std::vector<double>(dec, dec + n));
        double me_abs = max_err(orig, std::vector<double>(dec, dec + n));
        printf("Test 1 (qoi=1, x^2, tau=1): cmpSize=%zu  max_qoi_err=%.6f  max_abs_err=%.6f\n",
               cmpSize, me_qoi, me_abs);
        if (me_qoi <= 1.0 + 1e-6) { printf("  PASS\n"); pass++; }
        else { printf("  FAIL: qoi err %.6f > 1.0\n", me_qoi); fail++; }

        delete[] dec;
    }
    next_test_1: (void)0;

    // ===== Test 2: qoi=0 (f(x)=x), tau=0.01 =====
    {
        SZ3::Config conf(n);
        conf.cmprAlgo  = SZ3::ALGO_LORENZO_REG;
        conf.absErrorBound = 0.01;
        conf.qoi       = 0;
        conf.qEB       = 0.01;
        conf.qR        = 32;
        conf.lorenzo   = true;
        conf.lorenzo2  = false;
        conf.regression = false;

        size_t cmpSize = 0;
        std::vector<unsigned char> cmp(SZ3::SZ_compress_size_bound<double>(conf));
        cmpSize = SZ_compress<double>(conf, orig.data(),
                    reinterpret_cast<char*>(cmp.data()), cmp.size());

        fprintf(stderr, "[T2] decompressing...\n"); fflush(stderr);
        SZ3::Config conf2;
        double *dec = SZ_decompress<double>(conf2,
                        reinterpret_cast<const char*>(cmp.data()), cmpSize);
        fprintf(stderr, "[T2] decompressed, dec=%p, num=%zu\n",                 static_cast<void*>(dec), conf2.num); fflush(stderr);

        if (!dec) { printf("FAIL T2: dec is null\n"); fail++; delete[] dec; goto end; }
        if (conf2.num != n) { printf("FAIL T2: num mismatch %zu vs %zu\n", conf2.num, n); fail++; delete[] dec; goto end; }

        double me = max_err(orig, std::vector<double>(dec, dec + n));
        printf("Test 2 (qoi=0, f(x)=x, tau=0.01): cmpSize=%zu  max_err=%.6f\n", cmpSize, me);
        if (me <= 0.01 + 1e-6) { printf("  PASS\n"); pass++; }
        else { printf("  FAIL: abs err %.6f > 0.01\n", me); fail++; }

        delete[] dec;
    }
    end:
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
