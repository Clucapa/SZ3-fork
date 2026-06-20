// Shared verification helpers -- all QOI checks use hardcoded f(x) only.

#ifndef SZ3_TEST_VERIFY_HPP
#define SZ3_TEST_VERIFY_HPP

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "SZ3/api/sz.hpp"

namespace sz3_test {

struct TestResult {
    bool passed = true;
    const char *fail_reason = nullptr;
    double max_abs = 0;
    double max_qoi = 0;
    double max_qoi2 = 0;   // second group for MultiQoI
    size_t cmp_size = 0;
    size_t qoi_fails = 0;
    std::string detail;
};

inline double max_abs_err(const double *a, const double *b, size_t n) {
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(a[i] - b[i]);
        if (e > me) me = e;
    }
    return me;
}

// Hardcoded per-point QOI check: |f(x_i) - f(x_i')| ≤ tau for all i.
inline bool check_hardcoded_comply(double (*feval)(double), double tau,
                                    const double *orig, const double *dec, size_t n,
                                    size_t *fail_count) {
    size_t fc = 0;
    for (size_t i = 0; i < n; ++i) {
        if (std::fabs(feval(orig[i]) - feval(dec[i])) > tau * (1.0 + 1e-12))
            fc++;
    }
    *fail_count = fc;
    return fc == 0;
}

inline double max_hardcoded_qoi(double (*feval)(double),
                                 const double *orig, const double *dec, size_t n) {
    double me = 0;
    for (size_t i = 0; i < n; ++i) {
        double e = std::fabs(feval(orig[i]) - feval(dec[i]));
        if (e > me) me = e;
    }
    return me;
}

// Generic compress-decompress roundtrip.
inline double *roundtrip_compress(SZ3::Config &conf, const double *data, size_t num,
                                   size_t &cmpSize_out, TestResult &r) {
    using namespace SZ3;
    cmpSize_out = 0;
    auto bound = SZ_compress_size_bound<double>(conf);
    std::vector<unsigned char> cmp(bound);
    try {
        cmpSize_out = SZ_compress<double>(conf, data,
                       reinterpret_cast<char *>(cmp.data()), cmp.size());
    } catch (const std::exception &e) {
        r.passed = false; r.fail_reason = "cmp-exception"; r.detail = e.what();
        return nullptr;
    }
    r.cmp_size = cmpSize_out;
    if (cmpSize_out == 0) { r.passed = false; r.fail_reason = "cmpSize=0"; return nullptr; }

    Config conf2;
    double *dec = nullptr;
    try {
        dec = SZ_decompress<double>(conf2,
                reinterpret_cast<const char *>(cmp.data()), cmpSize_out);
    } catch (const std::exception &e) {
        r.passed = false; r.fail_reason = "dec-exception"; r.detail = e.what();
        return nullptr;
    }
    if (!dec) { r.passed = false; r.fail_reason = "dec-null"; return nullptr; }
    if (conf2.num != num) { delete[] dec; r.passed = false; r.fail_reason = "num-mismatch"; return nullptr; }
    return dec;
}

static const char kBase64Tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode_raw(const void *data, size_t len) {
    const auto *p = static_cast<const unsigned char *>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned b0 = p[i], b1 = (i+1 < len) ? p[i+1] : 0, b2 = (i+2 < len) ? p[i+2] : 0;
        out += kBase64Tbl[b0 >> 2];
        out += kBase64Tbl[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i+1 < len) ? kBase64Tbl[((b1 & 0xF) << 2) | (b2 >> 6)] : '=';
        out += (i+2 < len) ? kBase64Tbl[b2 & 0x3F] : '=';
    }
    return out;
}

inline std::vector<unsigned char> base64_decode_raw(const std::string &in) {
    if (in.empty()) return {};
    auto val = [](char c) -> unsigned char {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0xFF;
    };
    std::vector<unsigned char> out;
    out.reserve((in.size() * 3) / 4 + 1);
    size_t i = 0;
    for (; i + 4 <= in.size(); i += 4) {
        unsigned char a = val(in[i]), b = val(in[i + 1]);
        if (a == 0xFF || b == 0xFF) break;
        out.push_back((a << 2) | (b >> 4));
        if (in[i + 2] == '=') { out.push_back((b << 4)); break; }
        unsigned char c = val(in[i + 2]);
        if (c == 0xFF) break;
        out.push_back((b << 4) | (c >> 2));
        if (in[i + 3] == '=') break;
        unsigned char d = val(in[i + 3]);
        if (d == 0xFF) break;
        out.push_back((c << 6) | d);
    }
    return out;
}

inline std::string base64_encode_doubles(const double *data, size_t n) {
    return base64_encode_raw(data, n * sizeof(double));
}

inline bool check_isoline_comply(double (*feval)(double),
                                  const double *isovalues, size_t n_iso,
                                  const double *orig, const double *dec, size_t n,
                                  size_t *fail_count) {
    size_t fc = 0;
    for (size_t i = 0; i < n; ++i) {
        double vorig = feval ? feval(orig[i]) : orig[i];
        double vdec  = feval ? feval(dec[i])  : dec[i];
        for (size_t j = 0; j < n_iso; ++j) {
            if ((vorig - isovalues[j]) * (vdec - isovalues[j]) < 0) {
                fc++;
                break;
            }
        }
    }
    *fail_count = fc;
    return fc == 0;
}

inline std::vector<double> generate_isovalues(double min_v, double max_v, int count) {
    std::vector<double> iso;
    if (count <= 0 || max_v <= min_v) return iso;
    double range = max_v - min_v;
    iso.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++)
        iso.push_back(min_v + (i + 1) * range / (count + 1));
    return iso;
}

}  // namespace sz3_test
#endif
