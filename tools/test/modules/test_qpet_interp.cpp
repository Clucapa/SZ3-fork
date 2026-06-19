#include <cmath>
#include <vector>
#include <cstring>

#include "SZ3/decomposition/QpetInterpDecomp.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIIf.hpp"
#include "SZ3/qoi/QoI_RegionalAvgInterp.hpp"
#include "SZ3/qoi/QoI_RegionalMeanSqInterp.hpp"
#include "SZ3/quantizer/QpetQnt.hpp"
#include "SZ3/utils/Config.hpp"
#include "SZ3/utils/Interpolators.hpp"
#include "gtest/gtest.h"

using namespace SZ3;

// ---- QoI Unit Tests ----

TEST(QoI_RegionalAvgInterp, ReturnsGeb) {
    QoI_RegionalAvgInterp<float, 1> qoi(0.01, 0.1f);
    EXPECT_EQ(qoi.id, ~2);
    EXPECT_FLOAT_EQ(qoi.interpret_eb(42.0f), 0.1f);
    EXPECT_TRUE(qoi.check_comply(1.0f, 0.99f));
}

TEST(QoI_RegionalAvgInterp, UpdateToleranceNoOp) {
    QoI_RegionalAvgInterp<float, 1> qoi(0.01, 1.0f);
    qoi.update_tolerance(1.0f, 0.5f);
    EXPECT_FLOAT_EQ(qoi.interpret_eb(1.0f), 1.0f);
}

TEST(QoI_RegionalMeanSqInterp, ReturnsGeb) {
    QoI_RegionalMeanSqInterp<float, 1> qoi(1.0, 10.0f);
    EXPECT_EQ(qoi.id, ~3);
    qoi.precompress_block(10);
    float eb = qoi.interpret_eb(3.0f);
    EXPECT_GT(eb, 0.0f);
    EXPECT_LE(eb, 10.0f);
}

TEST(QoI_RegionalMeanSqInterp, UpdateToleranceNoOp) {
    QoI_RegionalMeanSqInterp<float, 1> qoi(1.0, 5.0f);
    qoi.precompress_block(10);
    float eb_before = qoi.interpret_eb(2.0f);
    EXPECT_LT(eb_before, 5.0f);  // budget tracking produces smaller eb
    qoi.update_tolerance(2.0f, 1.0f);  // introduce larger error
    float eb_after = qoi.interpret_eb(2.0f);
    EXPECT_LT(eb_after, eb_before);  // budget consumed, eb should shrink
}

// ---- EBProvider Tests ----

TEST(RegionalAvgInterpEBProvider, AdvanceReturnsGeb) {
    QoI_RegionalAvgInterp<float, 1> qoi(0.1, 2.0f);
    RegionalAvgInterpEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    float eb = provider.advance(3.0f, 2.9f);
    EXPECT_FLOAT_EQ(eb, 2.0f);
    provider.postcompress_block();
}

TEST(RegionalAvgInterpEBProvider, DecompressAdvance) {
    QoI_RegionalAvgInterp<float, 1> qoi(0.1, 2.0f);
    RegionalAvgInterpEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    provider.advance(1.0f, 1.0f);
    provider.advance(2.0f, 2.0f);
    provider.advance();
    provider.advance();
    EXPECT_NO_FATAL_FAILURE(provider.postcompress_block());
}

TEST(RegionalAvgInterpEBProvider, SaveLoadEmpty) {
    QoI_RegionalAvgInterp<float, 1> qoi(0.1, 1.0f);
    RegionalAvgInterpEBProvider<float, 1> provider(&qoi);

    std::vector<unsigned char> buf(64);
    unsigned char *p = buf.data();
    provider.save(p);
    size_t saved = p - buf.data();
    EXPECT_EQ(saved, 0u);
}

TEST(RegionalAvgInterpEBProvider, Reset) {
    QoI_RegionalAvgInterp<float, 1> qoi(0.1, 1.0f);
    RegionalAvgInterpEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    provider.advance(1.0f, 1.0f);
    provider.reset();
    provider.precompress_block(10);
    float eb = provider.advance(1.0f, 1.0f);
    EXPECT_FLOAT_EQ(eb, 1.0f);
}

