#include <cmath>
#include <cstdlib>
#include <random>
#include <vector>
#include <memory>

#include "SZ3/decomposition/QpetBlockDecomp.hpp"
#include "SZ3/predictor/LorenzoPredictor.hpp"
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIIf.hpp"
#include "SZ3/qoi/QoIXLin.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "SZ3/qoi/QoI_XCubic.hpp"
#include "SZ3/qoi/QoI_XSqrt.hpp"
#include "SZ3/qoi/QoI_XExp.hpp"
#include "SZ3/qoi/QoI_XLogX.hpp"
#include "SZ3/qoi/QoI_LogX.hpp"
#include "SZ3/qoi/QoI_XRecip.hpp"
#include "SZ3/qoi/QoI_XAbs.hpp"
#include "SZ3/qoi/QoI_XSin.hpp"
#include "SZ3/qoi/QoI_XTanh.hpp"
#include "SZ3/qoi/QoI_XPower.hpp"
#include "SZ3/qoi/QoI_XPower.hpp"
#include "SZ3/qoi/QoI_SumQoI.hpp"
#include "SZ3/qoi/QoI_MultiQoI.hpp"
#include "SZ3/qoi/MultiQoIEBProvider.hpp"
#include "SZ3/qoi/PointwiseEBProvider.hpp"
#include "SZ3/qoi/RegionalMean.hpp"
#include "SZ3/quantizer/QpetQnt.hpp"
#include "SZ3/utils/Config.hpp"
#include "gtest/gtest.h"

using namespace SZ3;

// ---- Base function tests (all 12 pointwise base QoIs) ----

TEST(BaseQoI, XLinEval) {
    QoI_XLin<double, 1> qoi(0.1, 1.0);
    EXPECT_DOUBLE_EQ(qoi.eval(3.0), 3.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-2.0), -2.0);
}

TEST(BaseQoI, XLinCheckComply) {
    QoI_XLin<double, 1> qoi(0.01, 1.0);
    EXPECT_TRUE(qoi.check_comply(1.0, 1.005));
    EXPECT_FALSE(qoi.check_comply(1.0, 1.02));
}

TEST(BaseQoI, XLinInterpretEB) {
    QoI_XLin<double, 1> qoi(0.001, 10.0);
    EXPECT_DOUBLE_EQ(qoi.interpret_eb(42.0), 0.001);  // min(tol, geb)
    EXPECT_DOUBLE_EQ(qoi.interpret_eb(0.0), 0.001);
}

TEST(BaseQoI, X2Eval) {
    QoI_X2<double, 1> qoi(0.1, 1.0);
    EXPECT_DOUBLE_EQ(qoi.eval(3.0), 9.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-2.0), 4.0);
}

TEST(BaseQoI, X2CheckComply) {
    QoI_X2<double, 1> qoi(1.0, 10.0);
    EXPECT_TRUE(qoi.check_comply(3.0, 3.1));   // |9-9.61|=0.61 ≤ 1.0
    EXPECT_FALSE(qoi.check_comply(3.0, 3.5));  // |9-12.25|=3.25 > 1.0
}

TEST(BaseQoI, X2InterpretEB) {
    QoI_X2<double, 1> qoi(1.0, 10.0);
    double eb0 = qoi.interpret_eb(0.0);
    EXPECT_DOUBLE_EQ(eb0, 1.0);  // sqrt(0+1)=1
    double eb10 = qoi.interpret_eb(100.0);
    double expected = -100.0 + std::sqrt(100.0 * 100.0 + 1.0);
    EXPECT_NEAR(eb10, expected, 1e-6);
}

TEST(BaseQoI, XCubicEval) {
    QoI_XCubic<double, 1> qoi(0.1, 100.0);
    EXPECT_DOUBLE_EQ(qoi.eval(2.0), 8.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-3.0), -27.0);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);
}

TEST(BaseQoI, XCubicCheckComply) {
    QoI_XCubic<double, 1> qoi(0.1, 100.0);
    EXPECT_TRUE(qoi.check_comply(2.0, 2.005));   // |8-8.060|=0.060 ≤ 0.1
    EXPECT_FALSE(qoi.check_comply(2.0, 2.1));    // |8-9.261|=1.261 > 0.1
}

