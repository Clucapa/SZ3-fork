#ifndef SZ3_QOI_XPOWER_HPP
#define SZ3_QOI_XPOWER_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XPower : public concepts::QoIIf<T, N> {
public:
    QoI_XPower(double tol, T geb, double expo = 2.0)
        : tol_(tol), geb_(geb), expo_(expo) {
        concepts::QoIIf<T, N>::id = 11;  // nibble 0xB
    }

    double eval(T val) const override {
        return std::pow(std::fabs(static_cast<double>(val)), expo_);
    }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &conf) override {
        return std::make_unique<PointwiseEBProvider<T>>(
            conf.ebs.data(), conf.ebs.size());
    }

    bool is_pointwise() const override { return true; }

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }

private:
    double tol_;
    T geb_;
    double expo_;
};

}  // namespace SZ3

#endif
