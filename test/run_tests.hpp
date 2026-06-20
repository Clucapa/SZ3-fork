// Shared test runners — qoi matrix and encoder roundtrip.
// Included by e2e_main.cpp only.

#ifndef SZ3_TEST_RUN_TESTS_HPP
#define SZ3_TEST_RUN_TESTS_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <vector>
#include <cmath>
#include <string>

#include "SZ3/api/sz.hpp"
#include "data_gen.hpp"
#include "test_config.hpp"
#include "verify.hpp"
#include "encoder_tests.hpp"

using namespace sz3_test;

// Data patterns used in the test matrix loop.
static const int ALL_PATTERNS[] = {D1_RAMP, D2_WIDE, D3_SINUSOID, D4_CLIFF,
                                    D5_ZEROCROSS, D6_EXP, D7_CONST, D8_RANDWALK};

// Dispatch to the correct generator by pattern and dimension.
static std::vector<double> generate_data(int pat, uint N, size_t, const std::array<size_t,3> &dims) {
    switch (N) {
    case 1: switch (pat) {
        case D1_RAMP: return gen_d1_ramp(dims[0]); case D2_WIDE: return gen_d2_wide(dims[0]);
        case D3_SINUSOID: return gen_d3_sinusoid(dims[0]); case D4_CLIFF: return gen_d4_cliff(dims[0]);
        case D5_ZEROCROSS: return gen_d5_zerocross(dims[0]); case D6_EXP: return gen_d6_exp(dims[0]);
        case D7_CONST: return gen_d7_const(dims[0]); case D8_RANDWALK: return gen_d8_randwalk(dims[0]);
    } break;
    case 2: switch (pat) {
        case D1_RAMP: return gen_d1_ramp_2d(dims[0],dims[1]); case D2_WIDE: return gen_d2_wide_2d(dims[0],dims[1]);
        case D3_SINUSOID: return gen_d3_sinusoid_2d(dims[0],dims[1]); case D4_CLIFF: return gen_d4_cliff_2d(dims[0],dims[1]);
        case D5_ZEROCROSS: return gen_d5_zerocross_2d(dims[0],dims[1]); case D6_EXP: return gen_d6_exp_2d(dims[0],dims[1]);
        case D7_CONST: return gen_d7_const_2d(dims[0],dims[1]); case D8_RANDWALK: return gen_d8_randwalk_2d(dims[0],dims[1]);
    } break;
    case 3: switch (pat) {
        case D1_RAMP: return gen_d1_ramp_3d(dims[0],dims[1],dims[2]); case D2_WIDE: return gen_d2_wide_3d(dims[0],dims[1],dims[2]);
        case D3_SINUSOID: return gen_d3_sinusoid_3d(dims[0],dims[1],dims[2]); case D4_CLIFF: return gen_d4_cliff_3d(dims[0],dims[1],dims[2]);
        case D5_ZEROCROSS: return gen_d5_zerocross_3d(dims[0],dims[1],dims[2]); case D6_EXP: return gen_d6_exp_3d(dims[0],dims[1],dims[2]);
        case D7_CONST: return gen_d7_const_3d(dims[0],dims[1],dims[2]); case D8_RANDWALK: return gen_d8_randwalk_3d(dims[0],dims[1],dims[2]);
    } break;
    }
    return {};
}

// Quick domain check: skip test if generated data violates QOI's input range.
static bool data_ok_for_qoi_domain(QoiDomain domain, const double *data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        double v = data[i];
        switch (domain) {
            case DOM_NON_NEGATIVE: if (v < 0)    return false; break;
            case DOM_POSITIVE:     if (v <= 0)   return false; break;
            case DOM_NON_ZERO:     if (v == 0.0) return false; break;
            default: break;
        }
    }
    return true;
}

// Defined in e2e_main.cpp.
extern std::string g_encoder_path;
extern bool call_encoder(const std::string &expr, int &qoi_out,
                          std::string &params_out, std::string &error_out,
                          const std::string &extra_flags = "");

