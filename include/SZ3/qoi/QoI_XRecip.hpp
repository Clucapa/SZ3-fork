#ifndef SZ3_QOI_XRECIP_HPP
#define SZ3_QOI_XRECIP_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XRecip : public concepts::QoIIf<T, N> {
public:
    QoI_XRecip(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 7;
    }

    double eval(T val) const override {
        double ax = std::fabs(static_cast<double>(val));
        if (ax < 1e-15) return 0.0;
        return 1.0 / static_cast<double>(val);
    }

    T interpret_eb(T x) const override {
        double d = static_cast<double>(x);
        T eb = d >= 0.0 ? static_cast<T>(tol_ * d * d / (1.0 + tol_ * d))
                        : static_cast<T>(tol_ * d * d / (1.0 - tol_ * d));
        return std::min(eb, geb_);
    }

    bool check_comply(T orig, T dec) const override {
        if (orig == static_cast<T>(0)) return dec == static_cast<T>(0);
        if (dec == static_cast<T>(0)) return false;
        return std::fabs(1.0 / static_cast<double>(orig)
                       - 1.0 / static_cast<double>(dec)) <= tol_;
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
