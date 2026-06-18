#ifndef SZ3_QOI_LOGX_HPP
#define SZ3_QOI_LOGX_HPP

#include <cmath>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_LogX : public concepts::QoIIf<T, N> {
public:
    QoI_LogX(double tol, T geb, double base = std::exp(1.0))
        : tol_(tol), geb_(geb), base_(base) {
        concepts::QoIIf<T, N>::id = 6;
    }

    double eval(T val) const override {
        double ax = std::fabs(static_cast<double>(val));
        if (ax < 1e-15) return 0.0;
        return std::log(ax) / std::log(base_);
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
    double base_;
};

}  // namespace SZ3

#endif
