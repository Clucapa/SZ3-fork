#ifndef SZ3_QOI_MULTIQOI_HPP
#define SZ3_QOI_MULTIQOI_HPP

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/MultiQoIEBProvider.hpp"

namespace SZ3 {

template <class T, uint N>
class QoI_MultiQoI : public concepts::QoIIf<T, N> {
public:
    QoI_MultiQoI(std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> groups)
        : groups_(std::move(groups)) {
        concepts::QoIIf<T, N>::id = 0;
        if (!groups_.empty()) {
            tol_ = groups_[0]->get_tol();
            geb_ = groups_[0]->get_geb();
        }
    }

    T interpret_eb(T x) const override {
        T eb = groups_[0]->interpret_eb(x);
        for (size_t i = 1; i < groups_.size(); ++i)
            eb = std::min(eb, groups_[i]->interpret_eb(x));
        return eb;
    }

    bool check_comply(T orig, T dec) const override {
        for (auto &g : groups_)
            if (!g->check_comply(orig, dec)) return false;
        return true;
    }

    double eval(T val) const override {
        return static_cast<double>(val);
    }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &conf) override {
        std::vector<std::unique_ptr<concepts::EBProvider<T>>> providers;
        for (auto &g : groups_)
            providers.push_back(g->create_eb_provider(conf));
        return std::make_unique<MultiQoIEBProvider<T>>(std::move(providers));
    }

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override {
        geb_ = eb;
        for (auto &g : groups_) g->set_geb(eb);
    }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override {
        tol_ = t;
        for (auto &g : groups_) g->set_tol(t);
    }

    void precompress_block(size_t n) override {
        for (auto &g : groups_) g->precompress_block(n);
    }
    void update_tolerance(T orig, T dec) override {
        for (auto &g : groups_) g->update_tolerance(orig, dec);
    }
    void postcompress_block() override {
        for (auto &g : groups_) g->postcompress_block();
    }

    const auto &groups() const { return groups_; }

private:
    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> groups_;
    double tol_;
    T geb_;
};

}  // namespace SZ3

#endif
