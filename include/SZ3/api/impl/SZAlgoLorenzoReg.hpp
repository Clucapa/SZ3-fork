#ifndef SZ3_SZALGO_LORENZO_REG_HPP
#define SZ3_SZALGO_LORENZO_REG_HPP

#include <cmath>
#include <memory>

#include "SZ3/compressor/SZGenericCompressor.hpp"
#include "SZ3/decomposition/BlockwiseDecomposition.hpp"
#include "SZ3/decomposition/QpetBlockDecomp.hpp"
#include "SZ3/def.hpp"
#include "SZ3/encoder/HuffmanEncoder.hpp"
#include "SZ3/lossless/Lossless_zstd.hpp"
#include "SZ3/predictor/ComposedPredictor.hpp"
#include "SZ3/predictor/LorenzoPredictor.hpp"
#include "SZ3/predictor/RegressionPredictor.hpp"
#include "SZ3/qoi/QoIIf.hpp"
#include "SZ3/quantizer/LinearQuantizer.hpp"
#include "SZ3/quantizer/QpetQnt.hpp"
#include "SZ3/utils/Config.hpp"
#include "SZ3/utils/Iterator.hpp"
#include "SZ3/utils/QuantOptimization.hpp"
#include "SZ3/utils/Statistic.hpp"

