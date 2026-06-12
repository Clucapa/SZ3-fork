#ifndef SZ3_EB_PROVIDER_HPP
#define SZ3_EB_PROVIDER_HPP

#include "SZ3/def.hpp"

namespace SZ3::concepts {

template <typename T>
class EBProvider {
public:
    virtual ~EBProvider() = default;

    virtual void precompress_block(size_t num_elements) = 0;

    virtual T advance(T orig, T dec) = 0;

    virtual void advance() = 0;

    virtual void postcompress_block() = 0;

    virtual void reset() = 0;

    virtual void save(uchar *&c) const = 0;
    virtual void load(const uchar *&c, size_t &remaining_length) = 0;
};

}  // namespace SZ3::concepts

#endif
