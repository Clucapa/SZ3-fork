#ifndef SZ3_QOI_XLIN_HPP
#define SZ3_QOI_XLIN_HPP

#include <algorithm>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XLin : public concepts::QoIIf<T, N> {
public:
    QoI_XLin(double tol, T geb, double A = 1.0, double B = 0.0)
        : tol(tol), geb(geb), A_(A), B_(B) {
        concepts::QoIIf<T, N>::id = 0;
    }

    T interpret_eb(T) const override {
        if (A_ == 0.0) return geb;
        return std::min(static_cast<T>(tol / std::fabs(A_)), geb);
    }

    bool check_comply(T orig, T dec) const override {
        return fabs(eval(orig) - eval(dec)) <= tol;
    }

    double eval(T val) const override {
        return A_ * static_cast<double>(val) + B_;
    }

    double A() const { return A_; }
    double B() const { return B_; }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &conf) override {
        return std::make_unique<PointwiseEBProvider<T>>(
            conf.ebs.data(), conf.ebs.size());
    }

    T get_geb() const override { return geb; }
    void set_geb(T eb) override { geb = eb; }
    double get_tol() const override { return tol; }
    void set_tol(double t) override { tol = t; }

private:
    double tol;
    T geb;
    double A_;
    double B_;
};

}  // namespace SZ3

#endif
