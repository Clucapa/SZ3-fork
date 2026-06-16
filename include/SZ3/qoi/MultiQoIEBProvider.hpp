#ifndef SZ3_QOI_MULTI_EBPROVIDER_HPP
#define SZ3_QOI_MULTI_EBPROVIDER_HPP

#include <memory>
#include <vector>
#include <algorithm>
#include "SZ3/def.hpp"
#include "SZ3/qoi/EBProvider.hpp"
#include "SZ3/utils/MemoryUtil.hpp"

namespace SZ3 {

template <typename T>
class MultiQoIEBProvider : public concepts::EBProvider<T> {
public:
    MultiQoIEBProvider(std::vector<std::unique_ptr<concepts::EBProvider<T>>> providers)
        : providers_(std::move(providers)) {}

    void precompress_block(size_t num_elements) override {
        for (auto &p : providers_)
            p->precompress_block(num_elements);
    }

    void postcompress_block() override {
        for (auto &p : providers_)
            p->postcompress_block();
    }

    T advance(T orig, T dec) override {
        T eb = providers_[0]->advance(orig, dec);
        for (size_t i = 1; i < providers_.size(); ++i)
            eb = std::min(eb, providers_[i]->advance(orig, dec));
        return eb;
    }

    void advance() override {
        for (auto &p : providers_)
            p->advance();
    }

    void reset() override {
        for (auto &p : providers_)
            p->reset();
    }

    void save(uchar *&c) const override {
        size_t sz = providers_.size();
        write(sz, c);
        for (auto &p : providers_)
            p->save(c);
    }

    void load(const uchar *&c, size_t &remaining_length) override {
        size_t sz = 0;
        read(sz, c, remaining_length);
        providers_.resize(sz);
        for (auto &p : providers_)
            p->load(c, remaining_length);
    }

private:
    std::vector<std::unique_ptr<concepts::EBProvider<T>>> providers_;
};

}  // namespace SZ3

#endif
