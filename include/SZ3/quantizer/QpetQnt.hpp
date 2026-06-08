#ifndef SZ3_QPET_QNT_HPP
#define SZ3_QPET_QNT_HPP

#include <vector>
#include "SZ3/def.hpp"
#include "SZ3/quantizer/Quantizer.hpp"
#include "SZ3/quantizer/EBLogQnt.hpp"
#include "SZ3/utils/MemoryUtil.hpp"

namespace SZ3 {

template <class T>
class QpetQnt : public concepts::QpetQntIf<T, int> {
public:
    QpetQnt() : rd(32768), eb_log() {}

    QpetQnt(int rd, double ebase, double elbase, int er, T geb)
        : rd(rd), eb_log(ebase, elbase, er, geb) {}

    // ---------- QuantizerIf 兼容层 ----------
    ALWAYS_INLINE int quantize_and_overwrite(T &, T) override { return 0; }

    ALWAYS_INLINE T recover(T, int) override { return 0; }

    ALWAYS_INLINE int force_save_unpred(T ori) override {
        unpred.push_back(ori);
        return 0;
    }

    // ---------- QpetQntIf 核心 ----------
    ALWAYS_INLINE int qnt_overwrite(T &d, T pred, T eb) override {
        if (eb == 0) {
            unpred.push_back(d);
            buf_eb.push_back(0);
            buf_d.push_back(0);
            return 0;
        }
        int qe = eb_log.qnt_overwrite(eb);  // ① log 有损变换, eb 被覆写
        buf_eb.push_back(qe);

        T diff = d - pred;
        auto qi64 = static_cast<int64_t>(fabs(diff) / eb) + 1;
        if (qi64 < rd * 2) {
            int qi = static_cast<int>(qi64);
            qi >>= 1;
            int hi = qi;
            qi <<= 1;
            int qs;
            if (diff < 0) {
                qi = -qi;
                qs = rd - hi;
            } else {
                qs = rd + hi;
            }
            T dec = pred + qi * eb;
            if (fabs(dec - d) > eb) {
                unpred.push_back(d);
                buf_d.push_back(0);
                return 0;
            }
            d = dec;
            buf_d.push_back(qs);
            return qs;
        }
        unpred.push_back(d);
        buf_d.push_back(0);
        return 0;
    }

    ALWAYS_INLINE T recv(T pred, int qi, T eb) override {
        if (qi) return pred + 2 * (qi - rd) * eb;
        return recv_unpred();
    }

    ALWAYS_INLINE T recv_eb(int qe) override {
        return eb_log.recv_eb(qe);
    }

    void flush(std::vector<int> &out) override {
        out.insert(out.end(), buf_eb.begin(), buf_eb.end());
        out.insert(out.end(), buf_d.begin(), buf_d.end());
        buf_eb.clear();
        buf_d.clear();
    }

    void clear_bufs() override {
        buf_eb.clear();
        buf_d.clear();
    }

    std::pair<int, int> get_out_range() const override {
        auto r = eb_log.get_out_range();
        return std::make_pair(0, std::max(rd * 2, r.second));
    }

    void precompress_data() override { clear_bufs(); }

    void postcompress_data() override {}

    void predecompress_data() override { clear_bufs(); }

    void postdecompress_data() override {}

    void save(uchar *&c) const override {
        write(uid, c);
        write(rd, c);
        size_t sz = unpred.size();
        write(sz, c);
        if (sz > 0) write(unpred.data(), sz, c);
        eb_log.save(c);
    }

    void load(const uchar *&c, size_t &rl) override {
        uchar rid;
        read(rid, c, rl);
        read(rd, c, rl);
        size_t sz = 0;
        read(sz, c, rl);
        if (sz > 0) {
            unpred.resize(sz);
            read(unpred.data(), sz, c, rl);
        }
        eb_log.load(c, rl);
        idx = 0;
    }

    size_t size_est() { return unpred.size() * sizeof(T); }

private:
    ALWAYS_INLINE T recv_unpred() { return unpred[idx++]; }

    uchar uid = 0b0011;
    int rd;
    EBLogQnt<T> eb_log;
    std::vector<T> unpred;
    std::vector<int> buf_eb;
    std::vector<int> buf_d;
    size_t idx = 0;
};

}  // namespace SZ3

#endif
