// qoi_encoder CLI — thin wrapper around encode.hpp
#include <cstdio>
#include "encode.hpp"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,
            "Usage: %s \"expression\"\n"
            "  Operators: + SumQoI, @ Compose, | MultiQoI\n"
            "  Functions: lin sqr cubic sqrt exp xlogx log recip abs sin tanh pow\n",
            argv[0]);
        return 1;
    }
    auto r = qoi_encode::encode(argv[1]);
    if (!r.ok) {
        fprintf(stderr, "Error: %s\n", r.error.c_str());
        return 1;
    }
    printf("qoi        = 0x%X\n", r.qoi);
    printf("qoiParams  = \"%s\"\n", r.qoiParams.c_str());
    return 0;
}
