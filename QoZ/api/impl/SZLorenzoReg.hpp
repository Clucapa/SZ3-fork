#ifndef SZ3_SZ_LORENZO_REG_HPP
#define SZ3_SZ_LORENZO_REG_HPP

#include "QoZ/compressor/SZGeneralCompressor.hpp"
#include "QoZ/frontend/SZFastFrontend.hpp"
#include "QoZ/frontend/SZGeneralFrontend.hpp"
#include "QoZ/frontend/SZQoIFrontend.hpp"
#include "QoZ/quantizer/IntegerQuantizer.hpp"
#include "QoZ/quantizer/QoIIntegerQuantizer.hpp"
#include "QoZ/predictor/ComposedPredictor.hpp"
#include "QoZ/predictor/LorenzoPredictor.hpp"
#include "QoZ/predictor/RegressionPredictor.hpp"
#include "QoZ/predictor/PolyRegressionPredictor.hpp"
#include "QoZ/predictor/ZeroPredictor.hpp"
#include "QoZ/encoder/QoIEncoder.hpp"
#include "QoZ/lossless/Lossless_zstd.hpp"
//#include "QoZ/qoi/XSquare.hpp"
#include "QoZ/qoi/QoIInfo.hpp"
#include "QoZ/utils/Iterator.hpp"
#include "QoZ/utils/Statistic.hpp"
#include "QoZ/utils/Extraction.hpp"
#include "QoZ/utils/QuantOptimization.hpp"
#include "QoZ/utils/Config.hpp"
#include "QoZ/def.hpp"

#include <cmath>
#include <cstdlib>
#include <memory>


template<class T, QoZ::uint N, class Quantizer, class Encoder, class Lossless>
std::shared_ptr<QoZ::concepts::CompressorInterface<T>>
make_lorenzo_regression_compressor(const QoZ::Config &conf, Quantizer quantizer, Encoder encoder, Lossless lossless) {
    std::vector<std::shared_ptr<QoZ::concepts::PredictorInterface<T, N>>> predictors;

    int methodCnt = (conf.lorenzo + conf.lorenzo2 + conf.regression + conf.regression2);
    int use_single_predictor = (methodCnt == 1);
    if (methodCnt == 0) {
        printf("All lorenzo and regression methods are disabled.\n");
        exit(0);
    }
    if (conf.lorenzo) {
        
        if (use_single_predictor) {
            return QoZ::make_sz_general_compressor<T, N>(
                    QoZ::make_sz_general_frontend<T, N>(conf, QoZ::LorenzoPredictor<T, N, 1>(conf.absErrorBound), quantizer),
                    encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<QoZ::LorenzoPredictor<T, N, 1>>(conf.absErrorBound));
        }
    }
    if (conf.lorenzo2) {
       
        if (use_single_predictor) {
            return QoZ::make_sz_general_compressor<T, N>(
                    QoZ::make_sz_general_frontend<T, N>(conf, QoZ::LorenzoPredictor<T, N, 2>(conf.absErrorBound), quantizer),
                    encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<QoZ::LorenzoPredictor<T, N, 2>>(conf.absErrorBound));
        }
    }
    if (conf.regression) {
        if (use_single_predictor) {
            return QoZ::make_sz_general_compressor<T, N>(
                    QoZ::make_sz_general_frontend<T, N>(conf, QoZ::RegressionPredictor<T, N>(conf.blockSize, conf.absErrorBound),
                                                       quantizer), encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<QoZ::RegressionPredictor<T, N>>(conf.blockSize, conf.absErrorBound));
        }
    }

    if (conf.regression2) {
        if (use_single_predictor) {
            return QoZ::make_sz_general_compressor<T, N>(
                    QoZ::make_sz_general_frontend<T, N>(conf, QoZ::PolyRegressionPredictor<T, N>(conf.blockSize, conf.absErrorBound),
                                                       quantizer), encoder, lossless);
        } else {
            predictors.push_back(std::make_shared<QoZ::PolyRegressionPredictor<T, N>>(conf.blockSize, conf.absErrorBound));
        }
    }
    return QoZ::make_sz_general_compressor<T, N>(
            QoZ::make_sz_general_frontend<T, N>(conf, QoZ::ComposedPredictor<T, N>(predictors),
                                               quantizer), encoder, lossless);
}


template<class T, QoZ::uint N, class Quantizer, class Quantizer_EB>
std::shared_ptr<QoZ::concepts::CompressorInterface<T>>
make_qoi_lorenzo_compressor(const QoZ::Config &conf, std::shared_ptr<QoZ::concepts::QoIInterface<T, N>> qoi, Quantizer quantizer, Quantizer_EB quantizer_eb) {

    quantizer.clear();
    quantizer_eb.clear();
    //std::shared_ptr<QoZ::concepts::CompressorInterface<T>> sz;

    int methodCnt = (conf.lorenzo + conf.lorenzo2);
    int use_single_predictor = (methodCnt == 1);

    if(use_single_predictor){
        if(conf.lorenzo){
            return QoZ::make_sz_general_compressor<T, N>(QoZ::make_sz_qoi_frontend<T, N>(conf, QoZ::LorenzoPredictor<T, N, 1>(conf.absErrorBound), quantizer, quantizer_eb, qoi),
                                                    QoZ::QoIEncoder<int>(), QoZ::Lossless_zstd());
        }
        else if(conf.lorenzo2){
            return QoZ::make_sz_general_compressor<T, N>(QoZ::make_sz_qoi_frontend<T, N>(conf, QoZ::LorenzoPredictor<T, N, 2>(conf.absErrorBound), quantizer, quantizer_eb, qoi),
                                                    QoZ::QoIEncoder<int>(), QoZ::Lossless_zstd());
        }
    }
    //else{
        std::vector<std::shared_ptr<QoZ::concepts::PredictorInterface<T, N>>> predictors;
        predictors.push_back(std::make_shared<QoZ::LorenzoPredictor<T, N, 1>>(conf.absErrorBound));
        predictors.push_back(std::make_shared<QoZ::LorenzoPredictor<T, N, 2>>(conf.absErrorBound));
        return QoZ::make_sz_general_compressor<T, N>(QoZ::make_sz_qoi_frontend<T, N>(conf, QoZ::ComposedPredictor<T, N>(predictors), quantizer, quantizer_eb, qoi),
                                                QoZ::QoIEncoder<int>(), QoZ::Lossless_zstd());
    //}
    //return sz;
    //return NULL;
}

