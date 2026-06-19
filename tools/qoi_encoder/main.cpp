// qoi_encoder CLI — thin wrapper around encode.hpp (nibble) and fx_encode.hpp (FX).
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
            "  nibble mode: lin sqr cubic sqrt exp xlogx log recip abs sin tanh pow\n"
            "               Operators: + SumQoI, @ Compose, | MultiQoI\n"
#ifdef QOI_ENCODER_HAS_FX
            "  FX mode:     fx(\"sin(x)+x^2\") — arbitrary math expression\n"
#endif
            , argv[0]);
        return 1;
    }

    std::string expr(argv[1]);

    // Detect FX mode: fx("...")
#ifdef QOI_ENCODER_HAS_FX
    if (expr.size() > 4 && expr.substr(0, 3) == "fx(" && expr.back() == ')') {
        std::string inner = expr.substr(3, expr.size() - 4);
        // Strip quotes if present
        if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"')
            inner = inner.substr(1, inner.size() - 2);

        auto r = qoi_encode::fx_encode(inner);
        if (!r.ok) {
            fprintf(stderr, "FX Error: %s\n", r.error.c_str());
            return 1;
        }
        printf("qoi        = 0x%X\n", r.qoi);
        printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
        return 0;
    }
#endif

    // Nibble mode
    auto r = qoi_encode::encode(expr);
    if (!r.ok) {
        fprintf(stderr, "Error: %s\n", r.error.c_str());
        return 1;
    }
    printf("qoi        = 0x%X\n", r.qoi);
    printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
    return 0;
}
