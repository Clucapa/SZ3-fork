#ifndef SZ3_QOI_X2_HPP
#define SZ3_QOI_X2_HPP

#include <algorithm>
#include <cmath>
#include "SZ3/qoi/QoI.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_X2 : public concepts::QoIIf<T, N> {
public:
    QoI_X2(double tol, T geb)
        : tol(tol), geb(geb) {
        concepts::QoIIf<T, N>::id = 1;
    }

    T interpret_eb(T x) const override {
        T eb = -fabs(x) + sqrt(x * x + tol);
        return std::min(eb, geb);
    }

    bool check_comply(T orig, T dec) const override {
        return fabs(orig * orig - dec * dec) <= tol;
    }

    T get_geb() const override { return geb; }
    void set_geb(T eb) override { geb = eb; }
    double get_tol() const override { return tol; }
    void set_tol(double t) override { tol = t; }

private:
    double tol;
    T geb;
};

}  // namespace SZ3

#endif
