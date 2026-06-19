// QoI_FX — arbitrary-function QOI via TinyExpr bytecode.
// Activated when (conf.qoi >> 28) & 0xF == 7 (high nibble marker from encoder).
// conf.qoiParams is base64-decoded BEFORE construction; the constructor receives
// the raw binary payload: [len_f|f_str|len_df|df_str|len_ddf|ddf_str].

#ifndef SZ3_QOI_FX_HPP
#define SZ3_QOI_FX_HPP

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "SZ3/utils/tinyexpr/tinyexpr.h"
}

#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_FX : public concepts::QoIIf<T, N> {
public:
    // raw = base64-decoded binary payload from conf.qoiParams
    QoI_FX(double tol, T geb, const std::vector<unsigned char> &raw)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 0x70000000;

        if (raw.size() < 12) { f_expr_ = df_expr_ = ddf_expr_ = nullptr; return; }

        size_t pos = 0;
        auto read_u32 = [&]() -> uint32_t {
            if (pos + 4 > raw.size()) return 0;
            uint32_t v = raw[pos] | (raw[pos+1]<<8) | (raw[pos+2]<<16) | (raw[pos+3]<<24);
            pos += 4;
            return v;
        };

        uint32_t len_f   = read_u32();
        f_str_   = std::string(reinterpret_cast<const char*>(&raw[pos]), len_f);   pos += len_f;
        uint32_t len_df  = read_u32();
        df_str_  = std::string(reinterpret_cast<const char*>(&raw[pos]), len_df);  pos += len_df;
        uint32_t len_ddf = read_u32();
        ddf_str_ = std::string(reinterpret_cast<const char*>(&raw[pos]), len_ddf);

        if (f_str_.empty() && df_str_.empty() && ddf_str_.empty()) { f_expr_ = df_expr_ = ddf_expr_ = nullptr; return; }

        te_variable vars[] = {{"x", &x_val_, TE_VARIABLE, nullptr}};
        int err = 0;
        f_expr_   = te_compile(f_str_.c_str(),   vars, 1, &err);
        df_expr_  = te_compile(df_str_.c_str(),  vars, 1, &err);
        ddf_expr_ = te_compile(ddf_str_.c_str(), vars, 1, &err);
    }

    ~QoI_FX() {
        if (f_expr_)   te_free(f_expr_);
        if (df_expr_)  te_free(df_expr_);
        if (ddf_expr_) te_free(ddf_expr_);
    }

    // Non-copyable (owns te_expr pointers).
    QoI_FX(const QoI_FX &) = delete;
    QoI_FX &operator=(const QoI_FX &) = delete;

    T interpret_eb(T x) const override {
        if (!df_expr_) return geb_;
        x_val_ = static_cast<double>(x);
        double d = te_eval(df_expr_);
        if (d == 0.0) return geb_;
        T eb = static_cast<T>(tol_ / std::fabs(d));
        return std::min(eb, geb_);
    }

    bool check_comply(T orig, T dec) const override {
        x_val_ = static_cast<double>(orig);
        double fo = f_expr_ ? te_eval(f_expr_) : static_cast<double>(orig);
        x_val_ = static_cast<double>(dec);
        double fd = f_expr_ ? te_eval(f_expr_) : static_cast<double>(dec);
        return std::fabs(fo - fd) <= tol_;
    }

    double eval(T val) const override {
        if (!f_expr_) return static_cast<double>(val);
        x_val_ = static_cast<double>(val);
        return te_eval(f_expr_);
    }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &conf) override {
        return std::make_unique<PointwiseEBProvider<T>>(
            conf.ebs.data(), conf.ebs.size());
    }

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }

private:
    double tol_;
    T geb_;
    mutable double x_val_ = 0.0;  // TinyExpr's bound variable, updated per call

    std::string f_str_, df_str_, ddf_str_;
    te_expr *f_expr_   = nullptr;
    te_expr *df_expr_  = nullptr;
    te_expr *ddf_expr_ = nullptr;
};

}  // namespace SZ3

#endif
