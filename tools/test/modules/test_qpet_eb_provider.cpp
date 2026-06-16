#include <cmath>
#include <vector>

#include "SZ3/qoi/EBProvider.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIXLin.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "SZ3/utils/FileUtil.hpp"
#include "gtest/gtest.h"

TEST(PointwiseEBProvider, Advance) {
    const double ebs[] = {0.1, 0.2, 0.3, 0.4, 0.5};
    size_t n = 5;
    SZ3::PointwiseEBProvider<float> provider(ebs, n);

    provider.precompress_block(n);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.1f);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.2f);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.3f);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.4f);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.5f);
    provider.postcompress_block();
}

TEST(PointwiseEBProvider, Reset) {
    const double ebs[] = {10.0, 20.0};
    SZ3::PointwiseEBProvider<float> provider(ebs, 2);

    provider.precompress_block(2);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 10.0f);

    provider.reset();
    provider.precompress_block(2);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 10.0f);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 20.0f);
}

TEST(PointwiseEBProvider, DecompressAdvance) {
    const double ebs[] = {0.01, 0.02, 0.03};
    SZ3::PointwiseEBProvider<float> provider(ebs, 3);

    provider.precompress_block(3);
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.01f);
    provider.advance();
    EXPECT_FLOAT_EQ(provider.advance(0.0f, 0.0f), 0.03f);
}

TEST(PointwiseEBProvider, SaveLoadRoundTrip) {
    const double ebs[] = {0.001, 0.002, 0.003, 0.004};
    size_t n = 4;
    SZ3::PointwiseEBProvider<float> provider(ebs, n);

    std::vector<unsigned char> buf(256);
    unsigned char *p = buf.data();
    provider.save(p);
    size_t saved = p - buf.data();

    SZ3::PointwiseEBProvider<float> loaded(nullptr, 0);
    const unsigned char *cp = buf.data();
    size_t rl = saved;
    loaded.load(cp, rl);

    loaded.precompress_block(n);
    for (size_t i = 0; i < n; i++) {
        EXPECT_FLOAT_EQ(loaded.advance(0.0f, 0.0f), static_cast<float>(ebs[i]));
    }
}

TEST(PointwiseEBProvider, DoubleTypeSupport) {
    const double ebs[] = {1.5, 2.5, 3.5};
    SZ3::PointwiseEBProvider<double> provider(ebs, 3);

    provider.precompress_block(3);
    EXPECT_DOUBLE_EQ(provider.advance(0.0, 0.0), 1.5);
    EXPECT_DOUBLE_EQ(provider.advance(0.0, 0.0), 2.5);
    EXPECT_DOUBLE_EQ(provider.advance(0.0, 0.0), 3.5);
}

TEST(PointwiseEBProvider, IntegrationWithQoi) {
    double tol = 0.001;
    float geb = 10.0f;
    SZ3::QoI_XLin<float, 1> qoi(tol, geb);

    size_t n = 4;
    std::vector<double> ebs(n);
    for (size_t i = 0; i < n; i++) {
        ebs[i] = qoi.interpret_eb(static_cast<float>(i));
    }

    SZ3::PointwiseEBProvider<float> provider(ebs.data(), n);
    provider.precompress_block(n);

    float data[] = {1.0f, 1.5f, 2.0f, 2.5f};
    for (size_t i = 0; i < n; i++) {
        float eb = provider.advance(data[i], 0.0f);
        EXPECT_FLOAT_EQ(eb, static_cast<float>(ebs[i]));
    }
}
