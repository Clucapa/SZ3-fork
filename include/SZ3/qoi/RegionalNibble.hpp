#ifndef SZ3_REGIONAL_NIBBLE_HPP
#define SZ3_REGIONAL_NIBBLE_HPP

#include <algorithm>
#include <cmath>
#include <memory>
#include "SZ3/def.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/EBProvider.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_RegionalNibble : public concepts::QoIIf<T, N> {
public:
    QoI_RegionalNibble(double tol, T geb, std::shared_ptr<concepts::QoIIf<T, N>> sub,
                        bool is_interp)
        : tol_(tol), geb_(geb), sub_(std::move(sub)), is_interp_(is_interp) {
        concepts::QoIIf<T, N>::id = ~0;
    }

    T interpret_eb(T data) const override {
        if (rest_elements_ == 0) return geb_;
        double eb_f = (agg_tol_ - std::fabs(error_)) / static_cast<double>(rest_elements_);
        if (rest_elements_ > 0.5 * block_elements_)
            eb_f *= 2.0;
        double h = std::max(1e-8, 1e-8 * std::fabs(static_cast<double>(data)));
        double deriv = std::fabs(sub_->eval(static_cast<T>(data + h))
                               - sub_->eval(static_cast<T>(data - h))) / (2.0 * h);
        if (deriv < 1e-15) deriv = 1e-15;
        T eb = static_cast<T>(eb_f / deriv);
        return std::min(eb, geb_);
    }

    bool check_comply(T, T) const override { return true; }

    void precompress_block(size_t num_elements) override {
        rest_elements_ = num_elements;
        block_elements_ = num_elements;
        agg_tol_ = tol_ * num_elements;
        error_ = 0;
    }

    void update_tolerance(T orig, T dec) override {
        error_ += sub_->eval(orig) - sub_->eval(dec);
        if (rest_elements_ > 0) rest_elements_--;
    }

    void postcompress_block() override {}

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }
    bool is_pointwise() const override { return false; }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &) override {
        struct Impl : concepts::EBProvider<T> {
            QoI_RegionalNibble *q;
            explicit Impl(QoI_RegionalNibble *q_) : q(q_) {}
            T advance(T orig, T dec) override {
                T eb = q->interpret_eb(orig);
                q->update_tolerance(orig, dec);
                return eb;
            }
            void advance() override {}
            void precompress_block(size_t n) override { q->precompress_block(n); }
            void postcompress_block() override { q->postcompress_block(); }
            void reset() override {}
            void save(uchar *&) const override {}
            void load(const uchar *&, size_t &) override {}
        };
        return std::make_unique<Impl>(this);
    }

    std::shared_ptr<concepts::QoIIf<T, N>> sub_qoi() const { return sub_; }

private:
    double tol_;
    T geb_;
    std::shared_ptr<concepts::QoIIf<T, N>> sub_;
    bool is_interp_;
    double error_ = 0;
    size_t rest_elements_ = 0;
    size_t block_elements_ = 0;
    double agg_tol_ = 0;
};

}  // namespace SZ3
#endif