TEST(RegionalMeanSqInterpEBProvider, AdvanceReturnsGeb) {
    QoI_RegionalMeanSqInterp<float, 1> qoi(1.0, 5.0f);
    RegionalMeanSqInterpEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(5);
    float eb = provider.advance(2.0f, 1.9f);
    EXPECT_GT(eb, 0.0f);
    EXPECT_LE(eb, 5.0f);  // capped by geb
}

// ---- Decomposition: 1D Interp Compress/Decompress Round-Trip ----

template <class T, uint N>
struct InterpTestConfig {
    std::array<size_t, N> dims;
    uint8_t interpAlgo = INTERP_ALGO_LINEAR;
    int interpDirection = 0;
    int interpAnchorStride = 0;
    double interpAlpha = -1;
    double interpBeta = -1;
    int quantbinCnt = 65536;
    double qEB = 0.01;
    int qR = 12;
};

template <class T, uint N>
Config make_interp_config(const InterpTestConfig<T, N> &tc, int regional_id) {
    Config conf;
    conf.setDims(tc.dims.begin(), tc.dims.end());
    conf.cmprAlgo = ALGO_INTERP;
    conf.interpAlgo = tc.interpAlgo;
    conf.interpDirection = tc.interpDirection;
    conf.interpAnchorStride = tc.interpAnchorStride;
    conf.interpAlpha = tc.interpAlpha;
    conf.interpBeta = tc.interpBeta;
    conf.quantbinCnt = tc.quantbinCnt;
    conf.absErrorBound = tc.qEB;
    conf.qEB = tc.qEB;
    conf.qR = tc.qR;
    conf.qoi = ~regional_id;
    return conf;
}

TEST(QpetInterpDecomp, CompressDecompress1D) {
    constexpr uint N = 1;
    InterpTestConfig<float, N> tc;
    tc.dims = {64};
    tc.interpAnchorStride = 0;
    tc.qEB = 0.1;

    size_t num = tc.dims[0];
    std::vector<float> data(num);
    for (size_t i = 0; i < num; i++)
        data[i] = static_cast<float>(i % 7) * 0.5f;

    auto conf = make_interp_config<float, N>(tc, 2);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto qis = decomp.compress(conf, data.data());

    std::vector<float> dec(num, 0.0f);
    auto confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    EXPECT_EQ(qis.size(), num * 2);
    for (size_t i = 0; i < num; i++) {
        EXPECT_NEAR(data[i], dec[i], static_cast<float>(tc.qEB) * 2.0f);
    }
}

TEST(QpetInterpDecomp, CompressDecompress2D) {
    constexpr uint N = 2;
    InterpTestConfig<float, N> tc;
    tc.dims = {16, 16};
    tc.interpAnchorStride = 0;
    tc.qEB = 0.1;

    size_t num = tc.dims[0] * tc.dims[1];
    std::vector<float> data(num);
    for (size_t i = 0; i < num; i++)
        data[i] = std::sin(static_cast<float>(i) * 0.1f);

    auto conf = make_interp_config<float, N>(tc, 2);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto qis = decomp.compress(conf, data.data());

    std::vector<float> dec(num, 0.0f);
    auto confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    EXPECT_EQ(qis.size(), num * 2);
    for (size_t i = 0; i < num; i++) {
        EXPECT_NEAR(data[i], dec[i], static_cast<float>(tc.qEB) * 4.0f);
    }
}

TEST(QpetInterpDecomp, CompressDecompress1DCubic) {
    constexpr uint N = 1;
    InterpTestConfig<float, N> tc;
    tc.dims = {128};
    tc.interpAlgo = INTERP_ALGO_CUBIC;
    tc.interpAnchorStride = 0;
    tc.qEB = 0.05;

    size_t num = tc.dims[0];
    std::vector<float> data(num);
    for (size_t i = 0; i < num; i++)
        data[i] = std::sin(static_cast<float>(i) * 0.05f) * 10.0f;

    auto conf = make_interp_config<float, N>(tc, 2);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto qis = decomp.compress(conf, data.data());

    std::vector<float> dec(num, 0.0f);
    auto confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    EXPECT_EQ(qis.size(), num * 2);
    for (size_t i = 0; i < num; i++) {
        EXPECT_NEAR(data[i], dec[i], static_cast<float>(tc.qEB) * 4.0f);
    }
}

