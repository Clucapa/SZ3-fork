#include <cmath>
#include <vector>
#include <memory>

#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIIf.hpp"
#include "SZ3/qoi/QoIXLin.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "SZ3/qoi/QoI_SumQoI.hpp"
#include "SZ3/qoi/QoI_MultiQoI.hpp"
#include "SZ3/qoi/MultiQoIEBProvider.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/utils/Config.hpp"
#include "gtest/gtest.h"

using namespace SZ3;

// ---- Base function eval tests ----

TEST(BaseQoI, XLinEval) {
    QoI_XLin<double, 1> qoi(0.1, 1.0);
    EXPECT_DOUBLE_EQ(qoi.eval(3.0), 3.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-2.0), -2.0);
}

TEST(BaseQoI, X2Eval) {
    QoI_X2<double, 1> qoi(0.1, 1.0);
    EXPECT_DOUBLE_EQ(qoi.eval(3.0), 9.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-2.0), 4.0);
}

TEST(BaseQoI, DefaultCheckComply) {
    QoI_XLin<double, 1> qoi(0.01, 1.0);
    EXPECT_TRUE(qoi.check_comply(1.0, 1.005));
    EXPECT_FALSE(qoi.check_comply(1.0, 1.02));
}

TEST(BaseQoI, DefaultInterpretEB) {
    QoI_X2<double, 1> qoi(1.0, 10.0);
    // The default interpret_eb is overridden by X2's analytical version.
    // But any QoI using the default (eval-based) should produce a valid bound.
    double x = 2.0;
    double eb = qoi.interpret_eb(x);
    EXPECT_GE(eb, 0.0);
    EXPECT_LE(eb, 10.0);
}

// ---- SumQoI tests ----

