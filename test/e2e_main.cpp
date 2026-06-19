// SZ3 end-to-end QOI compliance test driver.
//
// Runs a parameterized test matrix:
//   19 QOIs × 8 data patterns × 3 dimensions × (Block + Interp-Lin + Interp-Cub +
//   ILorenzo-L + ILorenzo-C)
//
// Each case: generate data → compress → decompress → verify QOI constraint.
// Pointwise QOIs are checked per-point via check_comply; regional QOIs are
// checked via aggregate constraint (mean / mean-of-squares).
//
// Usage: ./e2e [--fast] [--interp-only] [--block-only] [--verbose]
//   --fast      pruned suite for CI gate
//   (no flag)   full suite

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <array>
#include <string>
#include <vector>
#include <limits>

#include "SZ3/api/sz.hpp"
#include "SZ3/qoi/QoIIf.hpp"
#include "data_gen.hpp"
#include "test_config.hpp"

using namespace sz3_test;

static const int ALL_PATTERNS[] = {D1_RAMP, D2_WIDE, D3_SINUSOID, D4_CLIFF,
                                    D5_ZEROCROSS, D6_EXP, D7_CONST, D8_RANDWALK};

// --fast pruning: keep only critical QOI × dimension combinations.
static bool fast_filter(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    bool critical = (id_abs <= 0x1) || (id_abs == 0x7) || (id_abs == 0x8)
                    || (qd.id == 0x12) || (qd.id == 0x1F3) || (qd.id == 0x14E)
                    || qd.is_regional;
    if (critical) return true;
    if (N > 1) return false;
    return true;
}

// --fast: INTERP_LORENZO only for critical QOIs (avoids expensive tuning).
static bool fast_filter_lorenzo_interp(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    bool critical = (id_abs <= 0x1) || qd.is_regional;
    if (critical) return true;
    if (N > 1) return false;
    return true;
}

// --fast: LINEAR interp only for critical QOIs.
static bool fast_filter_interp_linear(const QoiDef &qd, uint N, int) {
    return fast_filter_lorenzo_interp(qd, N, 0);
}

// Dispatch to the correct generator by pattern, dimension, and size.
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

static double max_abs_err(const std::vector<double> &a, const std::vector<double> &b) {
    double me = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        double e = std::fabs(a[i] - b[i]);
        if (e > me) me = e;
    }
    return me;
}

// Skip test if QOI eval overflows on the generated data (e.g., e^(x²) at large x).
static bool qoi_eval_ok(SZ3::Config &conf, const std::vector<double> &data) {
    auto qoi = SZ3::GetQOI<double, 1>(conf);
    if (!qoi) return false;
    size_t n = data.size();
    for (size_t i : {0ul, n/4, n/2, 3*n/4, n-1}) {
        if (i >= n) continue;
        double v = qoi->eval(data[i]);
        if (!std::isfinite(v)) return false;
    }
    return true;
}

// Per-point QOI compliance check (for pointwise QOIs only).
static bool check_qoi_comply_all(SZ3::Config &conf,
                                  const std::vector<double> &orig,
                                  const std::vector<double> &dec,
                                  size_t *fail_count,
                                  size_t *first_fail) {
    auto qoi = SZ3::GetQOI<double, 1>(conf);
    if (!qoi) { *fail_count = 0; return true; }
    size_t fc = 0, ff = 0;
    bool found = false;
    for (size_t i = 0; i < orig.size(); ++i) {
        if (!qoi->check_comply(orig[i], dec[i])) {
            fc++;
            if (!found) { ff = i; found = true; }
        }
    }
    if (fail_count) *fail_count = fc;
    if (first_fail) *first_fail = ff;
    return fc == 0;
}

// Max QOI error (used for diagnostics on failure).
static double max_qoi_err(SZ3::Config &conf, const std::vector<double> &orig,
                           const std::vector<double> &dec) {
    auto qoi = SZ3::GetQOI<double, 1>(conf);
    double me = 0;
    for (size_t i = 0; i < orig.size(); ++i) {
        double e = std::fabs(qoi->eval(orig[i]) - qoi->eval(dec[i]));
        if (e > me) me = e;
    }
    return me;
}

struct TestResult {
    bool passed;
    const char *fail_reason;
    double max_abs;
    double max_qoi;
    size_t cmp_size;
    size_t qoi_fails;
    std::string detail;
};