TEST(QpetInterpDecomp, CompressDecompressWithAnchor) {
    constexpr uint N = 1;
    InterpTestConfig<float, N> tc;
    tc.dims = {64};
    tc.interpAnchorStride = 8;
    tc.qEB = 0.1;

    size_t num = tc.dims[0];
    std::vector<float> data(num);
    for (size_t i = 0; i < num; i++)
        data[i] = static_cast<float>(i % 13) * 0.7f;

    auto conf = make_interp_config<float, N>(tc, 2);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto qis = decomp.compress(conf, data.data());

    std::vector<float> dec(num, 0.0f);
    auto confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    EXPECT_EQ(qis.size(), num * 2);
    for (size_t i = 0; i < num; i++) {
        EXPECT_NEAR(data[i], dec[i], static_cast<float>(tc.qEB) * 4.0f);
    }
}

TEST(QpetInterpDecomp, AnchorPointsLossless) {
    constexpr uint N = 1;
    InterpTestConfig<float, N> tc;
    tc.dims = {16};
    tc.interpAnchorStride = 8;
    tc.qEB = 1.0;

    size_t num = tc.dims[0];
    std::vector<float> data(num);
    for (size_t i = 0; i < num; i++)
        data[i] = 100.0f + static_cast<float>(i) * 50.0f;

    auto conf = make_interp_config<float, N>(tc, 2);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto qis = decomp.compress(conf, data.data());

    std::vector<float> dec(num, 0.0f);
    auto confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    // anchor points at stride boundaries must be lossless
    for (size_t i = 0; i < num; i += tc.interpAnchorStride) {
        EXPECT_FLOAT_EQ(data[i], dec[i]);
    }
}

TEST(QpetInterpDecomp, ConsistentQebsQdsSize) {
    constexpr uint N = 1;
    for (size_t dim : {8u, 16u, 32u, 64u}) {
        InterpTestConfig<float, N> tc;
        tc.dims = {dim};
        tc.interpAnchorStride = 0;
        tc.qEB = 0.5;

        size_t num = dim;
        std::vector<float> data(num, 1.0f);

        auto conf = make_interp_config<float, N>(tc, 2);
        auto qoi = GetQOI<float, N>(conf);
        auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

        QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
        auto qis = decomp.compress(conf, data.data());

        EXPECT_EQ(qis.size(), num * 2)
            << "dim=" << dim << " expected " << num * 2 << " got " << qis.size();
    }
}

TEST(QpetInterpDecomp, QoIRegionalMeanSqInterpRoundTrip) {
    constexpr uint N = 1;
    InterpTestConfig<float, N> tc;
    tc.dims = {32};
    tc.interpAnchorStride = 0;
    tc.qEB = 0.5;

    size_t num = tc.dims[0];
    std::vector<float> data(num);
    for (size_t i = 0; i < num; i++)
        data[i] = static_cast<float>(i % 5) * 0.3f + 1.0f;

    auto conf = make_interp_config<float, N>(tc, 3);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto qis = decomp.compress(conf, data.data());

    std::vector<float> dec(num, 0.0f);
    auto confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    EXPECT_EQ(qis.size(), num * 2);
    for (size_t i = 0; i < num; i++) {
        EXPECT_NEAR(data[i], dec[i], static_cast<float>(tc.qEB) * 4.0f);
    }
}

TEST(QpetInterpDecomp, GetOutRange) {
    constexpr uint N = 1;
    InterpTestConfig<float, N> tc;
    tc.dims = {16};
    tc.qEB = 0.1;

    auto conf = make_interp_config<float, N>(tc, 2);
    auto qoi = GetQOI<float, N>(conf);
    auto qnt = QpetQnt<float>(conf.quantbinCnt / 2, 3.0, 0.2, conf.qR, conf.absErrorBound);

    QpetInterpDecomp<float, N, QpetQnt<float>> decomp(conf, qnt, qoi);
    auto range = decomp.get_out_range();
    EXPECT_GE(range.second, range.first);
}