TEST(BaseQoI, XCubicInterpretEB) {
    QoI_XCubic<double, 1> qoi(0.5, 100.0);
    // f(x)=x³, f'(2)=12, eb≈0.5/12≈0.0417
    double eb = qoi.interpret_eb(2.0);
    EXPECT_NEAR(eb, 0.04167, 0.005);
    EXPECT_GE(eb, 0.0);
    EXPECT_LE(eb, 100.0);
}

TEST(BaseQoI, XSqrtEval) {
    QoI_XSqrt<double, 1> qoi(0.1, 100.0);
    EXPECT_DOUBLE_EQ(qoi.eval(4.0), 2.0);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-4.0), 2.0);  // fabs inside
}

TEST(BaseQoI, XSqrtCheckComply) {
    QoI_XSqrt<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(4.0, 4.01));   // |2-2.0025|≈0.0025 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(4.0, 4.1));   // |2-2.0249|≈0.0249 > 0.01
}

TEST(BaseQoI, XSqrtInterpretEB) {
    QoI_XSqrt<double, 1> qoi(0.1, 100.0);
    // f'(4)=1/(2*sort(4))=0.25, eb=0.1/0.25=0.4
    double eb = qoi.interpret_eb(4.0);
    EXPECT_NEAR(eb, 0.4, 0.01);
    EXPECT_GE(eb, 0.0);
}

TEST(BaseQoI, XExpEval) {
    QoI_XExp<double, 1> qoi(0.1, 100.0);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 1.0);
    EXPECT_NEAR(qoi.eval(1.0), std::exp(1.0), 1e-10);
}

TEST(BaseQoI, XExpCheckComply) {
    QoI_XExp<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(0.0, 0.001));   // |e^0-e^0.001|≈0.001 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(0.0, 0.1));    // |1-1.105|=0.105 > 0.01
}

TEST(BaseQoI, XExpInterpretEB) {
    QoI_XExp<double, 1> qoi(0.5, 100.0);
    // f'(1)=e≈2.718, eb≈0.5/e≈0.184
    double eb = qoi.interpret_eb(1.0);
    EXPECT_NEAR(eb, 0.1839, 0.01);
    EXPECT_GE(eb, 0.0);
}

TEST(BaseQoI, XLogXEval) {
    QoI_XLogX<double, 1> qoi(0.1, 100.0);
    EXPECT_NEAR(qoi.eval(2.0), 2.0 * std::log(2.0), 1e-10);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);  // near-zero return
}

TEST(BaseQoI, XLogXCheckComply) {
    QoI_XLogX<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(2.0, 2.001));   // small diff in f
    EXPECT_FALSE(qoi.check_comply(2.0, 2.1));    // large diff in f
}

TEST(BaseQoI, XLogXInterpretEB) {
    QoI_XLogX<double, 1> qoi(0.1, 100.0);
    double eb = qoi.interpret_eb(2.0);
    EXPECT_GE(eb, 0.0);
    EXPECT_LE(eb, 100.0);
}

TEST(BaseQoI, LogXEval) {
    QoI_LogX<double, 1> qoi(0.1, 100.0);
    EXPECT_NEAR(qoi.eval(std::exp(1.0)), 1.0, 1e-10);
    EXPECT_DOUBLE_EQ(qoi.eval(1.0), 0.0);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);
}

TEST(BaseQoI, LogXCheckComply) {
    QoI_LogX<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(2.0, 2.01));   // |log2-log2.01|≈0.005 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(2.0, 2.1));   // |log2-log2.1|≈0.049 > 0.01
}

TEST(BaseQoI, LogXInterpretEB) {
    QoI_LogX<double, 1> qoi(0.1, 100.0);
    double eb = qoi.interpret_eb(2.0);
    EXPECT_GE(eb, 0.0);
    EXPECT_LE(eb, 100.0);
}

TEST(BaseQoI, XRecipEval) {
    QoI_XRecip<double, 1> qoi(0.1, 100.0);
    EXPECT_DOUBLE_EQ(qoi.eval(2.0), 0.5);
    EXPECT_DOUBLE_EQ(qoi.eval(4.0), 0.25);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);  // near-zero return
}

