#ifndef SZ3_QOI_ISOLINE_NIBBLE_HPP
#define SZ3_QOI_ISOLINE_NIBBLE_HPP

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "SZ3/def.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

struct IsolineConfig {
    double min_v = 0;
    double max_v = 0;
    int count = 0;
    double meb = 0;
};

template <class T, uint N>
class QoI_IsolineNibble : public concepts::QoIIf<T, N> {
public:
    QoI_IsolineNibble(double tol, T geb,
                      std::shared_ptr<concepts::QoIIf<T, N>> sub_qoi,
                      IsolineConfig config)
        : tol_(tol), geb_(geb), sub_qoi_(std::move(sub_qoi)), config_(config) {
        concepts::QoIIf<T, N>::id = 0xD;
        generate_isovalues();
    }

    T interpret_eb(T data) const override {
        T inner_eb = sub_qoi_ ? sub_qoi_->interpret_eb(data) : geb_;
        T isoline_eb = std::numeric_limits<T>::max();

        if (!isovalues_.empty()) {
            double v = sub_qoi_ ? sub_qoi_->eval(data) : static_cast<double>(data);
            double nearest_dist = nearest_isovalue_distance(v);
            if (nearest_dist < std::numeric_limits<double>::max()) {
                double deriv = approx_derivative(data);
                if (std::fabs(deriv) > 1e-15)
                    isoline_eb = static_cast<T>(nearest_dist / std::fabs(deriv));
            }
        }

        T eb = std::min(geb_, std::min(inner_eb, isoline_eb));
        return std::max(static_cast<T>(config_.meb), eb);
    }

    bool check_comply(T orig, T dec) const override {
        if (sub_qoi_ && !sub_qoi_->check_comply(orig, dec))
            return false;
        if (isovalues_.empty()) return true;
        double vorig = sub_qoi_ ? sub_qoi_->eval(orig) : static_cast<double>(orig);
        double vdec  = sub_qoi_ ? sub_qoi_->eval(dec)  : static_cast<double>(dec);
        for (double iso : isovalues_) {
            if ((vorig - iso) * (vdec - iso) < 0)
                return false;
        }
        return true;
    }

    double eval(T val) const override {
        return sub_qoi_ ? sub_qoi_->eval(val) : static_cast<double>(val);
    }

    void precompress_block(size_t) override {}
    void update_tolerance(T, T) override {}
    void postcompress_block() override {}

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }

    bool is_pointwise() const override { return true; }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &conf) override {
        return std::make_unique<PointwiseEBProvider<T>>(
            conf.ebs.data(), conf.ebs.size());
    }

    const std::vector<double> &isovalues() const { return isovalues_; }
    double meb() const { return config_.meb; }

private:
    void generate_isovalues() {
        if (config_.count <= 0) return;
        double range = config_.max_v - config_.min_v;
        if (range <= 0) return;
        isovalues_.reserve(static_cast<size_t>(config_.count));
        for (int i = 0; i < config_.count; i++)
            isovalues_.push_back(config_.min_v + (i + 1) * range / (config_.count + 1));
    }

    double nearest_isovalue_distance(double val) const {
        if (isovalues_.empty()) return std::numeric_limits<double>::max();
        auto it = std::lower_bound(isovalues_.begin(), isovalues_.end(), val);
        double d = std::numeric_limits<double>::max();
        if (it != isovalues_.end()) d = std::fabs(val - *it);
        if (it != isovalues_.begin()) d = std::min(d, std::fabs(val - *(it - 1)));
        return d;
    }

    double approx_derivative(double x) const {
        if (!sub_qoi_) return 1.0;
        double h = std::max(1e-8, 1e-8 * std::fabs(x));
        if (x + h == x) h = 1e-8;
        double fph = sub_qoi_->eval(static_cast<T>(x + h));
        double fmh = sub_qoi_->eval(static_cast<T>(x - h));
        if (!std::isfinite(fph) || !std::isfinite(fmh))
            return 0.0;
        return (fph - fmh) / (2 * h);
    }

    double tol_;
    T geb_;
    std::shared_ptr<concepts::QoIIf<T, N>> sub_qoi_;
    IsolineConfig config_;
    std::vector<double> isovalues_;
};

}  // namespace SZ3

#endif
