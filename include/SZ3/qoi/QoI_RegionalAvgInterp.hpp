#ifndef SZ3_QOI_REGIONAL_AVG_INTERP_HPP
#define SZ3_QOI_REGIONAL_AVG_INTERP_HPP

#include <algorithm>
#include "SZ3/def.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/EBProvider.hpp"

namespace SZ3 {

template <typename T, uint N>
class RegionalAvgInterpEBProvider;

template <class T, uint N>
class QoI_RegionalAvgInterp : public concepts::QoIIf<T, N> {
public:
    QoI_RegionalAvgInterp(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 12;
    }

    T interpret_eb(T) const override {
        return geb_;
    }

    bool check_comply(T, T) const override {
        return true;
    }

    void precompress_block(size_t) override {}

    void update_tolerance(T, T) override {}

    void postcompress_block() override {}

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }

    double eval(T val) const override {
        return static_cast<double>(val);
    }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &) override {
        return std::make_unique<RegionalAvgInterpEBProvider<T, N>>(this);
    }

private:
    double tol_;
    T geb_;
};

template <typename T, uint N>
class RegionalAvgInterpEBProvider : public concepts::EBProvider<T> {
public:
    RegionalAvgInterpEBProvider(QoI_RegionalAvgInterp<T, N> *qoi)
        : qoi_(qoi), pos_(0) {}

    void precompress_block(size_t num_elements) override {
        qoi_->precompress_block(num_elements);
        pos_ = 0;
    }

    void postcompress_block() override { qoi_->postcompress_block(); }

    T advance(T orig, T dec) override {
        T eb = qoi_->interpret_eb(orig);
        pos_++;
        return eb;
    }

    void advance() override { pos_++; }

    void reset() override {
        pos_ = 0;
    }

    void save(uchar *&) const override {}
    void load(const uchar *&, size_t &) override {}

private:
    QoI_RegionalAvgInterp<T, N> *qoi_;
    size_t pos_;
};

}  // namespace SZ3

#endif