template<class T, QoZ::uint N>
char *SZ_compress_LorenzoReg(QoZ::Config &conf, T *data, size_t &outSize, bool tuning=false) {

    assert(N == conf.N);
    assert(conf.cmprAlgo == QoZ::ALGO_LORENZO_REG);
    //QoZ::calAbsErrorBound(conf, data);
    //std::cout<<"ABSEB "<<conf.absErrorBound<<std::endl;
    char *cmpData;

    if(conf.qoi > 0 and !conf.use_global_eb){
        //std::cout << "absErrorBound = " << conf.absErrorBound << std::endl;
        //std::cout << conf.qoi << " " << conf.qoiEB << " " << conf.qoiEBBase << " " << conf.qoiEBLogBase << " " << conf.qoiQuantbinCnt << " " << conf.qoiRegionSize << std::endl;
        auto quantizer = QoZ::VariableEBLinearQuantizer<T, T>(conf.quantbinCnt / 2);
        auto quantizer_eb = QoZ::EBLogQuantizer<T>(conf.qoiEBBase, conf.qoiEBLogBase, conf.qoiQuantbinCnt / 2, conf.absErrorBound);
        auto qoi = QoZ::GetQOI<T, N>(conf);
        if(conf.qoi == 3){
            conf.blockSize = conf.qoiRegionSize;
        }
        //std::cout<<"1"<<std::e
        auto sz = make_qoi_lorenzo_compressor(conf, qoi, quantizer, quantizer_eb);
        cmpData = (char *) sz->compress(conf, data, outSize);
        return cmpData;
    }


    auto quantizer = QoZ::LinearQuantizer<T>(conf.absErrorBound, conf.quantbinCnt / 2);

    if (N == 3 and !conf.regression2 and conf.qoi==0) {
        // use fast version for 3D
        auto sz = QoZ::make_sz_general_compressor<T, N>(QoZ::make_sz_fast_frontend<T, N>(conf, quantizer), QoZ::HuffmanEncoder<int>(),
                                                       QoZ::Lossless_zstd());
        cmpData = (char *) sz->compress(conf, data, outSize);
    } else {
        auto sz = make_lorenzo_regression_compressor<T, N>(conf, quantizer, QoZ::HuffmanEncoder<int>(), QoZ::Lossless_zstd());
        //std::cout<<"lor1"<<std::endl;
        cmpData = (char *) sz->compress(conf, data, outSize);
        if(conf.qoi>0 and !tuning)
            conf.qoi = 99;
    }
    
    return cmpData;
}


template<class T, QoZ::uint N>
void SZ_decompress_LorenzoReg(const QoZ::Config &theconf, char *cmpData, size_t cmpSize, T *decData) {

    QoZ::Config conf(theconf);
    //std::cout<<"ABSEB "<<conf.absErrorBound<<std::endl;
    assert(conf.cmprAlgo == QoZ::ALGO_LORENZO_REG);
    QoZ::uchar const *cmpDataPos = (QoZ::uchar *) cmpData;

    if(conf.qoi > 0 and conf.qoi != 99){
        //std::cout << conf.qoi << " " << conf.qoiEB << " " << conf.qoiEBBase << " " << conf.qoiEBLogBase << " " << conf.qoiQuantbinCnt << " " << conf.qoiRegionSize << std::endl;
        auto quantizer = QoZ::VariableEBLinearQuantizer<T, T>(conf.quantbinCnt / 2);
        auto quantizer_eb = QoZ::EBLogQuantizer<T>(conf.qoiEBBase, conf.qoiEBLogBase, conf.qoiQuantbinCnt / 2, conf.absErrorBound);
        auto qoi = QoZ::GetQOI<T, N>(conf);
        auto sz = make_qoi_lorenzo_compressor(conf, qoi, quantizer, quantizer_eb);
        sz->decompress(cmpDataPos, cmpSize, decData);
        return;
    }  


    QoZ::LinearQuantizer<T> quantizer;
  
        
    if (N == 3 and !conf.regression2 and conf.qoi !=99) {
        // use fast version for 3D
        auto sz = QoZ::make_sz_general_compressor<T, N>(QoZ::make_sz_fast_frontend<T, N>(conf, quantizer),
                                                       QoZ::HuffmanEncoder<int>(), QoZ::Lossless_zstd());
        sz->decompress(cmpDataPos, cmpSize, decData);
       
    } else {
        auto sz = make_lorenzo_regression_compressor<T, N>(conf, quantizer, QoZ::HuffmanEncoder<int>(), QoZ::Lossless_zstd());
        sz->decompress(cmpDataPos, cmpSize, decData);
       
    }
    
    




}

#endif