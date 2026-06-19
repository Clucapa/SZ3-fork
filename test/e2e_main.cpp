// SZ3 end-to-end QOI compliance test driver.
// All QOI verification uses hardcoded f(x) functions -- independent of
// qoi->eval / check_comply, catching bugs in compressor and QOI classes.
//
// Usage: ./e2e [--fast|--compose|--interp-only|--block-only] [--encoder-path=PATH]
//
// Two test categories:
//   1. QOI matrix:  19 QOIs x 8 patterns x 3 dims x 5 algo variants = 1672 cases
//   2. Encoder roundtrip: 30 expressions x 2 patterns x 2 algos = 116 cases
//      (requires external qoi_encoder binary via --encoder-path)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <string>

#include "SZ3/api/sz.hpp"
#include "data_gen.hpp"
#include "test_config.hpp"
#include "run_tests.hpp"

using namespace sz3_test;

// --fast pruning for QOI matrix tests.
static bool fast_filter(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    bool critical = (id_abs <= 0x1) || (id_abs == 0x7) || (id_abs == 0x8)
                    || (qd.id == 0x12) || (qd.id == 0x1F3) || (qd.id == 0x14E) || qd.is_regional;
    return critical || N <= 1;
}
static bool fast_filter_lorenzo_interp(const QoiDef &qd, uint N, int) {
    int id_abs = (qd.id < 0) ? ~qd.id : qd.id;
    return (id_abs <= 0x1) || qd.is_regional || (N <= 1);
}
static bool fast_filter_interp_linear(const QoiDef &qd, uint N, int) {
    return fast_filter_lorenzo_interp(qd, N, 0);
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
                   std::string &params_out, std::string &error_out) {
    // Shell-escape single quotes in the expression.
    std::string escaped;
    for (char c : expr) { if (c == '\'') escaped += "'\\''"; else escaped += c; }
    std::string cmd = g_encoder_path + " '" + escaped + "' 2>&1";
    FILE *f = popen(cmd.c_str(), "r");
    if (!f) { error_out = "popen failed"; return false; }
    char buf[4096]; std::string out;
    while (fgets(buf, sizeof(buf), f)) out += buf;
    int rc = pclose(f);
    if (out.find("Error:") != std::string::npos || out.find("FX Error:") != std::string::npos) {
        error_out = out;
        while (!error_out.empty() && (error_out.back()=='\n'||error_out.back()=='\r')) error_out.pop_back();
        return false;
    }
    // Parse output lines: "qoi        = 0x1\nqoiParams  = \"...\"\n"
    auto pq = out.find("qoi        = "), pp = out.find("qoiParams  = \"");
    if (pq == std::string::npos || pp == std::string::npos) { error_out = "parse error"; return false; }
    qoi_out = (int)strtoul(out.c_str() + pq + 14, nullptr, 16);
    size_t qs = pp + 13;                        // position of opening '"'
    auto pe = out.find('"', qs + 1);            // find closing '"'
    if (pe == std::string::npos) { error_out = "param parse error"; return false; }
    params_out = out.substr(qs + 1, pe - qs - 1);
    return rc == 0 || rc == 256;
}

static void usage(const char *prog) { fprintf(stderr,
    "Usage: %s [--fast|--compose|--interp-only|--block-only] [--encoder-path=PATH]\n", prog); }

static bool is_skip(const TestResult &r) {
    return r.fail_reason && (!strcmp(r.fail_reason,"skip-domain")||!strcmp(r.fail_reason,"skip-overflow"));
}

