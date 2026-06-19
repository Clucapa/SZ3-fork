#ifndef SZ3_QOI_XLOGX_HPP
#define SZ3_QOI_XLOGX_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XLogX : public concepts::QoIIf<T, N> {
public:
    QoI_XLogX(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 5;
    }

    double eval(T val) const override {
        double ax = std::fabs(static_cast<double>(val));
        if (ax < 1e-15) return 0.0;
        return static_cast<double>(val) * std::log(ax);
    }

    T interpret_eb(T x) const override {
        double d = static_cast<double>(x);
        if (std::fabs(d) < 1e-15) return geb_;
        double a = std::fabs(std::log(std::fabs(d)) + 1.0) / std::log(2.0);
        double b = std::fabs(1.0 / (d * std::log(2.0)));
        T eb = (b != 0.0) ? static_cast<T>((std::sqrt(a * a + 2.0 * b * tol_) - a) / b)
              : (a != 0.0) ? static_cast<T>(tol_ / a)
              : geb_;
        return std::min(eb, geb_);
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
};

}  // namespace SZ3

#endif
