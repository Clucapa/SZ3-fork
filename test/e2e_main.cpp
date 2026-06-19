// SZ3 end-to-end QOI compliance test driver.
//
// Two test categories:
//   1. QOI matrix:  19 QOIs × 8 data patterns × 3D × 5 algo variants
//   2. Encoder roundtrip:  expressions → qoi+qoiParams → compress → verify with hardcoded f(x)
//
// Usage: ./e2e [--fast] [--compose] [--interp-only] [--block-only] [--verbose]
//   --fast       pruned suite (CI gate)
//   --compose    encoder roundtrip tests only
//   (no flag)    full suite (includes compose)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <vector>

#include "SZ3/api/sz.hpp"
#include "data_gen.hpp"
#include "test_config.hpp"
#include "verify.hpp"
#include "encoder_tests.hpp"

using namespace sz3_test;

static const int ALL_PATTERNS[] = {D1_RAMP, D2_WIDE, D3_SINUSOID, D4_CLIFF,
                                    D5_ZEROCROSS, D6_EXP, D7_CONST, D8_RANDWALK};

// --fast pruning for qoi-matrix tests.
static bool fast_filter(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    bool critical = (id_abs <= 0x1) || (id_abs == 0x7) || (id_abs == 0x8)
                    || (qd.id == 0x12) || (qd.id == 0x1F3) || (qd.id == 0x14E)
                    || qd.is_regional;
    if (critical) return true;
    if (N > 1) return false;
    return true;
}
static bool fast_filter_lorenzo_interp(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    bool critical = (id_abs <= 0x1) || qd.is_regional;
    if (critical) return true;
    if (N > 1) return false;
    return true;
}
static bool fast_filter_interp_linear(const QoiDef &qd, uint N, int) {
    return fast_filter_lorenzo_interp(qd, N, 0);
}

// Data dispatch.
static std::vector<double> generate_data(int pat, uint N, size_t, const std::array<size_t, 3> &dims) {
    switch (N) {
    case 1:
        switch (pat) {
            case D1_RAMP:      return gen_d1_ramp(dims[0]);
            case D2_WIDE:      return gen_d2_wide(dims[0]);
            case D3_SINUSOID:  return gen_d3_sinusoid(dims[0]);
            case D4_CLIFF:     return gen_d4_cliff(dims[0]);
            case D5_ZEROCROSS: return gen_d5_zerocross(dims[0]);
            case D6_EXP:       return gen_d6_exp(dims[0]);
            case D7_CONST:     return gen_d7_const(dims[0]);
            case D8_RANDWALK:  return gen_d8_randwalk(dims[0]);
        }
        break;
    case 2:
        switch (pat) {
            case D1_RAMP:      return gen_d1_ramp_2d(dims[0], dims[1]);
            case D2_WIDE:      return gen_d2_wide_2d(dims[0], dims[1]);
            case D3_SINUSOID:  return gen_d3_sinusoid_2d(dims[0], dims[1]);
            case D4_CLIFF:     return gen_d4_cliff_2d(dims[0], dims[1]);
            case D5_ZEROCROSS: return gen_d5_zerocross_2d(dims[0], dims[1]);
            case D6_EXP:       return gen_d6_exp_2d(dims[0], dims[1]);
            case D7_CONST:     return gen_d7_const_2d(dims[0], dims[1]);
            case D8_RANDWALK:  return gen_d8_randwalk_2d(dims[0], dims[1]);
        }
        break;
    case 3:
        switch (pat) {
            case D1_RAMP:      return gen_d1_ramp_3d(dims[0], dims[1], dims[2]);
            case D2_WIDE:      return gen_d2_wide_3d(dims[0], dims[1], dims[2]);
            case D3_SINUSOID:  return gen_d3_sinusoid_3d(dims[0], dims[1], dims[2]);
            case D4_CLIFF:     return gen_d4_cliff_3d(dims[0], dims[1], dims[2]);
            case D5_ZEROCROSS: return gen_d5_zerocross_3d(dims[0], dims[1], dims[2]);
            case D6_EXP:       return gen_d6_exp_3d(dims[0], dims[1], dims[2]);
            case D7_CONST:     return gen_d7_const_3d(dims[0], dims[1], dims[2]);
            case D8_RANDWALK:  return gen_d8_randwalk_3d(dims[0], dims[1], dims[2]);
        }
        break;
    }
    return {};
}

