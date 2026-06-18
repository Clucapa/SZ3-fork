#ifndef SZ3_QOI_COMPOSE_HPP
#define SZ3_QOI_COMPOSE_HPP

#include <cmath>
#include <memory>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_Compose : public concepts::QoIIf<T, N> {
public:
    QoI_Compose(std::shared_ptr<concepts::QoIIf<T, N>> outer,
                std::shared_ptr<concepts::QoIIf<T, N>> inner,
                double tol, T geb)
        : outer_(std::move(outer)), inner_(std::move(inner)),
          tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 0xE;
    }

    double eval(T val) const override {
        return outer_->eval(inner_->eval(val));
    }

    T interpret_eb(T x) const override {
        double h = std::max(1e-8, 1e-6 * std::fabs(static_cast<double>(x)));
        double inner_x = inner_->eval(x);
        double f_prime = (outer_->eval(static_cast<T>(inner_x + h)) -
                          outer_->eval(static_cast<T>(inner_x - h))) / (2 * h);
        double g_prime = (inner_->eval(static_cast<T>(x + h)) -
                          inner_->eval(static_cast<T>(x - h))) / (2 * h);
        double deriv = f_prime * g_prime;
        T eb = (deriv != 0) ? static_cast<T>(tol_ / std::fabs(deriv)) : geb_;
        return std::min(eb, geb_);
    }

    bool check_comply(T orig, T dec) const override {
        return std::fabs(eval(orig) - eval(dec)) <= tol_;
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
    std::shared_ptr<concepts::QoIIf<T, N>> outer_;
    std::shared_ptr<concepts::QoIIf<T, N>> inner_;
    double tol_;
    T geb_;
};

}  // namespace SZ3

#endif
