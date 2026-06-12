#ifndef SZ3_POINTWISE_EB_PROVIDER_HPP
#define SZ3_POINTWISE_EB_PROVIDER_HPP

#include <cstring>
#include <vector>
#include "SZ3/def.hpp"
#include "SZ3/qoi/EBProvider.hpp"
#include "SZ3/utils/MemoryUtil.hpp"

namespace SZ3 {

template <typename T>
class PointwiseEBProvider : public concepts::EBProvider<T> {
public:
    PointwiseEBProvider(const double *ebs, size_t n)
        : ebs_(ebs), size_(n), pos_(0) {}

    void precompress_block(size_t) override {}

    void postcompress_block() override {}

    T advance(T, T) override {
        T eb = static_cast<T>(ebs_[pos_]);
        pos_++;
        return eb;
    }

    void advance() override { pos_++; }

    void reset() override { pos_ = 0; }

    void save(uchar *&c) const override {
        write(size_, c);
        for (size_t i = 0; i < size_; i++) {
            write(ebs_[i], c);
        }
    }

    void load(const uchar *&c, size_t &remaining_length) override {
        size_t n;
        read(n, c, remaining_length);
        size_ = n;
        loaded_ebs_.resize(n);
        for (size_t i = 0; i < n; i++) {
            read(loaded_ebs_[i], c, remaining_length);
        }
        ebs_ = loaded_ebs_.data();
        pos_ = 0;
    }

private:
    const double *ebs_;           // external double[] pointer (from conf.ebs)
    std::vector<double> loaded_ebs_;  // owned copy for load
    size_t size_;
    size_t pos_;
};

}  // namespace SZ3

#endif