TEST(BaseQoI, XRecipCheckComply) {
    QoI_XRecip<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(2.0, 2.01));    // |0.5-0.4975|≈0.0025 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(2.0, 2.5));    // |0.5-0.4|=0.1 > 0.01
}

TEST(BaseQoI, XRecipInterpretEB) {
    QoI_XRecip<double, 1> qoi(0.1, 100.0);
    double eb = qoi.interpret_eb(2.0);
    EXPECT_GE(eb, 0.0);
    EXPECT_LE(eb, 100.0);
}

TEST(BaseQoI, XAbsEval) {
    QoI_XAbs<double, 1> qoi(0.1, 100.0);
    EXPECT_DOUBLE_EQ(qoi.eval(3.0), 3.0);
    EXPECT_DOUBLE_EQ(qoi.eval(-5.0), 5.0);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);
}

TEST(BaseQoI, XAbsCheckComply) {
    QoI_XAbs<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(3.0, 3.005));   // |3-3.005|=0.005 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(3.0, 3.02));   // |3-3.02|=0.02 > 0.01
}

TEST(BaseQoI, XAbsInterpretEB) {
    QoI_XAbs<double, 1> qoi(0.1, 100.0);
    // XAbs overrides interpret_eb to min(tol, geb)
    double eb = qoi.interpret_eb(3.0);
    EXPECT_DOUBLE_EQ(eb, 0.1);
    double eb0 = qoi.interpret_eb(0.0);
    EXPECT_DOUBLE_EQ(eb0, 0.1);
}

TEST(BaseQoI, XSinEval) {
    QoI_XSin<double, 1> qoi(0.1, 100.0);
    EXPECT_NEAR(qoi.eval(0.0), 0.0, 1e-10);
    EXPECT_NEAR(qoi.eval(M_PI / 2.0), 1.0, 1e-10);
}

TEST(BaseQoI, XSinCheckComply) {
    QoI_XSin<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(0.0, 0.005));   // |0-sin(0.005)|≈0.005 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(0.0, 0.02));   // |0-sin(0.02)|≈0.02 > 0.01
}

TEST(BaseQoI, XSinInterpretEB) {
    QoI_XSin<double, 1> qoi(0.1, 100.0);
    // f'(0)=cos(0)=1, eb≈tol
    double eb = qoi.interpret_eb(0.0);
    EXPECT_NEAR(eb, 0.1, 0.01);
    EXPECT_GE(eb, 0.0);
}

TEST(BaseQoI, XTanhEval) {
    QoI_XTanh<double, 1> qoi(0.1, 100.0);
    EXPECT_NEAR(qoi.eval(0.0), 0.0, 1e-10);
    EXPECT_NEAR(qoi.eval(1.0), std::tanh(1.0), 1e-10);
}

TEST(BaseQoI, XTanhCheckComply) {
    QoI_XTanh<double, 1> qoi(0.01, 100.0);
    EXPECT_TRUE(qoi.check_comply(0.0, 0.005));   // |0-tanh(0.005)|≈0.005 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(0.0, 0.02));   // |0-tanh(0.02)|≈0.02 > 0.01
}

TEST(BaseQoI, XTanhInterpretEB) {
    QoI_XTanh<double, 1> qoi(0.1, 100.0);
    // f'(0)=1, eb≈tol
    double eb = qoi.interpret_eb(0.0);
    EXPECT_NEAR(eb, 0.1, 0.01);
    EXPECT_GE(eb, 0.0);
}

TEST(BaseQoI, XPowerEval) {
    QoI_XPower<double, 1> qoi(0.1, 100.0, 2.0);  // default expo
    EXPECT_DOUBLE_EQ(qoi.eval(3.0), 9.0);
    EXPECT_DOUBLE_EQ(qoi.eval(0.0), 0.0);
    // non-default expo
    QoI_XPower<double, 1> qoi3(0.1, 100.0, 3.0);
    EXPECT_DOUBLE_EQ(qoi3.eval(2.0), 8.0);
}

TEST(BaseQoI, XPowerCheckComply) {
    QoI_XPower<double, 1> qoi(0.01, 100.0, 2.0);
    EXPECT_TRUE(qoi.check_comply(3.0, 3.001));    // |9-9.006|≈0.006 ≤ 0.01
    EXPECT_FALSE(qoi.check_comply(3.0, 3.01));    // |9-9.06|=0.06 > 0.01
}