// Run a single compress-decompress-verify round-trip.
static TestResult run_one(const QoiDef &qd, uint N, int algo, uint8_t interp_algo,
                           int pat, std::array<size_t, 3> dims) {
    using namespace SZ3;
    TestResult r = {};
    r.passed = true;

    size_t num = dims[0];
    if (N >= 2) num *= dims[1];
    if (N >= 3) num *= dims[2];

    auto data = generate_data(pat, N, num, dims);

    // Skip data that violates QOI domain (e.g., negative values for XSqrt).
    if (!data_ok_for_qoi(qd, data)) {
        r.passed = true;
        r.fail_reason = "skip-domain";
        return r;
    }

    auto conf = make_config(qd, N, algo, interp_algo, dims);

    // Skip if QOI overflows on this data.
    if (!qoi_eval_ok(conf, data)) {
        r.passed = true;
        r.fail_reason = "skip-overflow";
        return r;
    }

    // --- compress ---
    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    size_t cmpSize = 0;

    try {
        cmpSize = SZ_compress<double>(conf, data.data(),
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    } catch (const std::exception &e) {
        r.passed = false;
        r.fail_reason = "cmp-exception";
        r.detail = e.what();
        return r;
    }
    r.cmp_size = cmpSize;
    if (cmpSize == 0) { r.passed = false; r.fail_reason = "cmpSize=0"; return r; }

    // --- decompress ---
    Config conf2;
    double *dec = nullptr;
    try {
        dec = SZ_decompress<double>(conf2,
                reinterpret_cast<const char *>(cmp.data()), cmpSize);
    } catch (const std::exception &e) {
        r.passed = false;
        r.fail_reason = "dec-exception";
        r.detail = e.what();
        return r;
    }
    if (!dec)          { r.passed = false; r.fail_reason = "dec-null";     return r; }
    if (conf2.num != num) { delete[] dec; r.passed = false; r.fail_reason = "num-mismatch"; return r; }

    std::vector<double> dec_vec(dec, dec + num);
    delete[] dec;

    // --- verify QOI constraint ---
    if (qd.is_regional) {
        // Regional: check aggregate (mean or mean-of-squares) error.
        double agg_val = 0;
        if (!check_regional_aggregate(qd, num, data.data(), dec_vec.data(), &agg_val)) {
            r.passed = false;
            r.fail_reason = "qoi-regional";
            r.max_abs = max_abs_err(data, dec_vec);
            r.max_qoi = agg_val;
            return r;
        }
        r.max_abs = max_abs_err(data, dec_vec);
        r.max_qoi = agg_val;
    } else {
        // Pointwise: check per-point compliance.
        size_t qoi_fails = 0;
        if (!check_qoi_comply_all(conf, data, dec_vec, &qoi_fails, nullptr)) {
            r.passed = false;
            r.fail_reason = "qoi-violation";
            r.qoi_fails = qoi_fails;
            r.max_abs = max_abs_err(data, dec_vec);
            r.max_qoi = max_qoi_err(conf, data, dec_vec);
            return r;
        }
        r.max_abs = max_abs_err(data, dec_vec);
        r.max_qoi = max_qoi_err(conf, data, dec_vec);
    }

    // --- verify absolute error bound (global safeguard) ---
    if (conf2.cmprAlgo == ALGO_LORENZO_REG || conf2.cmprAlgo == ALGO_INTERP ||
        conf2.cmprAlgo == ALGO_INTERP_LORENZO) {
        if (r.max_abs > conf.absErrorBound * (1.0 + 1e-6)) {
            r.passed = false;
            r.fail_reason = "abs-bound";
            return r;
        }
    }

    return r;
}

// Human-readable algo label (includes interp variant).
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
        "Usage: %s [--fast] [--interp-only] [--block-only] [--verbose]\n"
        "  --fast          Pruned test suite (~1200 cases, CI gate)\n"
        "  --full          Full test suite (~1700 cases) [default]\n"
        "  --interp-only   Only ALGO_INTERP and ALGO_INTERP_LORENZO\n"
        "  --block-only    Only ALGO_LORENZO_REG\n"
        "  --verbose       Print perf stats for passed tests too\n",
        prog);
}

