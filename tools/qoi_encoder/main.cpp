// qoi_encoder CLI -- unified: tries nibble first, falls back to FX (SymEngine).
#include <cstdio>
#include <cstring>
#include <string>

#include "encode.hpp"

#ifdef QOI_ENCODER_HAS_FX
#include "fx_encode.hpp"
#endif

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,
            "Usage: %s \"expression\"\n"
            "  nibble: lin sqr cubic sqrt exp xlogx log recip abs sin tanh pow\n"
            "          Operators: + SumQoI, @ Compose, | MultiQoI\n"
            "  isoline: iso6(nibble_expr, min, max, count, meb [; ...])\n"
#ifdef QOI_ENCODER_HAS_FX
            "  fallback: arbitrary math expressions (sin(x)+x^2, etc.) via SymEngine\n"
#endif
            , argv[0]);
        return 1;
    }

    std::string expr(argv[1]);

    // Isoline mode: iso6(...) -- special syntax with isoline config params
    if (expr.size() > 6 && expr.substr(0, 4) == "iso6" && expr.back() == ')') {
        auto r = qoi_encode::iso6_encode(expr);
        if (!r.ok) {
            fprintf(stderr, "Iso6 Error: %s\n", r.error.c_str());
            return 1;
        }
        printf("qoi        = 0x%X\n", r.qoi);
        printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
        return 0;
    }

    // Try nibble encoding first
    auto r = qoi_encode::encode(expr);
    if (r.ok) {
        printf("qoi        = 0x%X\n", r.qoi);
        printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
        return 0;
    }

#ifdef QOI_ENCODER_HAS_FX
    // Fallback: SymEngine arbitrary function
    auto fr = qoi_encode::fx_encode(expr);
    if (fr.ok) {
        printf("qoi        = 0x%X\n", fr.qoi);
        printf("qoiParams  = \"%s\"\n", fr.qoiParams.c_str());
        return 0;
    }
    fprintf(stderr, "Error: %s\n", fr.error.c_str());
#else
    fprintf(stderr, "Error: %s\n", r.error.c_str());
#endif
    return 1;
}
