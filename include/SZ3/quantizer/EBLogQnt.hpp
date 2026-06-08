#ifndef SZ3_EB_LOG_QNT_HPP
#define SZ3_EB_LOG_QNT_HPP

#include <cmath>
#include "SZ3/def.hpp"
#include "SZ3/quantizer/Quantizer.hpp"

namespace SZ3 {

template <class T>
class EBLogQnt : public concepts::QuantizerInterface<T, int> {
public:
    EBLogQnt() : base(1e-15), lbase(2), r(32), geb(1e-3) {}

    EBLogQnt(double base, double lbase, int r, T geb)
        : base(base), lbase(lbase), r(r), geb(geb) {}

    ALWAYS_INLINE int quantize_and_overwrite(T &data, T) override {
        return qnt_overwrite(data);
    }

    ALWAYS_INLINE int qnt_overwrite(T &eb) {
        if (eb <= base || eb >= geb) {
            eb = geb;
            return 0;
        }
        int id = static_cast<int>(log2(eb / base) / log2(lbase));
        if (id == 0) {
            eb = geb;
            return 0;
        }
        id = std::min(id, r);
        eb = pow(lbase, id) * base;
        return id;
    }

    ALWAYS_INLINE T recover(T, int qi) override {
        return recv_eb(qi);
    }

    ALWAYS_INLINE T recv_eb(int qi) {
        return qi ? pow(lbase, qi) * base : geb;
    }

    int force_save_unpred(T) override { return 0; }

    std::pair<int, int> get_out_range() const override {
        return std::make_pair(0, r);
    }

    void save(uchar *&c) const override {
        write(uid, c);
        write(base, c);
        write(lbase, c);
        write(r, c);
    }

    void load(const uchar *&c, size_t &rl) override {
        uchar rid;
        read(rid, c, rl);
        read(base, c, rl);
        read(lbase, c, rl);
        read(r, c, rl);
    }

private:
    uchar uid = 0b0100;
    double base;
    double lbase;
    int r;
    T geb;
};

}  // namespace SZ3

#endif
