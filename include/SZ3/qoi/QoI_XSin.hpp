#ifndef SZ3_QOI_XSIN_HPP
#define SZ3_QOI_XSIN_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XSin : public concepts::QoIIf<T, N> {
public:
    QoI_XSin(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 9;
    }

    double eval(T val) const override {
        return std::sin(static_cast<double>(val));
    }

    T interpret_eb(T x) const override {
        double c = std::cos(static_cast<double>(x));
        if (std::abs(c) < 1e-15) return std::min(static_cast<T>(tol_), geb_);
        T eb = static_cast<T>(tol_ / std::abs(c));
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