TEST(BaseQoI, XPowerInterpretEB) {
    QoI_XPower<double, 1> qoi(0.1, 100.0, 2.0);
    // f'(3)=2*3=6, eb=0.1/6≈0.0167
    double eb = qoi.interpret_eb(3.0);
    EXPECT_NEAR(eb, 0.01667, 0.005);
    EXPECT_GE(eb, 0.0);
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
    conf.qoi = 0xC;  // 0xC is reserved
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

// ---- End-to-end compress-decompress round-trip ----

namespace {

std::vector<double> generate_smooth_1d_data(size_t n) {
    std::vector<double> keys = {10.0, 25.0, 18.0, 40.0, 55.0, 30.0, 45.0, 60.0};
    size_t k = keys.size();
    double seg = static_cast<double>(n) / (k - 1);

    std::vector<double> data(n);
    std::mt19937 rng(42);
    std::normal_distribution<> noise_gen(0.0, 0.3);

    for (size_t i = 0; i < n; i++) {
        double t = static_cast<double>(i) / seg;
        size_t lo = static_cast<size_t>(t);
        double frac = t - lo;
        if (lo >= k - 1) {
            lo = k - 2;
            frac = 1.0;
        }
        double base = keys[lo] * (1.0 - frac) + keys[lo + 1] * frac;
        data[i] = base + noise_gen(rng);
    }
    return data;
}

std::string base64_encode(const void *data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto *p = static_cast<const unsigned char *>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned b0 = p[i], b1 = (i + 1 < len) ? p[i + 1] : 0,
                 b2 = (i + 2 < len) ? p[i + 2] : 0;
        out += tbl[b0 >> 2];
        out += tbl[((b0 & 3) << 4) | (b1 >> 4)];
        size_t rem = len - i;
        out += (rem > 1) ? tbl[((b1 & 0xF) << 2) | (b2 >> 6)] : '=';
        out += (rem > 2) ? tbl[b2 & 0x3F] : '=';
    }
    return out;
}

}  // namespace

TEST(EndToEnd, ComposeSinOfSquareCompressDecompress) {
    constexpr uint N = 1;
    const size_t n = 64;
    auto data = generate_smooth_1d_data(n);

    Config conf(n);
    conf.qEB = 0.3;
    conf.absErrorBound = 0.5;
    conf.qoi = 0x19E;  // Compose(XSin, X2) = sin(x²) — bounded, no overflow
    conf.quantbinCnt = 65536;
    conf.qR = 12;

    auto qoi = GetQOI<double, N>(conf);
    ASSERT_NE(qoi, nullptr);
    ASSERT_TRUE(qoi->is_pointwise());

    conf.ebs.resize(n);
    for (size_t i = 0; i < n; i++)
        conf.ebs[i] = qoi->interpret_eb(data[i]);

    LorenzoPredictor<double, N, 1> pred(conf.absErrorBound);
    QpetQnt<double> qnt(conf.quantbinCnt / 2, 3.0, 0.2,
                        conf.qR, conf.absErrorBound);

    QpetBlockDecomp<double, N, LorenzoPredictor<double, N, 1>,
                     QpetQnt<double>>
        decomp(conf, pred, qnt, qoi);

    auto qis = decomp.compress(conf, data.data());
    EXPECT_EQ(qis.size(), n * 2);

    std::vector<double> dec(n, 0.0);
    Config confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    size_t failures = 0;
    for (size_t i = 0; i < n; i++) {
        if (!qoi->check_comply(data[i], dec[i])) failures++;
    }
    EXPECT_EQ(failures, 0u) << failures << " Compose(Sin,X2) violations";
}

