#ifndef SZ3_QOI_XABS_HPP
#define SZ3_QOI_XABS_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XAbs : public concepts::QoIIf<T, N> {
public:
    QoI_XAbs(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 8;
    }

    T interpret_eb(T) const override {
        return std::min(static_cast<T>(tol_), geb_);
    }

    double eval(T val) const override {
        return std::fabs(static_cast<double>(val));
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
};

}  // namespace SZ3

#endif
