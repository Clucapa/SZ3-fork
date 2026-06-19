#ifndef SZ3_TEST_DATA_GEN_HPP
#define SZ3_TEST_DATA_GEN_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace sz3_test {

enum DataPattern { D1_RAMP = 1, D2_WIDE, D3_SINUSOID, D4_CLIFF, D5_ZEROCROSS, D6_EXP, D7_CONST, D8_RANDWALK };

inline const char *pattern_name(int p) {
    switch (p) {
        case D1_RAMP:      return "Ramp";
        case D2_WIDE:      return "WideRange";
        case D3_SINUSOID:  return "Sinusoid";
        case D4_CLIFF:     return "Cliff";
        case D5_ZEROCROSS: return "ZeroCross";
        case D6_EXP:       return "Exponential";
        case D7_CONST:     return "Constant";
        case D8_RANDWALK:  return "RandomWalk";
        default: return "?";
    }
}

// ---------- 1D generators ----------

inline std::vector<double> gen_d1_ramp(size_t n) {
    std::vector<double> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<double>(i) / (n - 1) * 100.0;
    return d;
}

inline std::vector<double> gen_d2_wide(size_t n) {
    std::vector<double> d(n);
    double lo = -3.0, hi = 4.0;
    for (size_t i = 0; i < n; ++i) {
        double t = lo + (hi - lo) * i / (n - 1);
        d[i] = std::pow(10.0, t);  // 0.001 ~ 10000
    }
    return d;
}

inline std::vector<double> gen_d3_sinusoid(size_t n, unsigned seed = 42) {
    std::vector<double> d(n);
    std::mt19937 rng(seed);
    std::normal_distribution<> noise(0.0, 0.5);
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / n;
        d[i] = 50.0 + 30.0 * std::sin(2.0 * M_PI * 3.0 * t)
               + 8.0 * std::sin(2.0 * M_PI * 11.0 * t + 1.3)
               + noise(rng);
    }
    return d;
}

inline std::vector<double> gen_d4_cliff(size_t n) {
    std::vector<double> d(n);
    size_t half = n / 2;
    for (size_t i = 0; i < half; ++i) d[i] = 0.1;
    size_t trans_start = half - 2;
    size_t trans_end   = half + 2;
    if (trans_end > n) trans_end = n;
    for (size_t i = trans_start; i < trans_end && i < n; ++i) {
        double frac = static_cast<double>(i - trans_start) / (trans_end - trans_start);
        d[i] = 0.1 + (1000.0 - 0.1) * frac;
    }
    for (size_t i = trans_end; i < n; ++i) d[i] = 1000.0;
    return d;
}

inline std::vector<double> gen_d5_zerocross(size_t n) {
    std::vector<double> d(n);
    double lo = -100.0, hi = 100.0;
    for (size_t i = 0; i < n; ++i) d[i] = lo + (hi - lo) * i / (n - 1);
    return d;
}

inline std::vector<double> gen_d6_exp(size_t n) {
    std::vector<double> d(n);
    double base = 1.02;
    for (size_t i = 0; i < n; ++i) d[i] = std::pow(base, static_cast<double>(i));
    return d;
}

inline std::vector<double> gen_d7_const(size_t n) {
    return std::vector<double>(n, 42.0);
}

inline std::vector<double> gen_d8_randwalk(size_t n, unsigned seed = 99) {
    std::vector<double> d(n);
    std::mt19937 rng(seed);
    std::normal_distribution<> step(0.0, 10.0);
    double y = 0.0;
    for (size_t i = 0; i < n; ++i) {
        y += step(rng);
        d[i] = y;
    }
    return d;
}

// ---------- 2D generators (row-major flattening) ----------

inline std::vector<double> gen_d1_ramp_2d(size_t nx, size_t ny) {
    std::vector<double> d(nx * ny);
    for (size_t iy = 0; iy < ny; ++iy)
        for (size_t ix = 0; ix < nx; ++ix)
            d[iy * nx + ix] = (static_cast<double>(ix) / (nx - 1) + static_cast<double>(iy) / (ny - 1)) * 50.0;
    return d;
}