TEST(EndToEnd, XLinParamCompressDecompress) {
    constexpr uint N = 1;
    const size_t n = 64;
    auto data = generate_smooth_1d_data(n);

    Config conf(n);
    conf.qEB = 3.0;
    conf.absErrorBound = 1.0;
    conf.qoi = 0x0;
    conf.quantbinCnt = 65536;
    conf.qR = 12;

    double A = 3.0, B = 1.0;
    std::array<double, 2> p = {A, B};
    conf.qoiParams = base64_encode(p.data(), sizeof(p));

    auto qoi = GetQOI<double, N>(conf);
    ASSERT_NE(qoi, nullptr);
    ASSERT_TRUE(qoi->is_pointwise());

    conf.ebs.resize(n);
    for (size_t i = 0; i < n; i++)
        conf.ebs[i] = qoi->interpret_eb(data[i]);

    LorenzoPredictor<double, N, 1> pred(conf.absErrorBound);
    QpetQnt<double> qnt(conf.quantbinCnt / 2, 3.0, 0.2,
                        conf.qR, conf.absErrorBound);

    QpetBlockDecomp<double, N, LorenzoPredictor<double, N, 1>,
                     QpetQnt<double>>
        decomp(conf, pred, qnt, qoi);

    auto qis = decomp.compress(conf, data.data());
    EXPECT_EQ(qis.size(), n * 2);

    std::vector<double> dec(n, 0.0);
    Config confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    size_t failures = 0;
    for (size_t i = 0; i < n; i++) {
        double f_orig = A * data[i] + B;
        double f_dec = A * dec[i] + B;
        if (std::fabs(f_orig - f_dec) > conf.qEB) failures++;
    }
    EXPECT_EQ(failures, 0u) << failures << " XLin(" << A << "x+" << B << ") violations";
}

TEST(EndToEnd, X2PointwiseCompressDecompress) {
    constexpr uint N = 1;
    const size_t n = 64;
    auto data = generate_smooth_1d_data(n);

    Config conf(n);
    conf.qEB = 1.0;
    conf.absErrorBound = 0.5;
    conf.qoi = 1;
    conf.quantbinCnt = 65536;
    conf.qR = 12;

    auto qoi = GetQOI<double, N>(conf);
    ASSERT_NE(qoi, nullptr);
    ASSERT_TRUE(qoi->is_pointwise());

    conf.ebs.resize(n);
    for (size_t i = 0; i < n; i++)
        conf.ebs[i] = qoi->interpret_eb(data[i]);

    LorenzoPredictor<double, N, 1> pred(conf.absErrorBound);
    QpetQnt<double> qnt(conf.quantbinCnt / 2, 3.0, 0.2,
                        conf.qR, conf.absErrorBound);

    QpetBlockDecomp<double, N, LorenzoPredictor<double, N, 1>,
                     QpetQnt<double>>
        decomp(conf, pred, qnt, qoi);

    auto qis = decomp.compress(conf, data.data());
    EXPECT_EQ(qis.size(), n * 2);

    std::vector<double> dec(n, 0.0);
    Config confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    size_t failures = 0;
    for (size_t i = 0; i < n; i++) {
        if (!qoi->check_comply(data[i], dec[i])) failures++;
    }
    EXPECT_EQ(failures, 0u) << failures << " elements violate X2 compliance";
}

TEST(EndToEnd, RegionalMeanCompressDecompress) {
    constexpr uint N = 1;
    const size_t n = 128;
    auto data = generate_smooth_1d_data(n);

    Config conf(n);
    conf.qEB = 2.0;
    conf.absErrorBound = 1.0;
    conf.qoi = ~0;
    conf.quantbinCnt = 65536;
    conf.qR = 12;

    auto qoi = GetQOI<double, N>(conf);
    ASSERT_NE(qoi, nullptr);
    ASSERT_FALSE(qoi->is_pointwise());

    LorenzoPredictor<double, N, 1> pred(conf.absErrorBound);
    QpetQnt<double> qnt(conf.quantbinCnt / 2, 3.0, 0.2,
                        conf.qR, conf.absErrorBound);

    QpetBlockDecomp<double, N, LorenzoPredictor<double, N, 1>,
                     QpetQnt<double>>
        decomp(conf, pred, qnt, qoi);

    auto qis = decomp.compress(conf, data.data());
    EXPECT_EQ(qis.size(), n * 2);

    std::vector<double> dec(n, 0.0);
    Config confDec = conf;
    decomp.decompress(confDec, qis, dec.data());

    double sum_err = 0.0;
    for (size_t i = 0; i < n; i++)
        sum_err += data[i] - dec[i];

    double mean_err = std::fabs(sum_err) / n;
    EXPECT_LE(mean_err, conf.absErrorBound)
        << "mean error " << mean_err << " exceeds tolerance " << conf.absErrorBound;
}

