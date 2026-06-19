#ifndef SZ3_QOI_XSQRT_HPP
#define SZ3_QOI_XSQRT_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XSqrt : public concepts::QoIIf<T, N> {
public:
    QoI_XSqrt(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 3;
    }

    double eval(T val) const override {
        return std::sqrt(std::fabs(static_cast<double>(val)));
    }

    T interpret_eb(T x) const override {
        double d = std::fabs(static_cast<double>(x));
        double sqr = std::sqrt(d);
        T eb = sqr >= tol_ ? static_cast<T>(2.0 * tol_ * sqr - tol_ * tol_)
                           : static_cast<T>(2.0 * tol_ * sqr + tol_ * tol_);
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