// ============================================================================
//  QOI matrix test — verify one QOI × pattern × algo combination.
//  Uses hardcoded feval from QoiDef for verification.
// ============================================================================
static TestResult run_qoi_test(const QoiDef &qd, uint N, int algo, uint8_t ia,
                                int pat, std::array<size_t,3> dims) {
    using namespace SZ3;
    TestResult r; size_t num = dims[0]; if (N>=2) num*=dims[1]; if (N>=3) num*=dims[2];

    auto data = generate_data(pat, N, num, dims);
    if (qd.max_data > 0) {
        double max_val = 0;
        for (auto v : data) max_val = std::max(max_val, std::fabs(v));
        if (max_val > qd.max_data) {
            double scale = qd.max_data / max_val;
            for (auto &v : data) v *= scale;
        }
    }
    if (qd.min_abs > 0) {
        for (auto &v : data)
            if (std::fabs(v) < qd.min_abs) v = (v >= 0) ? qd.min_abs : -qd.min_abs;
    }
    if (!sz3_test::data_ok_for_qoi(qd, data)) { r.fail_reason = "skip-domain"; return r; }

    auto conf = make_config(qd, N, algo, ia, dims);

    // Use encoder to produce qoi/qoiParams.
    if (qd.expr) {
        if (g_encoder_path.empty()) { r.fail_reason = "skip-no-encoder"; return r; }
        int enc_qoi = 0; std::string enc_b64;
        std::string err;
        std::string flags;
        if (qd.is_regional) flags = "--regional";
        if (!call_encoder(qd.expr, enc_qoi, enc_b64, err, flags))
            { r.passed=false; r.fail_reason="encoder-error"; r.detail=err; return r; }
        conf.qoi = enc_qoi;
        conf.qoiParams = sz3_test::base64_decode_raw(enc_b64);
    }

    // Skip if the hardcoded feval overflows on this data.
    if (qd.feval) {
        bool overflow = false;
        for (size_t i : {size_t(0),num/4,num/2,3*num/4,num-1})
            if (i<num && !std::isfinite(qd.feval(data[i]))) { overflow=true; break; }
        if (qd.feval2) for (size_t i : {size_t(0),num/4,num/2,3*num/4,num-1})
            if (i<num && !std::isfinite(qd.feval2(data[i]))) { overflow=true; break; }
        if (overflow) { r.fail_reason = "skip-overflow"; return r; }
    }

    size_t cmpSize_unused = 0;
    double *dec = roundtrip_compress(conf, data.data(), num, cmpSize_unused, r);
    if (!dec) return r;

    if (qd.is_regional) {
        // Regional: hardcoded aggregate constraint (mean or mean-of-squares).
        double agg=0;
        if (!check_regional_aggregate(qd,num,data.data(),dec,&agg))
            { r.passed=false; r.fail_reason="qoi-regional"; r.max_abs=max_abs_err(data.data(),dec,num); r.max_qoi=agg; }
        else { r.max_abs=max_abs_err(data.data(),dec,num); r.max_qoi=agg; }
    } else {
        // Pointwise: per-point hardcoded f(x) check.
        size_t fails=0;
        if (!check_hardcoded_comply(qd.feval,qd.qEB,data.data(),dec,num,&fails))
            { r.passed=false; r.fail_reason="qoi-violation"; r.qoi_fails=fails;
              r.max_abs=max_abs_err(data.data(),dec,num); r.max_qoi=max_hardcoded_qoi(qd.feval,data.data(),dec,num); }
        else { r.max_abs=max_abs_err(data.data(),dec,num); r.max_qoi=max_hardcoded_qoi(qd.feval,data.data(),dec,num); }
        // MultiQoI second group check.
        if (r.passed && qd.feval2 && !check_hardcoded_comply(qd.feval2,qd.qEB2,data.data(),dec,num,&fails))
            { r.passed=false; r.fail_reason="qoi-violation"; r.qoi_fails=fails; r.max_qoi2=max_hardcoded_qoi(qd.feval2,data.data(),dec,num); }
    }

    delete[] dec; return r;
}

