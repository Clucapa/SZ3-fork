#ifndef SZ3_QOI_SUMQOI_HPP
#define SZ3_QOI_SUMQOI_HPP

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_SumQoI : public concepts::QoIIf<T, N> {
public:
    QoI_SumQoI(std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> funcs,
               double tol, T geb)
        : funcs_(std::move(funcs)), tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 0;
    }

    T interpret_eb(T x) const override {
        double sum_deriv = 0;
        for (auto &f : funcs_) {
            double h = std::max(1e-6, 1e-4 * std::fabs(static_cast<double>(x)));
            sum_deriv += (f->eval(static_cast<T>(x + h)) -
                          f->eval(static_cast<T>(x - h))) / (2 * h);
        }
        T eb = std::fabs(sum_deriv) < 1e-15 ? geb_
            : static_cast<T>(tol_ / std::fabs(sum_deriv));
        return std::min(eb, geb_);
    }

    bool check_comply(T orig, T dec) const override {
        return std::fabs(eval(orig) - eval(dec)) <= tol_;
    }

    double eval(T val) const override {
        double s = 0;
        for (auto &f : funcs_) s += f->eval(val);
        return s;
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

    const auto &funcs() const { return funcs_; }

private:
    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> funcs_;
    double tol_;
    T geb_;
};

}  // namespace SZ3

#endif