inline std::vector<double> gen_d2_wide_2d(size_t nx, size_t ny) {
    std::vector<double> d(nx * ny);
    double lo = -3.0, hi = 4.0;
    for (size_t iy = 0; iy < ny; ++iy) {
        double ty = lo + (hi - lo) * iy / (ny - 1);
        for (size_t ix = 0; ix < nx; ++ix) {
            double tx = lo + (hi - lo) * ix / (nx - 1);
            d[iy * nx + ix] = std::pow(10.0, (tx + ty) * 0.5);  // 0.001 ~ 10000
        }
    }
    return d;
}

inline std::vector<double> gen_d3_sinusoid_2d(size_t nx, size_t ny, unsigned seed = 42) {
    std::vector<double> d(nx * ny);
    std::mt19937 rng(seed);
    std::normal_distribution<> noise(0.0, 0.3);
    for (size_t iy = 0; iy < ny; ++iy) {
        double ty = static_cast<double>(iy) / ny;
        for (size_t ix = 0; ix < nx; ++ix) {
            double tx = static_cast<double>(ix) / nx;
            d[iy * nx + ix] = 50.0 + 25.0 * std::sin(2.0 * M_PI * 3.0 * tx)
                              + 20.0 * std::sin(2.0 * M_PI * 2.5 * ty)
                              + 10.0 * std::sin(2.0 * M_PI * 7.0 * tx * ty)
                              + noise(rng);
        }
    }
    return d;
}

inline std::vector<double> gen_d4_cliff_2d(size_t nx, size_t ny) {
    std::vector<double> d(nx * ny);
    size_t hx = nx / 2, hy = ny / 2;
    for (size_t iy = 0; iy < ny; ++iy)
        for (size_t ix = 0; ix < nx; ++ix)
            d[iy * nx + ix] = (ix < hx && iy < hy) ? 0.1 : 1000.0;
    return d;
}

inline std::vector<double> gen_d5_zerocross_2d(size_t nx, size_t ny) {
    std::vector<double> d(nx * ny);
    double lo = -100.0, hi = 100.0;
    for (size_t iy = 0; iy < ny; ++iy) {
        double ty = lo + (hi - lo) * iy / (ny - 1);
        for (size_t ix = 0; ix < nx; ++ix) {
            double tx = lo + (hi - lo) * ix / (nx - 1);
            d[iy * nx + ix] = (tx + ty) * 0.5;
        }
    }
    return d;
}

inline std::vector<double> gen_d6_exp_2d(size_t nx, size_t ny) {
    std::vector<double> d(nx * ny);
    double base = 1.02;
    for (size_t iy = 0; iy < ny; ++iy)
        for (size_t ix = 0; ix < nx; ++ix)
            d[iy * nx + ix] = std::pow(base, static_cast<double>(ix + iy));
    return d;
}

inline std::vector<double> gen_d7_const_2d(size_t nx, size_t ny) {
    return std::vector<double>(nx * ny, 42.0);
}

inline std::vector<double> gen_d8_randwalk_2d(size_t nx, size_t ny, unsigned seed = 99) {
    std::vector<double> d(nx * ny);
    std::mt19937 rng(seed);
    std::normal_distribution<> step(0.0, 10.0);
    double y = 0.0;
    for (size_t i = 0; i < nx * ny; ++i) {
        y += step(rng);
        d[i] = y;
    }
    return d;
}

// ---------- 3D generators (row-major flattening) ----------

inline std::vector<double> gen_d1_ramp_3d(size_t nx, size_t ny, size_t nz) {
    std::vector<double> d(nx * ny * nz);
    for (size_t iz = 0; iz < nz; ++iz)
        for (size_t iy = 0; iy < ny; ++iy)
            for (size_t ix = 0; ix < nx; ++ix) {
                size_t idx = (iz * ny + iy) * nx + ix;
                d[idx] = (static_cast<double>(ix) / (nx - 1) + static_cast<double>(iy) / (ny - 1) + static_cast<double>(iz) / (nz - 1)) * 33.3;
            }
    return d;
}

