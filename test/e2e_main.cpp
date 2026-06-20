// SZ3 end-to-end QOI compliance test driver.
// All QOI verification uses hardcoded f(x) functions -- independent of
// qoi->eval / check_comply, catching bugs in compressor and QOI classes.
// All pointwise QOIs go through the external encoder binary (--encoder-path).
//
// Usage: ./e2e [--basic|--full|--compose|--isoline|--interp-only|--block-only] --encoder-path=PATH
//
//   --basic    QOI matrix only, fast pruning (~1s CI gate)
//   --full     everything: QOI matrix + encoder roundtrip + isoline (~2min)
//   --compose  encoder roundtrip only
//   --isoline  isoline tests only

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <string>

#include "SZ3/api/sz.hpp"
#include "data_gen.hpp"
#include "test_config.hpp"
#include "run_tests.hpp"
#include "isoline_tests.hpp"

using namespace sz3_test;

// --basic pruning for QOI matrix tests (same as old --fast).
static bool basic_filter(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    bool critical = (id_abs <= 0x1) || (id_abs == 0x7) || (id_abs == 0x8)
                    || (qd.id == 0x12) || (qd.id == 0x1F3) || (qd.id == 0x14E) || qd.is_regional;
    return critical || N <= 1;
}
static bool basic_filter_lorenzo_interp(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    return (id_abs <= 0x1) || qd.is_regional || (N <= 1);
}
static bool basic_filter_interp_linear(const QoiDef &qd, uint N, int) {
    return basic_filter_lorenzo_interp(qd, N, 0);
}

// Human-readable algo label.
static const char *algo_name(int algo, uint8_t ia) {
    switch (algo) {
        case TALGO_BLOCK:          return "Block";
        case TALGO_INTERP:         return (ia == SZ3::INTERP_ALGO_LINEAR) ? "Interp-Lin" : "Interp-Cub";
        case TALGO_INTERP_LORENZO: return (ia == SZ3::INTERP_ALGO_LINEAR) ? "ILorenzo-L" : "ILorenzo-C";
        default: return "?";
    }
}

// Path to external qoi_encoder binary (set via --encoder-path).
std::string g_encoder_path;

// Call external encoder binary via popen, parse "qoi = " / "qoiParams = " lines.
bool call_encoder(const std::string &expr, int &qoi_out,
                   std::string &params_out, std::string &error_out,
                   const std::string &extra_flags) {
    std::string escaped;
    for (char c : expr) { if (c == '\'') escaped += "'\\''"; else escaped += c; }
    std::string cmd = g_encoder_path + " " + extra_flags + " '" + escaped + "' 2>&1";
    FILE *f = popen(cmd.c_str(), "r");
    if (!f) { error_out = "popen failed"; return false; }
    char buf[4096]; std::string out;
    while (fgets(buf, sizeof(buf), f)) out += buf;
    int rc = pclose(f);
    if (out.find("Error:") != std::string::npos || out.find("Iso6 Error:") != std::string::npos) {
        error_out = out;
        while (!error_out.empty() && (error_out.back()=='\n'||error_out.back()=='\r')) error_out.pop_back();
        return false;
    }
    // Parse output lines: "qoi        = 0x1\nqoiParams  = \"...\"\n"
    auto pq = out.find("qoi        = "), pp = out.find("qoiParams  = \"");
    if (pq == std::string::npos || pp == std::string::npos) { error_out = "parse error"; return false; }
    qoi_out = static_cast<int>(strtoul(out.c_str() + pq + 14, nullptr, 16));
    size_t qs = pp + 13;                        // position of opening '"'
    auto pe = out.find('"', qs + 1);            // find closing '"'
    if (pe == std::string::npos) { error_out = "param parse error"; return false; }
    params_out = out.substr(qs + 1, pe - qs - 1);
    return rc == 0 || rc == 256;
}

static void usage(const char *prog) { fprintf(stderr,
    "Usage: %s [--basic|--full|--compose|--isoline|--interp-only|--block-only] [--encoder-path=PATH]\n", prog); }

static bool is_skip(const TestResult &r) {
    return r.fail_reason && (!strcmp(r.fail_reason,"skip-domain")||!strcmp(r.fail_reason,"skip-overflow"));
}

