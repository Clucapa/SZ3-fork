// Isoline-specific test runners -- use hardcoded check_isoline_comply only.
// Triggered by e2e --isoline flag. Not part of --fast or --full.

#ifndef SZ3_TEST_ISOLINE_TESTS_HPP
#define SZ3_TEST_ISOLINE_TESTS_HPP

#include <cstdio>
#include <cmath>
#include <array>
#include <vector>
#include <string>

#include "SZ3/api/sz.hpp"
#include "data_gen.hpp"
#include "test_config.hpp"
#include "verify.hpp"

using namespace sz3_test;

// ============================================================================
//  Isoline test case definition
// ============================================================================
struct IsolineTestCase {
    const char *label;
    const char *expr;
    double qEB;
    double absEB;
    double (*feval)(double);
    double iso_min;
    double iso_max;
    int iso_count;
    double meb;
};

static double ih_xlin(double x)  { return x; }
static double ih_x2(double x)    { return x * x; }

static inline std::vector<IsolineTestCase> all_isoline_tests() {
    return {
        {"Iso6-XLin", "iso6(lin, -5, 5, 3, 0.01)",
         5.0, 10.0, ih_xlin, -5.0, 5.0, 3, 0.01},
        {"Iso6-X2",   "iso6(sqr, -5, 5, 3, 0.01)",
         5.0, 10.0, ih_x2,   -5.0, 5.0, 3, 0.01},
    };
}

// ============================================================================
//  Run one isoline test case (hardcoded verification, no conf.qoi dependency)
// ============================================================================
static TestResult run_isoline_test(const IsolineTestCase &tc, uint N,
                                    int algo, uint8_t ia,
                                    int pat, std::array<size_t,3> dims) {
    using namespace SZ3;
    TestResult r;
    size_t num = dims[0];
    if (N >= 2) num *= dims[1];
    if (N >= 3) num *= dims[2];

    auto data = generate_data(pat, N, num, dims);

    // Use encoder to produce qoi/qoiParams
    extern std::string g_encoder_path;
    extern bool call_encoder(const std::string &, int &, std::string &, std::string &);
    if (g_encoder_path.empty()) { r.fail_reason = "skip-no-encoder"; return r; }
    int enc_qoi = 0; std::string enc_b64; std::string err;
    if (!call_encoder(tc.expr, enc_qoi, enc_b64, err))
        { r.passed=false; r.fail_reason="encoder-error"; r.detail=err; return r; }

    Config conf(num);
    conf.setDims(dims.begin(), dims.begin() + N);
    conf.qoi = enc_qoi;
    conf.qoiParams = sz3_test::base64_decode_raw(enc_b64);
    conf.qEB = tc.qEB;
    conf.absErrorBound = tc.absEB;
    conf.quantbinCnt = 65536;
    conf.qR = 32;

    switch (algo) {
        case TALGO_BLOCK:
            conf.cmprAlgo = ALGO_LORENZO_REG;
            conf.lorenzo = true; conf.lorenzo2 = false; conf.regression = false;
            break;
        case TALGO_INTERP:
            conf.cmprAlgo = ALGO_INTERP;
            break;
        case TALGO_INTERP_LORENZO:
            conf.cmprAlgo = ALGO_INTERP_LORENZO;
            break;
    }
    if (algo == TALGO_INTERP || algo == TALGO_INTERP_LORENZO)
        conf.interpAlgo = ia;

    // Generate expected isovalues (must match QoI_IsolineNibble logic).
    std::vector<double> isovals = generate_isovalues(tc.iso_min, tc.iso_max, tc.iso_count);

    size_t cmpSize = 0;
    double *dec = roundtrip_compress(conf, data.data(), num, cmpSize, r);
    if (!dec) return r;

    // Hardcoded isoline check: verify no isovalue crossing.
    size_t iso_fails = 0;
    if (!check_isoline_comply(tc.feval, isovals.data(), isovals.size(),
                               data.data(), dec, num, &iso_fails)) {
        r.passed = false;
        r.fail_reason = "isoline-violation";
        r.qoi_fails = iso_fails;
        r.max_abs = max_abs_err(data.data(), dec, num);
    } else {
        r.max_abs = max_abs_err(data.data(), dec, num);
    }

    delete[] dec;
    return r;
}

#endif