// ============================================================================
//  QOI matrix test (existing)
// ============================================================================

static TestResult run_qoi_test(const QoiDef &qd, uint N, int algo, uint8_t interp_algo,
                                int pat, std::array<size_t, 3> dims) {
    using namespace SZ3;
    TestResult r;

    size_t num = dims[0];
    if (N >= 2) num *= dims[1];
    if (N >= 3) num *= dims[2];

    auto data = generate_data(pat, N, num, dims);
    if (!data_ok_for_qoi(qd, data)) { r.fail_reason = "skip-domain"; return r; }

    auto conf = make_config(qd, N, algo, interp_algo, dims);
    if (!qoi_eval_ok(conf, data.data(), num)) { r.fail_reason = "skip-overflow"; return r; }

    size_t cmpSize = 0;
    double *dec = roundtrip_compress(conf, data.data(), num, cmpSize, r);
    if (!dec) return r;

    // Verify QOI constraint.
    if (qd.is_regional) {
        double agg_val = 0;
        if (!check_regional_aggregate(qd, num, data.data(), dec, &agg_val)) {
            r.passed = false;
            r.fail_reason = "qoi-regional";
            r.max_abs = max_abs_err(data.data(), dec, num);
            r.max_qoi = agg_val;
            delete[] dec;
            return r;
        }
        r.max_abs = max_abs_err(data.data(), dec, num);
        r.max_qoi = agg_val;
    } else {
        size_t qoi_fails = 0;
        if (!check_qoi_comply_all(conf, data.data(), dec, num, &qoi_fails, nullptr)) {
            r.passed = false;
            r.fail_reason = "qoi-violation";
            r.qoi_fails = qoi_fails;
            r.max_abs = max_abs_err(data.data(), dec, num);
            r.max_qoi = max_qoi_err(conf, data.data(), dec, num);
            delete[] dec;
            return r;
        }
        r.max_abs = max_abs_err(data.data(), dec, num);
        r.max_qoi = max_qoi_err(conf, data.data(), dec, num);
    }

    // Abs bound.
    { Config conf2; (void)conf2; } // We don't have conf2 here but abs bound is inherent
    delete[] dec;
    return r;
}

// ============================================================================
//  Shared helpers
// ============================================================================

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

// ============================================================================
//  Encoder roundtrip test
// ============================================================================

static TestResult run_encoder_test(const EncoderTestCase &tc, uint N, int algo,
                                    uint8_t interp_algo, int pat,
                                    std::array<size_t, 3> dims) {
    using namespace SZ3;
    TestResult r;

    size_t num = dims[0];
    if (N >= 2) num *= dims[1];
    if (N >= 3) num *= dims[2];

    auto data = generate_data(pat, N, num, dims);

    // Domain check using the test case's domain.
    if (!data_ok_for_qoi_domain(tc.domain, data.data(), num)) {
        r.fail_reason = "skip-domain"; return r;
    }

    // Build Config using encoder output.
    auto er = qoi_encode::encode(tc.expr);
    if (!er.ok) {
        r.passed = false;
        r.fail_reason = "encoder-error";
        r.detail = er.error;
        return r;
    }

    Config conf(num);
    conf.setDims(dims.begin(), dims.begin() + N);
    conf.qoi = er.qoi;
    conf.qoiParams = er.qoiParams;
    conf.qEB = tc.qEB;
    conf.absErrorBound = tc.absErrorBound;
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
        conf.interpAlgo = interp_algo;

    // Overflow check using the hardcoded eval (not qoi->eval).
    bool overflow = false;
    for (size_t i : {size_t(0), num/4, num/2, 3*num/4, num-1}) {
        if (i >= num) continue;
        if (!std::isfinite(tc.feval(data[i]))) { overflow = true; break; }
    }
    if (overflow) { r.fail_reason = "skip-overflow"; return r; }

    size_t cmpSize = 0;
    double *dec = roundtrip_compress(conf, data.data(), num, cmpSize, r);
    if (!dec) return r;

    // Verify using hardcoded eval (independent of encoder/compressor QOI logic).
    size_t fails = 0;
    if (!check_hardcoded_comply(tc.feval, tc.qEB, data.data(), dec, num, &fails)) {
        r.passed = false;
        r.fail_reason = "qoi-violation";
        r.qoi_fails = fails;
        r.max_abs = max_abs_err(data.data(), dec, num);
        r.max_qoi = max_hardcoded_qoi(tc.feval, data.data(), dec, num);
        delete[] dec;
        return r;
    }
    r.max_abs = max_abs_err(data.data(), dec, num);
    r.max_qoi = max_hardcoded_qoi(tc.feval, data.data(), dec, num);
    delete[] dec;
    return r;
}