int main(int argc, char **argv) {
    bool fast=false, compose_only=false; int algo_mask=0;
    for (int i=1; i<argc; ++i) {
        if (!strcmp(argv[i],"--fast")) fast=true;
        else if (!strcmp(argv[i],"--full")) fast=false;
        else if (!strcmp(argv[i],"--compose")) compose_only=true;
        else if (!strcmp(argv[i],"--interp-only")) algo_mask=(1<<TALGO_INTERP)|(1<<TALGO_INTERP_LORENZO);
        else if (!strcmp(argv[i],"--block-only")) algo_mask=1<<TALGO_BLOCK;
        else if (!strncmp(argv[i],"--encoder-path=",15)) g_encoder_path=argv[i]+15;
        else { usage(argv[0]); return 1; }
    }
    int total=0, passed=0, skipped=0;
    const uint8_t ia[]={SZ3::INTERP_ALGO_CUBIC,SZ3::INTERP_ALGO_LINEAR};

    // Print pass/fail diagnostics for one test case.
    auto run=[&](const char *l,int N,int p,int a,uint8_t i,const TestResult &r){
        total++; if (is_skip(r)){skipped++;return;} if (r.passed){passed++;return;}
        fprintf(stderr,"FAIL [%s] N=%u %s %s: %s",l,N,pattern_name(p),algo_name(a,i),r.fail_reason);
        if (!strcmp(r.fail_reason,"qoi-violation"))
            fprintf(stderr," (%zu pts abs=%.4g qoi=%.4g)",r.qoi_fails,r.max_abs,r.max_qoi);
        else if (!r.detail.empty()) fprintf(stderr," (%s)",r.detail.c_str());
        fprintf(stderr,"\n");
    };

    // ====== QOI matrix tests ======
    if (!compose_only) {
        auto qs=all_qois();
        for (int di=0; di<num_qois(); ++di){ auto &qd=qs[di];
            for (uint N=1; N<=3; ++N){ std::array<size_t,3> ds{dim_size(N,512),dim_size(N,32),dim_size(N,18)};
                for (int pi=0; pi<num_data_patterns(); ++pi){ int p=ALL_PATTERNS[pi];
                    // Block
                    if (!algo_mask||(algo_mask&(1<<TALGO_BLOCK))){
                        if (fast&&!fast_filter(qd,N,p)) goto nxt;
                        run(qd.name,N,p,TALGO_BLOCK,SZ3::INTERP_ALGO_CUBIC,run_qoi_test(qd,N,TALGO_BLOCK,SZ3::INTERP_ALGO_CUBIC,p,ds));
                    } nxt:(void)0;
                    // Interp (Cubic + Linear, 1D/2D only)
                    if ((!algo_mask||(algo_mask&(1<<TALGO_INTERP)))&&N<=2) for (auto a:ia){
                        if (fast&&a==SZ3::INTERP_ALGO_LINEAR&&!fast_filter_interp_linear(qd,N,p)) continue;
                        run(qd.name,N,p,TALGO_INTERP,a,run_qoi_test(qd,N,TALGO_INTERP,a,p,ds));
                    }
                    // InterpLorenzo (Cubic + Linear, 1D/2D only)
                    if ((!algo_mask||(algo_mask&(1<<TALGO_INTERP_LORENZO)))&&N<=2) for (auto a:ia){
                        if (fast&&!fast_filter_lorenzo_interp(qd,N,p)) continue;
                        if (fast&&a==SZ3::INTERP_ALGO_LINEAR&&!fast_filter_interp_linear(qd,N,p)) continue;
                        run(qd.name,N,p,TALGO_INTERP_LORENZO,a,run_qoi_test(qd,N,TALGO_INTERP_LORENZO,a,p,ds));
                    }
                }
            }
        }
    }

    // ====== Encoder roundtrip tests (--compose or --full) ======
    if (compose_only||!fast){
        int n=0; auto ts=all_encoder_tests(n);
        const int ps[]={D1_RAMP,D3_SINUSOID};
        const int as[]={TALGO_BLOCK,TALGO_INTERP};
        std::array<size_t,3> ds={512,32,18};
        for (int ti=0; ti<n; ++ti){ auto &tc=ts[ti];
            for (int p:ps) for (int a:as){
                if (algo_mask&&!(algo_mask&(1<<a))) continue;
                if (fast&&a==TALGO_INTERP) continue;
                run(tc.label,1,p,a,SZ3::INTERP_ALGO_CUBIC,
                    run_encoder_test(tc,1,a,SZ3::INTERP_ALGO_CUBIC,p,ds));
            }
        }
    }

    int failed=total-passed-skipped;
    printf("\n%d total, %d passed, %d failed, %d skipped\n",total,passed,failed,skipped);
    return failed?1:0;
}