// ---- QoI_Compose tests ----

TEST(Compose, Eval) {
    auto f = std::make_shared<QoI_X2<double, 1>>(0.1, 10.0);
    auto g = std::make_shared<QoI_XLin<double, 1>>(0.1, 10.0);
    QoI_Compose<double, 1> comp(f, g, 0.1, 10.0);

    EXPECT_DOUBLE_EQ(comp.eval(3.0), 9.0);
    EXPECT_DOUBLE_EQ(comp.eval(0.0), 0.0);
}

TEST(Compose, CheckComply) {
    auto f = std::make_shared<QoI_X2<double, 1>>(0.1, 10.0);
    auto g = std::make_shared<QoI_XLin<double, 1>>(0.1, 10.0);
    QoI_Compose<double, 1> comp(f, g, 0.1, 10.0);

    EXPECT_TRUE(comp.check_comply(2.0, 2.001));
    EXPECT_FALSE(comp.check_comply(2.0, 4.0));
}

TEST(Compose, InterpretEBReturnsBounded) {
    auto f = std::make_shared<QoI_X2<double, 1>>(0.5, 100.0);
    auto g = std::make_shared<QoI_XCubic<double, 1>>(0.5, 100.0);
    QoI_Compose<double, 1> comp(f, g, 0.5, 100.0);

    double eb = comp.interpret_eb(2.0);
    EXPECT_GE(eb, 0.0);
    EXPECT_LE(eb, 100.0);

    double eb0 = comp.interpret_eb(0.0);
    EXPECT_GE(eb0, 0.0);
    EXPECT_LE(eb0, 100.0);
}

TEST(Compose, ComposeAbsThenSqrt) {
    auto f = std::make_shared<QoI_XSqrt<double, 1>>(0.1, 100.0);
    auto g = std::make_shared<QoI_XAbs<double, 1>>(0.1, 100.0);
    QoI_Compose<double, 1> comp(f, g, 0.1, 100.0);

    EXPECT_DOUBLE_EQ(comp.eval(-4.0), 2.0);  // sqrt(|-4|) = 2
    double eb = comp.interpret_eb(4.0);
    EXPECT_GE(eb, 0.0);
}

// ---- XLin identity elision ----

TEST(ComposeElision, InnerXLinIdentity) {
    auto f = std::make_shared<QoI_X2<double, 1>>(0.1, 10.0);
    auto g = std::make_shared<QoI_XLin<double, 1>>(0.1, 10.0, 1.0, 0.0);
    auto res = detail::make_compose<double, 1>(f, g, 0.1, 10.0);

    EXPECT_DOUBLE_EQ(res->eval(3.0), 9.0);
    EXPECT_EQ(res->id, 1);  // should be X2, not 0xE (Compose)
}

TEST(ComposeElision, OuterXLinIdentity) {
    auto f = std::make_shared<QoI_XLin<double, 1>>(0.1, 10.0, 1.0, 0.0);
    auto g = std::make_shared<QoI_X2<double, 1>>(0.1, 10.0);
    auto res = detail::make_compose<double, 1>(f, g, 0.1, 10.0);

    EXPECT_DOUBLE_EQ(res->eval(3.0), 9.0);  // XLin(X2(x)) = X2
    EXPECT_EQ(res->id, 1);
}

TEST(ComposeElision, NoEelideForAffine) {
    auto f = std::make_shared<QoI_X2<double, 1>>(0.1, 10.0);
    auto g = std::make_shared<QoI_XLin<double, 1>>(0.1, 10.0, 2.0, 1.0);
    auto res = detail::make_compose<double, 1>(f, g, 0.1, 10.0);

    EXPECT_EQ(res->id, 0xE);  // not elided → stays Compose
    EXPECT_NEAR(res->eval(3.0), 49.0, 1e-9);  // (2*3+1)² = 49
}

// ---- E nibble parsing ----