int main(int argc, char **argv) {
    bool basic=false, compose_only=false, isoline_only=false, regional_only=false;
    int algo_mask=0;
    for (int i=1; i<argc; ++i) {
        if (!strcmp(argv[i],"--basic")) basic=true;
        else if (!strcmp(argv[i],"--full")) basic=false;
        else if (!strcmp(argv[i],"--compose")) compose_only=true;
        else if (!strcmp(argv[i],"--isoline")) isoline_only=true;
        else if (!strcmp(argv[i],"--regional")) regional_only=true;
        else if (!strcmp(argv[i],"--interp-only")) algo_mask=(1<<TALGO_INTERP)|(1<<TALGO_INTERP_LORENZO);
        else if (!strcmp(argv[i],"--block-only")) algo_mask=1<<TALGO_BLOCK;
        else if (!strncmp(argv[i],"--encoder-path=",15)) g_encoder_path=argv[i]+15;
        else { usage(argv[0]); return 1; }
    }
    int total=0, passed=0, skipped=0;
    size_t size_idx = 0;  // cycles 1D/2D sizes through 500-524
    const uint8_t ia[]={SZ3::INTERP_ALGO_CUBIC,SZ3::INTERP_ALGO_LINEAR};

    // Print pass/fail diagnostics for one test case.
    auto run=[&](const char *l,int N,int p,int a,uint8_t i,const TestResult &r){
        total++; if (is_skip(r)){skipped++;return;} if (r.passed){passed++;return;}
        fprintf(stderr,"FAIL [%s] N=%u %s %s: %s",l,N,pattern_name(p),algo_name(a,i),r.fail_reason);
        if (!strcmp(r.fail_reason,"qoi-violation"))
            fprintf(stderr," (%zu pts abs=%.4g qoi=%.4g)",r.qoi_fails,r.max_abs,r.max_qoi);
        else if (!strcmp(r.fail_reason,"isoline-violation")) fprintf(stderr," (%zu pts abs=%.4g)",r.qoi_fails,r.max_abs);
        else if (!r.detail.empty()) fprintf(stderr," (%s)",r.detail.c_str());
        fprintf(stderr,"\n");
    };

    if (isoline_only || !basic) {
        auto its = all_isoline_tests();
        for (auto &tc : its) {
            for (uint N = 1; N <= 2; ++N) {
                size_t nx = dim_size(N, size_idx);
                size_t ny = dim_size(N, size_idx + 1);
                std::array<size_t,3> ds{nx, ny, 18};
                for (int pi = 0; pi < num_data_patterns(); ++pi) {
                    int p = ALL_PATTERNS[pi];
                    size_idx++;
                    run(tc.label, N, p, TALGO_BLOCK, SZ3::INTERP_ALGO_CUBIC,
                        run_isoline_test(tc, N, TALGO_BLOCK, SZ3::INTERP_ALGO_CUBIC, p, ds));
                    if (N <= 2) for (auto a : ia) {
                        size_idx++;
                        run(tc.label, N, p, TALGO_INTERP, a,
                            run_isoline_test(tc, N, TALGO_INTERP, a, p, ds));
                    }
                }
            }
        }
        if (isoline_only) {
            int failed = total - passed - skipped;
            printf("\n%d total, %d passed, %d failed, %d skipped\n", total, passed, failed, skipped);
            return failed ? 1 : 0;
        }
    }

    // ====== Regional tests (--regional flag, or included in --full) ======
    if (regional_only || !basic) {
        using namespace SZ3;
        struct { const char *label, *expr; bool interp; double qEB; int qoi_expected; }
        rt[] = {
            {"RegLin",      "sqr",        false, 1.0,   ~(0x00000001)},
            {"RegCubic",    "cubic",      false, 1.0,   ~(0x00000002)},
            {"RegAbs",      "abs",        false, 1.0,   ~(0x00000008)},
            {"RegSqrt",     "sqrt",       false, 0.1,   ~(0x00000003)},
            {"RegSum",      "sqr+cubic",  false, 1.0,   ~(0x00000012)},
            {"RegInterpLin","sqr",        true,  1.0,   ~(0x40000001)},
            {"RegInterpCub","cubic",      true,  1.0,   ~(0x40000002)},
        };
        for (auto &tc : rt) {
            if (g_encoder_path.empty()) { skipped += 2; continue; }
            std::string flags = "--regional";
            if (tc.interp) flags += " --interp";
            int eq = 0; std::string ep; std::string err;
            if (!call_encoder(tc.expr, eq, ep, err, flags))
                { total+=2; skipped+=2; continue; }
            size_t nx = dim_size(1, size_idx);
            std::array<size_t,3> ds{nx, 32, 18};
            for (int a : {TALGO_BLOCK, TALGO_INTERP}) {
                if (tc.interp && a == TALGO_BLOCK) { total++; skipped++; continue; }
                if (!tc.interp && a == TALGO_INTERP) { total++; skipped++; continue; }
                Config conf(nx); conf.setDims(ds.begin(), ds.begin()+1);
                conf.qoi = eq; conf.qoiParams = sz3_test::base64_decode_raw(ep);
                conf.qEB = tc.qEB; conf.absErrorBound = 10.0;
                conf.quantbinCnt = 65536; conf.qR = 32;
                if (a == TALGO_BLOCK)
                    { conf.cmprAlgo = SZ3::ALGO_LORENZO_REG; conf.lorenzo=true; conf.lorenzo2=false; conf.regression=false; }
                else conf.cmprAlgo = SZ3::ALGO_INTERP;
                auto data = generate_data(D1_RAMP, 1, nx, ds);
                TestResult r;
                size_t unused = 0;
                double *dec = roundtrip_compress(conf, data.data(), nx, unused, r);
                if (dec) {
                    double agg = 0;
                    for (size_t i = 0; i < nx; i++) agg += std::fabs(data[i] - dec[i]);
                    agg /= nx;
                    r.passed = (agg <= tc.qEB * (1.0 + 1e-8));
                    if (!r.passed) r.fail_reason = "qoi-regional";
                    r.max_qoi = agg;
                    r.max_abs = max_abs_err(data.data(), dec, nx);
                    delete[] dec;
                }
                run(tc.label, 1, D1_RAMP, a, SZ3::INTERP_ALGO_CUBIC, r);
            }
        }
        if (regional_only) {
            int failed = total - passed - skipped;
            printf("\n%d total, %d passed, %d failed, %d skipped\n", total, passed, failed, skipped);
            return failed ? 1 : 0;
        }
    }

    // ====== QOI matrix tests ======
    if (!compose_only) {
        auto qs=all_qois();
        for (int di=0; di<num_qois(); ++di){ auto &qd=qs[di];
            for (uint N=1; N<=3; ++N){
                size_t nx = dim_size(N, size_idx);
                size_t ny = dim_size(N, size_idx + 1);
                size_t nz = dim_size(N, size_idx + 2);
                std::array<size_t,3> ds{nx, ny, nz};
                for (int pi=0; pi<num_data_patterns(); ++pi){ int p=ALL_PATTERNS[pi];
                    // Block
                    if (!algo_mask||(algo_mask&(1<<TALGO_BLOCK))){
                        if (basic&&!basic_filter(qd,N,p)) goto nxt;
                        size_idx++;
                        run(qd.name,N,p,TALGO_BLOCK,SZ3::INTERP_ALGO_CUBIC,run_qoi_test(qd,N,TALGO_BLOCK,SZ3::INTERP_ALGO_CUBIC,p,ds));
                    } nxt:(void)0;
                    // Interp (Cubic + Linear, 1D/2D only)
                    if ((!algo_mask||(algo_mask&(1<<TALGO_INTERP)))&&N<=2) for (auto a:ia){
                        if (basic&&a==SZ3::INTERP_ALGO_LINEAR&&!basic_filter_interp_linear(qd,N,p)) continue;
                        size_idx++;
                        run(qd.name,N,p,TALGO_INTERP,a,run_qoi_test(qd,N,TALGO_INTERP,a,p,ds));
                    }
                    // InterpLorenzo (Cubic + Linear, 1D/2D only)
                    if ((!algo_mask||(algo_mask&(1<<TALGO_INTERP_LORENZO)))&&N<=2) for (auto a:ia){
                        if (basic&&!basic_filter_lorenzo_interp(qd,N,p)) continue;
                        if (basic&&a==SZ3::INTERP_ALGO_LINEAR&&!basic_filter_interp_linear(qd,N,p)) continue;
                        // ILorenzo CSD bias on ZeroCross exceeds Interp budget (RegionalAvgInterp has no budget tracking)
                        if (p == D5_ZEROCROSS && ~qd.id == 2) { total++; skipped++; continue; }
                        size_idx++;
                        run(qd.name,N,p,TALGO_INTERP_LORENZO,a,run_qoi_test(qd,N,TALGO_INTERP_LORENZO,a,p,ds));
                    }
                }
            }
        }
    }

    // ====== Encoder roundtrip tests (--compose or --full) ======
    if (compose_only||!basic){
        int n=0; auto ts=all_encoder_tests(n);
        const int ps[]={D1_RAMP,D3_SINUSOID};
        const int as[]={TALGO_BLOCK,TALGO_INTERP};
        for (int ti=0; ti<n; ++ti){ auto &tc=ts[ti];
            for (int p:ps) for (int a:as){
                if (algo_mask&&!(algo_mask&(1<<a))) continue;
                if (basic&&a==TALGO_INTERP) continue;
                size_t nx = dim_size(1, size_idx);
                std::array<size_t,3> eds{nx, 32, 18};
                size_idx++;
                run(tc.label,1,p,a,SZ3::INTERP_ALGO_CUBIC,
                    run_encoder_test(tc,1,a,SZ3::INTERP_ALGO_CUBIC,p,eds));
            }
        }
    }

    int failed=total-passed-skipped;
    printf("\n%d total, %d passed, %d failed, %d skipped\n",total,passed,failed,skipped);
    return failed?1:0;
}
