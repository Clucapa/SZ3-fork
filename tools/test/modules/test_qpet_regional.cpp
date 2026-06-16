#include <cmath>
#include <vector>

#include "SZ3/qoi/EBProvider.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "SZ3/qoi/RegionalMean.hpp"
#include "SZ3/qoi/RegionalMeanSq.hpp"
#include "gtest/gtest.h"

TEST(RegionalMeanEBProvider, BasicBudgetTracking) {
    SZ3::QoI_RegionalMean<float, 1> qoi(0.01, 1.0);
    SZ3::RegionalMeanEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(100);
    float eb1 = provider.advance(1.0f, 0.995f);  // error += 0.005
    EXPECT_GE(eb1, 0.0f);
    EXPECT_LE(eb1, 1.0f);  // geb

    float eb2 = provider.advance(2.0f, 1.990f);  // error += 0.01, total 0.015
    EXPECT_GE(eb2, 0.0f);
    EXPECT_LE(eb2, 1.0f);
    provider.postcompress_block();
}

TEST(RegionalMeanEBProvider, GebCapping) {
    SZ3::QoI_RegionalMean<float, 1> qoi(0.1, 0.001);
    SZ3::RegionalMeanEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    float eb = provider.advance(1.0f, 1.0f);
    EXPECT_FLOAT_EQ(eb, 0.001f);  // capped by geb
}

TEST(RegionalMeanEBProvider, DecompressAdvance) {
    SZ3::QoI_RegionalMean<float, 1> qoi(0.01, 1.0);
    SZ3::RegionalMeanEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    provider.advance(1.0f, 0.99f);  // compress
    provider.advance(2.0f, 1.98f);
    provider.advance();             // decompress
    provider.advance();             // decompress
    EXPECT_NO_FATAL_FAILURE(provider.postcompress_block());
}

TEST(RegionalMeanEBProvider, SaveLoad) {
    SZ3::QoI_RegionalMean<float, 1> qoi(0.01, 1.0);
    SZ3::RegionalMeanEBProvider<float, 1> provider(&qoi);

    std::vector<unsigned char> buf(64);
    unsigned char *p = buf.data();
    provider.save(p);
    size_t saved = p - buf.data();
    EXPECT_EQ(saved, 0u);  // regional: no data to save
}

TEST(RegionalMeanEBProvider, Reset) {
    SZ3::QoI_RegionalMean<float, 1> qoi(0.01, 1.0);
    SZ3::RegionalMeanEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    float first = provider.advance(1.0f, 1.0f);

    provider.reset();
    provider.precompress_block(10);
    float afterReset = provider.advance(1.0f, 1.0f);

    EXPECT_FLOAT_EQ(first, afterReset);
}

TEST(RegionalMeanSqEBProvider, BasicBudgetWithFInverse) {
    SZ3::QoI_RegionalMeanSq<float, 1> qoi(1.0, 10.0);
    SZ3::RegionalMeanSqEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    float eb = provider.advance(3.0f, 2.995f);
    // eb_sq = 1.0*10/10 = 1.0
    // eb = -3.0 + sqrt(9 + 1) ≈ 0.1623
    EXPECT_NEAR(eb, 0.1623f, 1e-3);

    float eb2 = provider.advance(5.0f, 4.990f);
    // error += 3.0² - 2.995² ≈ 0.030
    // eb_sq = (10.0 - 0.030) / 9 ≈ 1.108
    // eb2 = -5.0 + sqrt(25 + 1.108) ≈ 0.110
    EXPECT_NEAR(eb2, 0.110f, 1e-3);
    provider.postcompress_block();
}

TEST(RegionalMeanSqEBProvider, EquivalentToX2ForSingleElement) {
    SZ3::QoI_RegionalMeanSq<float, 1> rmq(1.0, 10.0);
    rmq.precompress_block(1);
    float eb_rm = rmq.interpret_eb(3.0f);

    SZ3::QoI_X2<float, 1> x2(1.0, 10.0f);
    float eb_x2 = x2.interpret_eb(3.0f);

    EXPECT_FLOAT_EQ(eb_rm, eb_x2);
}

TEST(RegionalMeanSqEBProvider, GebCapping) {
    SZ3::QoI_RegionalMeanSq<float, 1> qoi(100.0, 0.001);
    SZ3::RegionalMeanSqEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(10);
    float eb = provider.advance(1.0f, 0.999f);
    EXPECT_FLOAT_EQ(eb, 0.001f);  // capped by small geb
}

TEST(RegionalMeanSqEBProvider, NegativeValues) {
    SZ3::QoI_RegionalMeanSq<float, 1> qoi(1.0, 10.0);
    SZ3::RegionalMeanSqEBProvider<float, 1> provider(&qoi);

    provider.precompress_block(1);
    float eb_neg = provider.advance(-3.0f, -3.0f);

    SZ3::QoI_RegionalMeanSq<float, 1> qoi2(1.0, 10.0);
    qoi2.precompress_block(1);
    float eb_pos = qoi2.interpret_eb(3.0f);

    // |fabs(-3)| = |fabs(3)|, so eb for x^2 should be symmetric
    EXPECT_FLOAT_EQ(eb_neg, eb_pos);
}