inline std::vector<double> gen_d2_wide_3d(size_t nx, size_t ny, size_t nz) {
    std::vector<double> d(nx * ny * nz);
    double lo = -3.0, hi = 4.0;
    for (size_t iz = 0; iz < nz; ++iz) {
        double tz = lo + (hi - lo) * iz / (nz - 1);
        for (size_t iy = 0; iy < ny; ++iy) {
            double ty = lo + (hi - lo) * iy / (ny - 1);
            for (size_t ix = 0; ix < nx; ++ix) {
                double tx = lo + (hi - lo) * ix / (nx - 1);
                size_t idx = (iz * ny + iy) * nx + ix;
                d[idx] = std::pow(10.0, (tx + ty + tz) / 3.0);
            }
        }
    }
    return d;
}

inline std::vector<double> gen_d3_sinusoid_3d(size_t nx, size_t ny, size_t nz, unsigned seed = 42) {
    std::vector<double> d(nx * ny * nz);
    std::mt19937 rng(seed);
    std::normal_distribution<> noise(0.0, 0.3);
    for (size_t iz = 0; iz < nz; ++iz) {
        double tz = static_cast<double>(iz) / nz;
        for (size_t iy = 0; iy < ny; ++iy) {
            double ty = static_cast<double>(iy) / ny;
            for (size_t ix = 0; ix < nx; ++ix) {
                double tx = static_cast<double>(ix) / nx;
                size_t idx = (iz * ny + iy) * nx + ix;
                d[idx] = 50.0 + 15.0 * std::sin(2.0 * M_PI * 3.0 * tx)
                         + 15.0 * std::sin(2.0 * M_PI * 2.5 * ty)
                         + 15.0 * std::sin(2.0 * M_PI * 2.0 * tz)
                         + noise(rng);
            }
        }
    }
    return d;
}

inline std::vector<double> gen_d4_cliff_3d(size_t nx, size_t ny, size_t nz) {
    std::vector<double> d(nx * ny * nz);
    size_t hx = nx / 2, hy = ny / 2, hz = nz / 2;
    for (size_t iz = 0; iz < nz; ++iz)
        for (size_t iy = 0; iy < ny; ++iy)
            for (size_t ix = 0; ix < nx; ++ix) {
                size_t idx = (iz * ny + iy) * nx + ix;
                d[idx] = (ix < hx && iy < hy && iz < hz) ? 0.1 : 1000.0;
            }
    return d;
}

// 3D: proper 3D generators
inline std::vector<double> gen_d5_zerocross_3d(size_t nx, size_t ny, size_t nz) {
    std::vector<double> d(nx * ny * nz);
    double lo = -100.0, hi = 100.0;
    for (size_t iz = 0; iz < nz; ++iz) {
        double tz = lo + (hi - lo) * iz / (nz - 1);
        for (size_t iy = 0; iy < ny; ++iy) {
            double ty = lo + (hi - lo) * iy / (ny - 1);
            for (size_t ix = 0; ix < nx; ++ix) {
                double tx = lo + (hi - lo) * ix / (nx - 1);
                size_t idx = (iz * ny + iy) * nx + ix;
                d[idx] = (tx + ty + tz) / 3.0;
            }
        }
    }
    return d;
}

inline std::vector<double> gen_d6_exp_3d(size_t nx, size_t ny, size_t nz) {
    std::vector<double> d(nx * ny * nz);
    double base = 1.015;
    for (size_t i = 0; i < nx * ny * nz; ++i)
        d[i] = std::pow(base, static_cast<double>(i));
    return d;
}

inline std::vector<double> gen_d7_const_3d(size_t nx, size_t ny, size_t nz) {
    return std::vector<double>(nx * ny * nz, 42.0);
}

inline std::vector<double> gen_d8_randwalk_3d(size_t nx, size_t ny, size_t nz) {
    std::vector<double> d(nx * ny * nz);
    std::mt19937 rng(99u);
    std::normal_distribution<> step(0.0, 10.0);
    double y = 0.0;
    for (size_t i = 0; i < nx * ny * nz; ++i) {
        y += step(rng);
        d[i] = y;
    }
    return d;
}

}  // namespace sz3_test

#endif