// ============================================================================
//  Encoder roundtrip test — expression → external encoder → compress → verify.
//  Uses hardcoded feval from EncoderTestCase for verification.
// ============================================================================
static TestResult run_encoder_test(const EncoderTestCase &tc, uint N, int algo,
                                    uint8_t ia, int pat, std::array<size_t,3> dims) {
    using namespace SZ3;
    TestResult r; size_t num = dims[0]; if (N>=2) num*=dims[1]; if (N>=3) num*=dims[2];

    auto data = generate_data(pat, N, num, dims);
    if (tc.max_data > 0) {
        double max_val = 0;
        for (auto v : data) max_val = std::max(max_val, std::fabs(v));
        if (max_val > tc.max_data) {
            double scale = tc.max_data / max_val;
            for (auto &v : data) v *= scale;
        }
    }
    if (tc.min_abs > 0) {
        for (auto &v : data)
            if (std::fabs(v) < tc.min_abs) v = (v >= 0) ? tc.min_abs : -tc.min_abs;
    }
    if (!data_ok_for_qoi_domain(tc.domain, data.data(), num)) { r.fail_reason = "skip-domain"; return r; }

    // Call external encoder binary to get qoi + qoiParams.
    int qoi_val = 0; std::string qoi_params_b64;
    if (g_encoder_path.empty()) { r.fail_reason = "skip-no-encoder"; return r; }
    std::string err;
    if (!call_encoder(tc.expr, qoi_val, qoi_params_b64, err))
        { r.passed=false; r.fail_reason="encoder-error"; r.detail=err; return r; }

    // Build Config from encoder output and test case parameters.
    Config conf(num); conf.setDims(dims.begin(), dims.begin()+N);
    conf.qoi = qoi_val;     conf.qoiParams = sz3_test::base64_decode_raw(qoi_params_b64);
    conf.qEB = tc.qEB; conf.absErrorBound = tc.absErrorBound; conf.quantbinCnt = 65536; conf.qR = 32;
    switch (algo) {
        case TALGO_BLOCK: conf.cmprAlgo=ALGO_LORENZO_REG; conf.lorenzo=true; conf.lorenzo2=false; conf.regression=false; break;
        case TALGO_INTERP: conf.cmprAlgo=ALGO_INTERP; break;
        case TALGO_INTERP_LORENZO: conf.cmprAlgo=ALGO_INTERP_LORENZO; break;
    }
    if (algo==TALGO_INTERP||algo==TALGO_INTERP_LORENZO) conf.interpAlgo=ia;

    // Skip if the hardcoded feval overflows on this data.
    bool overflow=false;
    for (size_t i : {size_t(0),num/4,num/2,3*num/4,num-1})
        if (i<num && !std::isfinite(tc.feval(data[i]))) { overflow=true; break; }
    if (overflow) { r.fail_reason="skip-overflow"; return r; }

    size_t cmpSize_unused = 0;
    double *dec = roundtrip_compress(conf, data.data(), num, cmpSize_unused, r);
    if (!dec) return r;

    // Per-point hardcoded f(x) check.
    size_t fails=0;
    if (!check_hardcoded_comply(tc.feval,tc.qEB,data.data(),dec,num,&fails))
        { r.passed=false; r.fail_reason="qoi-violation"; r.qoi_fails=fails;
          r.max_abs=max_abs_err(data.data(),dec,num); r.max_qoi=max_hardcoded_qoi(tc.feval,data.data(),dec,num); }
    else { r.max_abs=max_abs_err(data.data(),dec,num); r.max_qoi=max_hardcoded_qoi(tc.feval,data.data(),dec,num); }
    // MultiQoI second group check.
    if (tc.feval2 && r.passed && !check_hardcoded_comply(tc.feval2,tc.qEB2,data.data(),dec,num,&fails))
        { r.passed=false; r.fail_reason="qoi-violation"; r.qoi_fails=fails; r.max_qoi2=max_hardcoded_qoi(tc.feval2,data.data(),dec,num); }

    delete[] dec; return r;
}

#endif
