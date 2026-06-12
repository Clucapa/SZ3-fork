#ifndef SZ3_QOI_XLIN_HPP
#define SZ3_QOI_XLIN_HPP

#include <algorithm>
#include "SZ3/qoi/QoI.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_XLin : public concepts::QoIIf<T, N> {
public:
    QoI_XLin(double tol, T geb)
        : tol(tol), geb(geb) {
        concepts::QoIIf<T, N>::id = 0;
    }

    T interpret_eb(T) const override {
        return std::min(static_cast<T>(tol), geb);
    }

    bool check_comply(T orig, T dec) const override {
        return fabs(orig - dec) <= tol;
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
