#ifndef SZ3_QOI_CONV_HPP
#define SZ3_QOI_CONV_HPP

#include <algorithm>
#include <cmath>
#include <vector>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_Conv : public concepts::QoIIf<T, N> {
public:
    QoI_Conv(double tol, T geb, std::vector<double> kernel, double conv_tol)
        : tol_(tol), geb_(geb), kernel_(std::move(kernel)), conv_tol_(conv_tol) {
        concepts::QoIIf<T, N>::id = ~0x70000000;
        int w = static_cast<int>(kernel_.size());
        center_ = w / 2;
    }

    T interpret_eb(T) const override { return geb_; }

    double eval(T val) const override { return static_cast<double>(val); }

    bool check_comply(T, T) const override { return true; }

    bool is_pointwise() const override { return true; }

    bool has_bias() const override { return true; }

    void precompute_data(const T *data, size_t n) override {
        biases_.clear();
        int w = static_cast<int>(kernel_.size());
        int c = center_;
        if (n < static_cast<size_t>(w)) {
            biases_.resize(n, 0);
            return;
        }
        biases_.assign(n, 0);
        max_iters_ = 5;

        for (int iter = 0; iter < max_iters_; ++iter) {
            bool any_violation = false;
            for (int p = c; p < static_cast<int>(n) - (w - 1 - c); ++p) {
                if (p < 0 || p + w > static_cast<int>(n)) continue;
                double delta = 0;
                for (int j = 0; j < w; ++j) {
                    int idx = p - c + j;
                    delta += kernel_[j] * biases_[idx];
                }
                if (std::fabs(delta) <= conv_tol_) continue;
                any_violation = true;
                double excess = delta - (delta > 0 ? conv_tol_ : -conv_tol_);
                double k_sq_sum = 0;
                for (int j = 0; j < w; ++j) k_sq_sum += kernel_[j] * kernel_[j];
                if (k_sq_sum < 1e-15) continue;
                for (int j = 0; j < w; ++j) {
                    int idx = p - c + j;
                    biases_[idx] -= excess * kernel_[j] / k_sq_sum;
                }
            }
            if (!any_violation) break;
        }
    }

    T get_bias(size_t idx) const override {
        return idx < biases_.size() ? static_cast<T>(biases_[idx]) : 0;
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

    const std::vector<double> &kernel() const { return kernel_; }
    double conv_tol() const { return conv_tol_; }

private:
    double tol_;
    T geb_;
    std::vector<double> kernel_;
    double conv_tol_;
    int center_ = 0;
    int max_iters_ = 5;
    std::vector<double> biases_;
};

}  // namespace SZ3

#endif