TEST(SumQoI, SingleFunctionEval) {
    auto f1 = std::make_shared<QoI_X2<double, 1>>(1.0, 10.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> funcs = {f1};
    QoI_SumQoI<double, 1> sum(std::move(funcs), 1.0, 10.0);

    EXPECT_DOUBLE_EQ(sum.eval(3.0), 9.0);
    EXPECT_DOUBLE_EQ(sum.eval(0.0), 0.0);
}

TEST(SumQoI, TwoFunctionsEval) {
    auto f1 = std::make_shared<QoI_XLin<double, 1>>(1.0, 10.0);
    auto f2 = std::make_shared<QoI_X2<double, 1>>(1.0, 10.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> funcs = {f1, f2};
    QoI_SumQoI<double, 1> sum(std::move(funcs), 1.0, 10.0);

    EXPECT_DOUBLE_EQ(sum.eval(3.0), 12.0);  // 3 + 9
    EXPECT_DOUBLE_EQ(sum.eval(0.0), 0.0);
}

TEST(SumQoI, CheckComply) {
    auto f1 = std::make_shared<QoI_XLin<double, 1>>(0.1, 10.0);
    auto f2 = std::make_shared<QoI_X2<double, 1>>(0.1, 10.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> funcs = {f1, f2};
    QoI_SumQoI<double, 1> sum(std::move(funcs), 0.1, 10.0);

    // | (1.0+1.0²) - (1.005+1.005²) | ≈ 0.015 > 0.1? No, ~0.015 ≤ 0.1, pass
    EXPECT_TRUE(sum.check_comply(1.0, 1.005));
    // | (1.0+1.0²) - (1.5+1.5²) | = |2.0 - 3.75| = 1.75 > 0.1, fail
    EXPECT_FALSE(sum.check_comply(1.0, 1.5));
}

TEST(SumQoI, InterpretEBReturn) {
    auto f1 = std::make_shared<QoI_XLin<double, 1>>(0.5, 100.0);
    auto f2 = std::make_shared<QoI_X2<double, 1>>(0.5, 100.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> funcs = {f1, f2};
    QoI_SumQoI<double, 1> sum(std::move(funcs), 0.5, 100.0);

    double eb = sum.interpret_eb(2.0);
    // deriv ≈ |1 + 4| = 5, eb ≈ 0.5/5 = 0.1
    EXPECT_NEAR(eb, 0.1, 0.05);
    EXPECT_GE(eb, 0.0);
}

TEST(SumQoI, IsPointwise) {
    auto f1 = std::make_shared<QoI_XLin<double, 1>>(1.0, 10.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> funcs = {f1};
    QoI_SumQoI<double, 1> sum(std::move(funcs), 1.0, 10.0);

    EXPECT_TRUE(sum.is_pointwise());
}

// ---- MultiQoI tests ----

TEST(MultiQoI, TwoGroupsMinEB) {
    auto g1 = std::make_shared<QoI_XLin<double, 1>>(0.01, 1.0);   // eb = 0.01
    auto g2 = std::make_shared<QoI_X2<double, 1>>(0.001, 10.0);   // eb = -1 + sqrt(1+0.001) ≈ 0.0005
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> groups = {g1, g2};
    QoI_MultiQoI<double, 1> multi(std::move(groups));

    double eb = multi.interpret_eb(1.0);
    EXPECT_NEAR(eb, 0.0005, 0.0003);
    EXPECT_GT(eb, 0.0);
    EXPECT_LE(eb, 0.01);
}

TEST(MultiQoI, CheckComplyAll) {
    auto g1 = std::make_shared<QoI_XLin<double, 1>>(0.005, 1.0);
    auto g2 = std::make_shared<QoI_X2<double, 1>>(0.005, 1.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> groups = {g1, g2};
    QoI_MultiQoI<double, 1> multi(std::move(groups));

    // Small error: both pass
    EXPECT_TRUE(multi.check_comply(2.0, 2.001));
    // Large error: both fail
    EXPECT_FALSE(multi.check_comply(2.0, 3.0));
}

TEST(MultiQoI, DelegatesLifecycle) {
    auto g1 = std::make_shared<QoI_X2<double, 1>>(1.0, 10.0);
    auto g2 = std::make_shared<QoI_X2<double, 1>>(1.0, 10.0);
    std::vector<std::shared_ptr<concepts::QoIIf<double, 1>>> groups = {g1, g2};
    QoI_MultiQoI<double, 1> multi(std::move(groups));

    EXPECT_NO_FATAL_FAILURE(multi.precompress_block(100));
    EXPECT_NO_FATAL_FAILURE(multi.update_tolerance(1.0, 0.9));
    EXPECT_NO_FATAL_FAILURE(multi.postcompress_block());
}

// ---- MultiQoIEBProvider tests ----

TEST(MultiQoIEBProvider, AdvanceReturnsMin) {
    std::vector<double> ebs1 = {0.5, 0.3, 0.8};
    std::vector<double> ebs2 = {0.2, 0.6, 0.4};
    std::vector<std::unique_ptr<concepts::EBProvider<double>>> providers;
    providers.push_back(std::make_unique<PointwiseEBProvider<double>>(ebs1.data(), ebs1.size()));
    providers.push_back(std::make_unique<PointwiseEBProvider<double>>(ebs2.data(), ebs2.size()));
    MultiQoIEBProvider<double> multi(std::move(providers));

    multi.precompress_block(3);
    EXPECT_DOUBLE_EQ(multi.advance(1.0, 1.0), 0.2);  // min(0.5, 0.2)
    EXPECT_DOUBLE_EQ(multi.advance(1.0, 1.0), 0.3);  // min(0.3, 0.6)
    EXPECT_DOUBLE_EQ(multi.advance(1.0, 1.0), 0.4);  // min(0.8, 0.4)
    multi.postcompress_block();
}

TEST(MultiQoIEBProvider, DecompressAdvance) {
    std::vector<double> ebs1 = {1.0, 2.0};
    std::vector<double> ebs2 = {3.0, 4.0};
    std::vector<std::unique_ptr<concepts::EBProvider<double>>> providers;
    providers.push_back(std::make_unique<PointwiseEBProvider<double>>(ebs1.data(), ebs1.size()));
    providers.push_back(std::make_unique<PointwiseEBProvider<double>>(ebs2.data(), ebs2.size()));
    MultiQoIEBProvider<double> multi(std::move(providers));

    multi.precompress_block(2);
    multi.advance(1.0, 1.0);  // compress
    multi.advance(1.0, 1.0);
    multi.advance();           // decompress
    multi.advance();           // decompress
    EXPECT_NO_FATAL_FAILURE(multi.postcompress_block());
}

// ---- Nibble parser + factory tests ----

TEST(NibbleParser, SingleFunction) {
    Config conf(10);
    conf.qEB = 0.1;
    conf.absErrorBound = 1.0;
    conf.ebs.resize(10, 0.1);

    conf.qoi = 1;
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    EXPECT_DOUBLE_EQ(qoi->eval(3.0), 9.0);
}

TEST(NibbleParser, SumGroup) {
    Config conf(10);
    conf.qEB = 0.5;
    conf.absErrorBound = 10.0;
    conf.ebs.resize(10, 0.1);

    conf.qoi = 0x12;
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    EXPECT_DOUBLE_EQ(qoi->eval(2.0), 12.0);
    EXPECT_TRUE(qoi->is_pointwise());
}

TEST(NibbleParser, MultiGroupAND) {
    Config conf(10);
    conf.qEB = 0.5;
    conf.absErrorBound = 10.0;
    conf.ebs.resize(10, 0.1);

    conf.qoi = 0x1F3;
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    double eb = qoi->interpret_eb(4.0);
    EXPECT_GT(eb, 0.0);
    EXPECT_LE(eb, 10.0);
}

TEST(NibbleParser, ComplexEncoding) {
    Config conf(10);
    conf.qEB = 1.0;
    conf.absErrorBound = 100.0;
    conf.ebs.resize(10, 0.5);

    conf.qoi = 0x12F3F456;
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    double eb = qoi->interpret_eb(1.0);
    EXPECT_GT(eb, 0.0);
    EXPECT_LE(eb, 100.0);
}

TEST(NibbleParser, UnknownNibbleThrows) {
    Config conf;
    conf.qoi = 0x1E;  // E is reserved
    auto f = [&]() { GetQOI<double, 1>(conf); };
    EXPECT_THROW(f(), std::invalid_argument);
}

// ---- create_eb_provider integration ----

TEST(CreateEBProvider, PointwiseFromConfig) {
    Config conf(10);
    conf.ebs = {0.1, 0.2, 0.3, 0.1, 0.2, 0.3, 0.1, 0.2, 0.3, 0.1};
    conf.qEB = 0.1;
    conf.absErrorBound = 1.0;
    conf.qoi = 1;

    auto qoi = GetQOI<double, 1>(conf);
    auto provider = qoi->create_eb_provider(conf);
    ASSERT_NE(provider, nullptr);

    provider->precompress_block(3);
    EXPECT_DOUBLE_EQ(provider->advance(1.0, 1.0), 0.1);
    EXPECT_DOUBLE_EQ(provider->advance(1.0, 1.0), 0.2);
    EXPECT_DOUBLE_EQ(provider->advance(1.0, 1.0), 0.3);
    provider->postcompress_block();
}

TEST(CreateEBProvider, MultiQoIProvider) {
    Config conf(10);
    conf.ebs = {0.1, 0.05};
    conf.qEB = 1.0;
    conf.absErrorBound = 100.0;
    conf.qoi = 0x1F3;

    auto qoi = GetQOI<double, 1>(conf);
    auto provider = qoi->create_eb_provider(conf);
    ASSERT_NE(provider, nullptr);

    provider->precompress_block(2);
    double eb1 = provider->advance(1.0, 1.0);
    double eb2 = provider->advance(1.0, 1.0);
    EXPECT_GE(eb1, 0.0);
    EXPECT_GE(eb2, 0.0);
    provider->postcompress_block();
}
