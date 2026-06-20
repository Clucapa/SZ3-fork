// qoi_encoder CLI -- unified: tries nibble first, falls back to FX (SymEngine).
#include <cstdio>
#include <cstring>
#include <string>

#include "encode.hpp"

#ifdef QOI_ENCODER_HAS_FX
#include "fx_encode.hpp"
#endif

int main(int argc, char **argv) {
    bool is_regional = false;
    const char *expr = nullptr;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--regional")) is_regional = true;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            return 1;
        } else {
            expr = argv[i];
        }
    }

    if (!expr) {
        fprintf(stderr,
            "Usage: %s [--regional] \"expression\"\n"
            "  nibble:    lin sqr cubic sqrt exp xlogx log recip abs sin tanh pow\n"
            "             Operators: + SumQoI, @ Compose, | MultiQoI\n"
            "  isoline:   iso6(nibble_expr, min, max, count, meb [; ...])\n"
#ifdef QOI_ENCODER_HAS_FX
            "  fallback:  arbitrary math expressions (sin(x)+x^2, etc.) via SymEngine\n"
#endif
            "  --regional  wrap output as Regional\n"
            , argv[0]);
        return 1;
    }

    std::string expr_str(expr);

    // Isoline mode
    if (expr_str.size() > 6 && expr_str.substr(0, 4) == "iso6" && expr_str.back() == ')') {
        if (is_regional) {
            fprintf(stderr, "Error: --regional cannot be combined with iso6\n");
            return 1;
        }
        auto r = qoi_encode::iso6_encode(expr_str);
        if (!r.ok) { fprintf(stderr, "Iso6 Error: %s\n", r.error.c_str()); return 1; }
        printf("qoi        = 0x%X\n", r.qoi);
        printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
        return 0;
    }

    // Try nibble encoding
    auto r = qoi_encode::encode(expr_str);
    bool is_fx = false;

    if (!r.ok) {
#ifdef QOI_ENCODER_HAS_FX
        r = qoi_encode::fx_encode(expr_str);
        is_fx = true;
        if (!r.ok) {
            fprintf(stderr, "Error: %s\n", r.error.c_str());
            return 1;
        }
#else
        fprintf(stderr, "Error: %s\n", r.error.c_str());
        return 1;
#endif
    }

    if (is_regional) {
        r = qoi_encode::make_regional(r, is_fx);
        if (!r.ok) { fprintf(stderr, "Error: %s\n", r.error.c_str()); return 1; }
    }

    printf("qoi        = 0x%X\n", r.qoi);
    printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
    return 0;
}
