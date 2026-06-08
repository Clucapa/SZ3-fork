#ifndef SZ3_QPET_BLOCK_DECOMP_HPP
#define SZ3_QPET_BLOCK_DECOMP_HPP

#include <cstring>
#include "Decomposition.hpp"
#include "SZ3/def.hpp"
#include "SZ3/predictor/LorenzoPredictor.hpp"
#include "SZ3/predictor/Predictor.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/quantizer/Quantizer.hpp"
#include "SZ3/utils/Config.hpp"
#include "SZ3/utils/FileUtil.hpp"
#include "SZ3/utils/BlockwiseIterator.hpp"

namespace SZ3 {

template <class T, uint N, class Pred, class Qnt>
class QpetBlockDecomp : public concepts::DecompositionInterface<T, int, N> {
public:
    using Block_iter = typename block_data<T, N>::block_iterator;

    QpetBlockDecomp(const Config &conf, Pred pred, Qnt qnt,
                    std::shared_ptr<concepts::QoIIf<T, N>> qoi)
        : pred(pred), qnt(qnt), qoi(qoi),
          fallback_pred(conf.absErrorBound) {
        static_assert(std::is_base_of<concepts::PredictorInterface<T, N>, Pred>::value,
                      "must implement the Predictor interface");
        static_assert(std::is_base_of<concepts::QpetQntIf<T, int>, Qnt>::value,
                      "must implement the QpetQntIf interface");
    }

    std::vector<int> compress(const Config &conf, T *data) override {
        auto dpad = std::make_shared<block_data<T, N>>(data, conf.dims, pred.get_padding(), true);
        auto blk = dpad->block_iter(conf.blockSize);
        std::vector<int> qis;
        qis.reserve(conf.num * 2);
        size_t off = 0;

        do {
            concepts::PredictorInterface<T, N> *pwb = &pred;
            if (!pred.precompress(blk)) pwb = &fallback_pred;
            pwb->precompress_block_commit();

            Block_iter::foreach (blk, [&](T *c, const std::array<size_t, N> &ix) {
                T eb = conf.ebs[off++];
                T prd = pwb->predict(blk, c, ix);
                T ori = *c;
                qnt.qnt_overwrite(*c, prd, eb);

                if (!qoi->check_comply(ori, *c)) {
                    *c = ori;
                    qnt.qnt_overwrite(*c, static_cast<T>(0), static_cast<T>(0));
                }
            });

            qnt.flush(qis);
        } while (blk.next());

        return qis;
    }

    T *decompress(const Config &conf, std::vector<int> &qis, T *dec) override {
        int *qeb = &qis[0];
        int *qd  = &qis[conf.num];

        auto dpad = std::make_shared<block_data<T, N>>(dec, conf.dims, pred.get_padding(), false);
        auto blk = dpad->block_iter(conf.blockSize);
        do {
            concepts::PredictorInterface<T, N> *pwb = &pred;
            if (!pred.predecompress(blk)) pwb = &fallback_pred;
            Block_iter::foreach (blk, [&](T *c, const std::array<size_t, N> &ix) {
                T eb = qnt.recv_eb(*(qeb++));
                T prd = pwb->predict(blk, c, ix);
                *c = qnt.recv(prd, *(qd++), eb);
            });
        } while (blk.next());

        return dec;
    }

    void save(uchar *&c) override {
        fallback_pred.save(c);
        pred.save(c);
        qnt.save(c);
    }

    void load(const uchar *&c, size_t &rl) override {
        fallback_pred.load(c, rl);
        pred.load(c, rl);
        qnt.load(c, rl);
    }

    std::pair<int, int> get_out_range() override {
        return qnt.get_out_range();
    }

private:
    Pred pred;
    Qnt qnt;
    std::shared_ptr<concepts::QoIIf<T, N>> qoi;
    LorenzoPredictor<T, N, 1> fallback_pred;
};

}  // namespace SZ3

#endif