int main(int argc, char **argv) {
    bool fast = false;
    bool verbose = false;
    int algo_mask = 0;  // 0 = all algos

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--fast"))        fast = true;
        else if (!strcmp(argv[i], "--full"))   fast = false;
        else if (!strcmp(argv[i], "--interp-only"))
            algo_mask = (1 << TALGO_INTERP) | (1 << TALGO_INTERP_LORENZO);
        else if (!strcmp(argv[i], "--block-only"))
            algo_mask = 1 << TALGO_BLOCK;
        else if (!strcmp(argv[i], "--verbose")) verbose = true;
        else { usage(argv[0]); return 1; }
    }

    int total = 0, passed = 0, skipped = 0;
    auto qois = all_qois();

    const uint8_t interp_algos[] = { SZ3::INTERP_ALGO_CUBIC, SZ3::INTERP_ALGO_LINEAR };

    // ---- parameterized test matrix ----
    for (int di = 0; di < num_qois(); ++di) {
        auto &qd = qois[di];

        for (uint N = 1; N <= 3; ++N) {
            std::array<size_t, 3> dims;
            dims[0] = dim_size(N, 512);
            dims[1] = dim_size(N, 32);
            dims[2] = dim_size(N, 18);

            for (int pi = 0; pi < num_data_patterns(); ++pi) {
                int pat = ALL_PATTERNS[pi];

                // ----- ALGO_LORENZO_REG (Block) -----
                if (!algo_mask || (algo_mask & (1 << TALGO_BLOCK))) {
                    if (fast && !fast_filter(qd, N, pat)) goto skip_block;
                    total++;
                    auto r = run_one(qd, N, TALGO_BLOCK, SZ3::INTERP_ALGO_CUBIC, pat, dims);
                    if (r.fail_reason && (!strcmp(r.fail_reason, "skip-domain")
                                      || !strcmp(r.fail_reason, "skip-overflow"))) {
                        skipped++;
                    } else if (r.passed) {
                        passed++;
                        if (verbose) printf("PASS [%s] N=%u %s Block: cmp=%zu\n",
                                            qd.name, N, pattern_name(pat), r.cmp_size);
                    } else {
                        fprintf(stderr, "FAIL [%s] N=%u %s Block: %s\n",
                                qd.name, N, pattern_name(pat), r.fail_reason);
                    }
                }
                skip_block: (void)0;

                // ----- ALGO_INTERP (Cubic + Linear) -----
                if (!algo_mask || (algo_mask & (1 << TALGO_INTERP))) {
                    if (N > 2) goto skip_interp;  // 3D interp not tested
                    for (auto ia : interp_algos) {
                        if (fast && ia == SZ3::INTERP_ALGO_LINEAR
                            && !fast_filter_interp_linear(qd, N, pat))
                            continue;
                        total++;
                        auto r = run_one(qd, N, TALGO_INTERP, ia, pat, dims);
                        if (r.fail_reason && (!strcmp(r.fail_reason, "skip-domain")
                                          || !strcmp(r.fail_reason, "skip-overflow"))) {
                            skipped++;
                        } else if (r.passed) {
                            passed++;
                            if (verbose) printf("PASS [%s] N=%u %s %s: cmp=%zu\n",
                                                qd.name, N, pattern_name(pat),
                                                algo_name(TALGO_INTERP, ia), r.cmp_size);
                        } else {
                            fprintf(stderr, "FAIL [%s] N=%u %s %s: %s\n",
                                    qd.name, N, pattern_name(pat),
                                    algo_name(TALGO_INTERP, ia), r.fail_reason);
                        }
                    }
                }
                skip_interp: (void)0;

                // ----- ALGO_INTERP_LORENZO (Cubic + Linear, 1D/2D only) -----
                if (!algo_mask || (algo_mask & (1 << TALGO_INTERP_LORENZO))) {
                    if (N > 2) goto skip_ilo;
                    for (auto ia : interp_algos) {
                        if (fast && !fast_filter_lorenzo_interp(qd, N, pat)) continue;
                        if (fast && ia == SZ3::INTERP_ALGO_LINEAR
                            && !fast_filter_interp_linear(qd, N, pat))
                            continue;
                        total++;
                        auto r = run_one(qd, N, TALGO_INTERP_LORENZO, ia, pat, dims);
                        if (r.fail_reason && (!strcmp(r.fail_reason, "skip-domain")
                                          || !strcmp(r.fail_reason, "skip-overflow"))) {
                            skipped++;
                        } else if (r.passed) {
                            passed++;
                            if (verbose) printf("PASS [%s] N=%u %s %s: cmp=%zu\n",
                                                qd.name, N, pattern_name(pat),
                                                algo_name(TALGO_INTERP_LORENZO, ia), r.cmp_size);
                        } else {
                            fprintf(stderr, "FAIL [%s] N=%u %s %s: %s\n",
                                    qd.name, N, pattern_name(pat),
                                    algo_name(TALGO_INTERP_LORENZO, ia), r.fail_reason);
                        }
                    }
                }
                skip_ilo: (void)0;
            }
        }
    }

    int failed = total - passed - skipped;
    printf("\n%d total, %d passed, %d failed, %d skipped\n",
           total, passed, failed, skipped);
    if (verbose && skipped > 0)
        printf("Skipped: domain mismatch or QOI overflow (expected for some combos)\n");
    return failed ? 1 : 0;
}
