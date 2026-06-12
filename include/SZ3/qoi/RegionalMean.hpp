#ifndef SZ3_REGIONAL_MEAN_HPP
#define SZ3_REGIONAL_MEAN_HPP

#include <algorithm>
#include <cmath>
#include "SZ3/def.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/EBProvider.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_RegionalMean : public concepts::QoIIf<T, N> {
public:
    QoI_RegionalMean(double tol, T geb)
        : tol_(tol), geb_(geb) {
        concepts::QoIIf<T, N>::id = 10;
    }

    T interpret_eb(T) const override {
        T eb;
        if (rest_elements_ > 0.5 * block_elements_) {
            eb = (aggregated_tolerance_ - fabs(error_)) * 2 / rest_elements_;
        } else {
            eb = (aggregated_tolerance_ - fabs(error_)) / rest_elements_;
        }
        return std::min(eb, geb_);
    }

    bool check_comply(T, T) const override {
        return true;
    }

    void precompress_block(size_t num_elements) override {
        rest_elements_ = static_cast<int>(num_elements);
        block_elements_ = static_cast<int>(num_elements);
        aggregated_tolerance_ = tol_ * num_elements;
        error_ = 0;
    }

    void update_tolerance(T orig, T dec) override {
        error_ += orig - dec;
        rest_elements_--;
    }

    void postcompress_block() override {}

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }

private:
    double tol_;
    T geb_;
    double error_ = 0;
    int rest_elements_ = 0;
    int block_elements_ = 0;
    double aggregated_tolerance_ = 0;
};

template <typename T, uint N>
class RegionalMeanEBProvider : public concepts::EBProvider<T> {
public:
    RegionalMeanEBProvider(QoI_RegionalMean<T, N> *qoi)
        : qoi_(qoi), pos_(0) {}

    void precompress_block(size_t num_elements) override {
        qoi_->precompress_block(num_elements);
        pos_ = 0;
    }

    void postcompress_block() override { qoi_->postcompress_block(); }

    T advance(T orig, T dec) override {
        T eb = qoi_->interpret_eb(orig);
        qoi_->update_tolerance(orig, dec);
        pos_++;
        return eb;
    }

    void advance() override { pos_++; }

    void reset() override {
        qoi_->precompress_block(0);
        pos_ = 0;
    }

    void save(uchar *&) const override {}
    void load(const uchar *&, size_t &) override {}

private:
    QoI_RegionalMean<T, N> *qoi_;
    size_t pos_;
};

}  // namespace SZ3

#endif