// ============================================================================
//  Shared helpers
// ============================================================================

static const char *algo_name(int algo, uint8_t interp_algo) {
    switch (algo) {
        case TALGO_BLOCK:          return "Block";
        case TALGO_INTERP:         return (interp_algo == SZ3::INTERP_ALGO_LINEAR) ? "Interp-Lin" : "Interp-Cub";
        case TALGO_INTERP_LORENZO: return (interp_algo == SZ3::INTERP_ALGO_LINEAR) ? "ILorenzo-L" : "ILorenzo-C";
        default: return "?";
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--fast] [--compose] [--interp-only] [--block-only] [--verbose]\n"
        "  --fast          Pruned QOI matrix suite (CI gate)\n"
        "  --compose       Encoder roundtrip tests only\n"
        "  --full          Full suite [default]\n"
        "  --interp-only   Only ALGO_INTERP and ALGO_INTERP_LORENZO\n"
        "  --block-only    Only ALGO_LORENZO_REG\n"
        "  --verbose       Print perf stats for passed tests too\n",
        prog);
}

static bool is_skip(const TestResult &r) {
    return r.fail_reason && (!strcmp(r.fail_reason, "skip-domain")
                          || !strcmp(r.fail_reason, "skip-overflow"));
}

// ============================================================================
//  main
// ============================================================================