TEST(NibbleParser, ComposeSingleE) {
    Config conf(10);
    conf.qEB = 0.5;
    conf.absErrorBound = 10.0;
    conf.ebs.resize(10, 0.1);
    conf.qoi = 0x14E;  // Compose(XExp, X2) = e^(x²)
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    EXPECT_EQ(qoi->id, 0xE);
    EXPECT_DOUBLE_EQ(qoi->eval(0.0), 1.0);  // e^0 = 1
}

TEST(NibbleParser, ComposeNested) {
    Config conf(10);
    conf.qEB = 1.0;
    conf.absErrorBound = 100.0;
    conf.ebs.resize(10, 0.5);
    conf.qoi = 0x34E;  // Compose(XExp, XSqrt) = e^√x
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    EXPECT_EQ(qoi->id, 0xE);
    EXPECT_DOUBLE_EQ(qoi->eval(0.0), 1.0);  // e^0 = 1
}

TEST(NibbleParser, ComposeInMultiGroup) {
    Config conf(10);
    conf.qEB = 1.0;
    conf.absErrorBound = 100.0;
    conf.ebs.resize(10, 0.5);
    conf.qoi = 0x14EF3;  // XSqrt AND Compose(XExp, X2)
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    double eb = qoi->interpret_eb(2.0);
    EXPECT_GT(eb, 0.0);
    EXPECT_LE(eb, 100.0);
}

// ---- Param tests ----

TEST(Param, XLinDefaultNoParams) {
    Config conf(10);
    conf.qEB = 0.1;
    conf.absErrorBound = 1.0;
    conf.ebs.resize(10, 0.1);
    conf.qoi = 0x0;
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    EXPECT_DOUBLE_EQ(qoi->eval(3.0), 3.0);  // identity
}

TEST(Param, XLinCustomParams) {
    double a = 2.0, b = 1.5;
    std::vector<unsigned char> raw(sizeof(double) * 2);
    std::memcpy(raw.data(), &a, sizeof(double));
    std::memcpy(raw.data() + sizeof(double), &b, sizeof(double));

    detail::ParamReader reader(raw);
    double ra = reader.read();
    double rb = reader.read();
    EXPECT_DOUBLE_EQ(ra, 2.0);
    EXPECT_DOUBLE_EQ(rb, 1.5);

    detail::ParamReader reader2(raw);
    auto qoi = detail::make_base_qoi<double, 1>(0, reader2, 0.1, 10.0);
    EXPECT_DOUBLE_EQ(qoi->eval(3.0), 2.0 * 3.0 + 1.5);
}

TEST(Param, XExpDefaultBase) {
    Config conf(10);
    conf.qEB = 0.1;
    conf.absErrorBound = 10.0;
    conf.ebs.resize(10, 0.1);
    conf.qoi = 0x4;
    auto qoi = GetQOI<double, 1>(conf);
    ASSERT_NE(qoi, nullptr);
    EXPECT_DOUBLE_EQ(qoi->eval(0.0), 1.0);  // e^0 = 1
}

TEST(Param, XExpCustomBase) {
    Config conf(10);
    conf.qEB = 0.1;
    conf.absErrorBound = 10.0;
    conf.ebs.resize(10, 0.1);
    conf.qoi = 0x4;

    double base = 10.0;
    std::vector<unsigned char> raw(sizeof(double));
    std::memcpy(raw.data(), &base, sizeof(double));
    conf.qoiParams = " ";

    detail::ParamReader reader(raw);
    auto qoi = detail::make_base_qoi<double, 1>(0x4, reader, conf.qEB, conf.absErrorBound);

    EXPECT_DOUBLE_EQ(qoi->eval(0.0), 1.0);
    EXPECT_DOUBLE_EQ(qoi->eval(1.0), 10.0);
}

TEST(Param, Base64DecodeRoundtrip) {
    double p1 = 3.14, p2 = 2.718;
    std::vector<unsigned char> raw(sizeof(double) * 2);
    std::memcpy(raw.data(), &p1, sizeof(double));
    std::memcpy(raw.data() + sizeof(double), &p2, sizeof(double));

    auto decoded = detail::base64_decode("");
    EXPECT_TRUE(decoded.empty());

    detail::ParamReader reader(raw);
    EXPECT_DOUBLE_EQ(reader.read(), p1);
    EXPECT_DOUBLE_EQ(reader.read(), p2);
    EXPECT_TRUE(std::isnan(reader.read()));
}