namespace SZ3 {
template <class T, uint N, class Quantizer, class Encoder, class Lossless>
std::shared_ptr<concepts::CompressorInterface<T>> make_compressor_lorenzo_regression(const Config &conf,
                                                                                     Quantizer quantizer,
                                                                                     Encoder encoder,
                                                                                     Lossless lossless) {
    std::vector<std::shared_ptr<concepts::PredictorInterface<T, N>>> predictors;

    int methodCnt = (conf.lorenzo + conf.lorenzo2 + conf.regression);
    int use_single_predictor = (methodCnt == 1);
    if (methodCnt == 0) {
        throw std::invalid_argument("All lorenzo and regression methods are disabled.");
    }
    if (conf.lorenzo) {
        if (use_single_predictor) {
            return make_compressor_sz_generic<T, N>(
                make_decomposition_blockwise<T, N>(conf, LorenzoPredictor<T, N, 1>(conf.absErrorBound), quantizer),
                encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<LorenzoPredictor<T, N, 1>>(conf.absErrorBound));
        }
    }
    if (conf.lorenzo2) {
        if (use_single_predictor) {
            return make_compressor_sz_generic<T, N>(
                make_decomposition_blockwise<T, N>(conf, LorenzoPredictor<T, N, 2>(conf.absErrorBound), quantizer),
                encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<LorenzoPredictor<T, N, 2>>(conf.absErrorBound));
        }
    }
    if (conf.regression) {
        if (use_single_predictor) {
            return make_compressor_sz_generic<T, N>(
                make_decomposition_blockwise<T, N>(conf, RegressionPredictor<T, N>(conf.blockSize, conf.absErrorBound),
                                                   quantizer),
                encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<RegressionPredictor<T, N>>(conf.blockSize, conf.absErrorBound));
        }
    }

    return make_compressor_sz_generic<T, N>(
        make_decomposition_blockwise<T, N>(conf, ComposedPredictor<T, N>(predictors), quantizer), encoder, lossless);
}

template <class T, uint N>
size_t SZ_compress_LorenzoReg(Config &conf, T *data, uchar *cmpData, size_t cmpCap) {
    assert(N == conf.N);
    assert(conf.cmprAlgo == ALGO_LORENZO_REG);
    calAbsErrorBound(conf, data);

    auto qoi     = GetQOI<T, N>(conf);
    auto qnt     = QpetQnt<T>(conf.quantbinCnt / 2, conf.qEBase, conf.qELogB,
                               conf.qR, conf.absErrorBound);
    auto encoder = HuffmanEncoder<int>();
    auto lossless = Lossless_zstd();

    int methodCnt = (conf.lorenzo + conf.lorenzo2 + conf.regression);
    int single = (methodCnt == 1);
    if (methodCnt == 0) {
        throw std::invalid_argument("All lorenzo and regression methods are disabled.");
    }

    if (conf.lorenzo && single) {
        auto decomp = QpetBlockDecomp<T, N, LorenzoPredictor<T, N, 1>, QpetQnt<T>>(
                          conf, LorenzoPredictor<T, N, 1>(conf.absErrorBound), qnt, qoi);
        return make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->compress(conf, data, cmpData, cmpCap);
    }
    if (conf.lorenzo2 && single) {
        auto decomp = QpetBlockDecomp<T, N, LorenzoPredictor<T, N, 2>, QpetQnt<T>>(
                          conf, LorenzoPredictor<T, N, 2>(conf.absErrorBound), qnt, qoi);
        return make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->compress(conf, data, cmpData, cmpCap);
    }
    if (conf.regression && single) {
        auto decomp = QpetBlockDecomp<T, N, RegressionPredictor<T, N>, QpetQnt<T>>(
                          conf, RegressionPredictor<T, N>(conf.blockSize, conf.absErrorBound), qnt, qoi);
        return make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->compress(conf, data, cmpData, cmpCap);
    }

    // composed predictor
    std::vector<std::shared_ptr<concepts::PredictorInterface<T, N>>> preds;
    if (conf.lorenzo)   preds.push_back(std::make_shared<LorenzoPredictor<T, N, 1>>(conf.absErrorBound));
    if (conf.lorenzo2)  preds.push_back(std::make_shared<LorenzoPredictor<T, N, 2>>(conf.absErrorBound));
    if (conf.regression) preds.push_back(std::make_shared<RegressionPredictor<T, N>>(conf.blockSize, conf.absErrorBound));
    auto decomp = QpetBlockDecomp<T, N, ComposedPredictor<T, N>, QpetQnt<T>>(
                      conf, ComposedPredictor<T, N>(preds), qnt, qoi);
    return make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->compress(conf, data, cmpData, cmpCap);
}

template <class T, uint N>
void SZ_decompress_LorenzoReg(const Config &conf, const uchar *cmpData,
                               size_t cmpSize, T *decData) {
    assert(conf.cmprAlgo == ALGO_LORENZO_REG);

    auto qoi     = GetQOI<T, N>(conf);
    auto qnt     = QpetQnt<T>(conf.quantbinCnt / 2, conf.qEBase, conf.qELogB,
                               conf.qR, conf.absErrorBound);
    auto encoder = HuffmanEncoder<int>();
    auto lossless = Lossless_zstd();

    bool lorenzo   = conf.lorenzo;
    bool lorenzo2  = conf.lorenzo2;
    bool regression = conf.regression;
    int methodCnt = (lorenzo + lorenzo2 + regression);
    int single = (methodCnt == 1);
    if (methodCnt == 0) {
        lorenzo = true;
        methodCnt = 1; single = 1;
    }

    if (lorenzo && single) {
        auto decomp = QpetBlockDecomp<T, N, LorenzoPredictor<T, N, 1>, QpetQnt<T>>(
                          conf, LorenzoPredictor<T, N, 1>(conf.absErrorBound), qnt, qoi);
        make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->decompress(conf, cmpData, cmpSize, decData);
        return;
    }
    if (lorenzo2 && single) {
        auto decomp = QpetBlockDecomp<T, N, LorenzoPredictor<T, N, 2>, QpetQnt<T>>(
                          conf, LorenzoPredictor<T, N, 2>(conf.absErrorBound), qnt, qoi);
        make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->decompress(conf, cmpData, cmpSize, decData);
        return;
    }
    if (regression && single) {
        auto decomp = QpetBlockDecomp<T, N, RegressionPredictor<T, N>, QpetQnt<T>>(
                          conf, RegressionPredictor<T, N>(conf.blockSize, conf.absErrorBound), qnt, qoi);
        make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->decompress(conf, cmpData, cmpSize, decData);
        return;
    }

    std::vector<std::shared_ptr<concepts::PredictorInterface<T, N>>> preds;
    if (lorenzo)   preds.push_back(std::make_shared<LorenzoPredictor<T, N, 1>>(conf.absErrorBound));
    if (lorenzo2)  preds.push_back(std::make_shared<LorenzoPredictor<T, N, 2>>(conf.absErrorBound));
    if (regression) preds.push_back(std::make_shared<RegressionPredictor<T, N>>(conf.blockSize, conf.absErrorBound));
    auto decomp = QpetBlockDecomp<T, N, ComposedPredictor<T, N>, QpetQnt<T>>(
                      conf, ComposedPredictor<T, N>(preds), qnt, qoi);
    make_compressor_sz_generic<T, N>(decomp, encoder, lossless)->decompress(conf, cmpData, cmpSize, decData);
}

}  // namespace SZ3

#endif