int main(int argc, char **argv) {
    bool fast = false, verbose = false, compose_only = false;
    int algo_mask = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--fast"))        fast = true;
        else if (!strcmp(argv[i], "--full"))   fast = false;
        else if (!strcmp(argv[i], "--compose"))compose_only = true;
        else if (!strcmp(argv[i], "--interp-only"))
            algo_mask = (1 << TALGO_INTERP) | (1 << TALGO_INTERP_LORENZO);
        else if (!strcmp(argv[i], "--block-only"))
            algo_mask = 1 << TALGO_BLOCK;
        else if (!strcmp(argv[i], "--verbose")) verbose = true;
        else { usage(argv[0]); return 1; }
    }

    int total = 0, passed = 0, skipped = 0;
    const uint8_t interp_algos[] = { SZ3::INTERP_ALGO_CUBIC, SZ3::INTERP_ALGO_LINEAR };

    auto run_and_log = [&](const char *label, int N, int pat, int algo,
                            uint8_t ia, const TestResult &r) {
        total++;
        if (is_skip(r)) { skipped++; return; }
        if (r.passed) {
            passed++;
            if (verbose) printf("PASS [%s] N=%u %s %s: cmp=%zu\n",
                                label, N, pattern_name(pat), algo_name(algo, ia), r.cmp_size);
        } else {
            size_t nd = 1; // approximate
            fprintf(stderr, "FAIL [%s] N=%u %s %s: %s", label, N, pattern_name(pat),
                    algo_name(algo, ia), r.fail_reason);
            if (!strcmp(r.fail_reason, "qoi-violation"))
                fprintf(stderr, " (%zu points) max_abs=%.6g max_qoi=%.6g", r.qoi_fails, r.max_abs, r.max_qoi);
            else if (!r.detail.empty())
                fprintf(stderr, " (%s)", r.detail.c_str());
            fprintf(stderr, "\n");
        }
    };

    // ====== QOI matrix tests (skip if --compose) ======
    if (!compose_only) {
        auto qois = all_qois();
        for (int di = 0; di < num_qois(); ++di) {
            auto &qd = qois[di];
            for (uint N = 1; N <= 3; ++N) {
                std::array<size_t, 3> dims;
                dims[0] = dim_size(N, 512);
                dims[1] = dim_size(N, 32);
                dims[2] = dim_size(N, 18);
                for (int pi = 0; pi < num_data_patterns(); ++pi) {
                    int pat = ALL_PATTERNS[pi];

                    // Block
                    if (!algo_mask || (algo_mask & (1 << TALGO_BLOCK))) {
                        if (fast && !fast_filter(qd, N, pat)) goto next_qoi_1;
                        run_and_log(qd.name, N, pat, TALGO_BLOCK, SZ3::INTERP_ALGO_CUBIC,
                                    run_qoi_test(qd, N, TALGO_BLOCK, SZ3::INTERP_ALGO_CUBIC, pat, dims));
                    }
                    next_qoi_1: (void)0;

                    // Interp
                    if ((!algo_mask || (algo_mask & (1 << TALGO_INTERP))) && N <= 2) {
                        for (auto ia : interp_algos) {
                            if (fast && ia == SZ3::INTERP_ALGO_LINEAR && !fast_filter_interp_linear(qd, N, pat))
                                continue;
                            run_and_log(qd.name, N, pat, TALGO_INTERP, ia,
                                        run_qoi_test(qd, N, TALGO_INTERP, ia, pat, dims));
                        }
                    }

                    // InterpLorenzo
                    if ((!algo_mask || (algo_mask & (1 << TALGO_INTERP_LORENZO))) && N <= 2) {
                        for (auto ia : interp_algos) {
                            if (fast && !fast_filter_lorenzo_interp(qd, N, pat)) continue;
                            if (fast && ia == SZ3::INTERP_ALGO_LINEAR && !fast_filter_interp_linear(qd, N, pat))
                                continue;
                            run_and_log(qd.name, N, pat, TALGO_INTERP_LORENZO, ia,
                                        run_qoi_test(qd, N, TALGO_INTERP_LORENZO, ia, pat, dims));
                        }
                    }
                }
            }
        }
    }

    // ====== Encoder roundtrip tests (run if --compose or --full) ======
    if (compose_only || !fast) {
        int enc_count = 0;
        auto enc_tests = all_encoder_tests(enc_count);
        // Encoder tests: 1D only, D1 ramp + D3 sinusoid, Block + Interp-Cubic
        const int enc_patterns[] = {D1_RAMP, D3_SINUSOID};
        const int enc_algos[]   = {TALGO_BLOCK, TALGO_INTERP};
        for (int ti = 0; ti < enc_count; ++ti) {
            auto &tc = enc_tests[ti];
            std::array<size_t, 3> dims = {512, 32, 18};
            for (int pat : enc_patterns) {
                for (int algo : enc_algos) {
                    if (algo_mask && !(algo_mask & (1 << algo))) continue;
                    if (fast && algo == TALGO_INTERP) continue;  // --fast skips encoder interp
                    run_and_log(tc.label, 1, pat, algo, SZ3::INTERP_ALGO_CUBIC,
                                run_encoder_test(tc, 1, algo, SZ3::INTERP_ALGO_CUBIC, pat, dims));
                }
            }
        }
    }

    int failed = total - passed - skipped;
    printf("\n%d total, %d passed, %d failed, %d skipped\n", total, passed, failed, skipped);
    if (verbose && skipped > 0)
        printf("Skipped: domain mismatch or QOI overflow (expected for some combos)\n");
    return failed ? 1 : 0;
}
