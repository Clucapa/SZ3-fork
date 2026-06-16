#include <cmath>
#include <memory>

#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIXLin.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "gtest/gtest.h"

template <typename T, uint N>
void runXLinInterpretEB() {
    double tol = 0.001;
    T geb = 10.0;
    SZ3::QoI_XLin<T, N> qoi(tol, geb);

    T eb = qoi.interpret_eb(T(42.0));
    EXPECT_FLOAT_EQ(eb, std::min(T(tol), geb));

    eb = qoi.interpret_eb(T(-3.14));
    EXPECT_FLOAT_EQ(eb, std::min(T(tol), geb));

    eb = qoi.interpret_eb(T(0.0));
    EXPECT_FLOAT_EQ(eb, std::min(T(tol), geb));
}

template <typename T, uint N>
void runXLinCheckComply() {
    double tol = 0.001;
    T geb = 10.0;
    SZ3::QoI_XLin<T, N> qoi(tol, geb);

    EXPECT_TRUE(qoi.check_comply(T(1.0), T(1.0005)));
    EXPECT_TRUE(qoi.check_comply(T(1.0), T(0.9995)));
    EXPECT_FALSE(qoi.check_comply(T(1.0), T(1.002)));
    EXPECT_FALSE(qoi.check_comply(T(1.0), T(0.997)));
    EXPECT_TRUE(qoi.check_comply(T(1.0), T(1.0)));
}

template <typename T, uint N>
void runXLinSetTol() {
    double tol = 0.001;
    T geb = 10.0;
    SZ3::QoI_XLin<T, N> qoi(tol, geb);

    EXPECT_TRUE(qoi.check_comply(T(1.0), T(1.0005)));
    EXPECT_FALSE(qoi.check_comply(T(1.0), T(1.002)));

    qoi.set_tol(0.01);
    EXPECT_TRUE(qoi.check_comply(T(1.0), T(1.002)));
    EXPECT_TRUE(qoi.check_comply(T(1.0), T(1.009)));
    EXPECT_FALSE(qoi.check_comply(T(1.0), T(1.011)));
}

template <typename T, uint N>
void runXLinSetGeb() {
    double tol = 0.001;
    T geb = 10.0;
    SZ3::QoI_XLin<T, N> qoi(tol, geb);

    EXPECT_EQ(qoi.get_geb(), geb);

    T eb = qoi.interpret_eb(T(42.0));
    EXPECT_FLOAT_EQ(eb, std::min(T(tol), geb));

    T new_geb = 0.0005;
    qoi.set_geb(new_geb);

    EXPECT_EQ(qoi.get_geb(), new_geb);
    eb = qoi.interpret_eb(T(42.0));
    EXPECT_FLOAT_EQ(eb, std::min(T(tol), new_geb));
    EXPECT_EQ(eb, new_geb);
}

template <typename T, uint N>
void runX2InterpretEBAtZero() {
    double tol = 1.0;
    T geb = 10.0;
    SZ3::QoI_X2<T, N> qoi(tol, geb);

    T eb = qoi.interpret_eb(T(0.0));
    EXPECT_FLOAT_EQ(eb, T(1.0));
}

template <typename T, uint N>
void runX2InterpretEBAtLarge() {
    double tol = 1.0;
    T geb = 10.0;
    SZ3::QoI_X2<T, N> qoi(tol, geb);

    T eb = qoi.interpret_eb(T(100.0));
    T expected = T(-100.0) + std::sqrt(double(100.0 * 100.0 + 1.0));
    EXPECT_NEAR(eb, expected, T(1e-6));
}

template <typename T, uint N>
void runX2InterpretEBCapping() {
    double tol = 100.0;
    T geb = 0.1;
    SZ3::QoI_X2<T, N> qoi(tol, geb);

    T eb = qoi.interpret_eb(T(0.0));
    EXPECT_FLOAT_EQ(eb, geb);
}

template <typename T, uint N>
void runX2CheckComply() {
    double tol = 1.0;
    T geb = 10.0;
    SZ3::QoI_X2<T, N> qoi(tol, geb);

    EXPECT_TRUE(qoi.check_comply(T(3.0), T(3.1)));
    EXPECT_FALSE(qoi.check_comply(T(3.0), T(3.5)));
    EXPECT_TRUE(qoi.check_comply(T(3.0), T(3.0)));
    EXPECT_TRUE(qoi.check_comply(T(5.0), T(5.099)));
    EXPECT_FALSE(qoi.check_comply(T(1.0), T(1.42)));
}

template <typename T, uint N>
void runX2SetTol() {
    double tol = 1.0;
    T geb = 10.0;
    SZ3::QoI_X2<T, N> qoi(tol, geb);

    EXPECT_FALSE(qoi.check_comply(T(3.0), T(3.5)));

    qoi.set_tol(3.5);
    EXPECT_TRUE(qoi.check_comply(T(3.0), T(3.5)));
}

template <typename T, uint N>
void runX2SetGeb() {
    double tol = 1.0;
    T geb = 10.0;
    SZ3::QoI_X2<T, N> qoi(tol, geb);

    EXPECT_EQ(qoi.get_geb(), geb);

    T eb = qoi.interpret_eb(T(0.0));
    EXPECT_FLOAT_EQ(eb, T(1.0));

    T new_geb = 0.5;
    qoi.set_geb(new_geb);
    EXPECT_EQ(qoi.get_geb(), new_geb);

    eb = qoi.interpret_eb(T(0.0));
    EXPECT_FLOAT_EQ(eb, new_geb);
}

TEST(QoIXLinTest, InterpretEB) { runXLinInterpretEB<float, 1>(); }

TEST(QoIXLinTest, CheckComply) { runXLinCheckComply<float, 1>(); }

TEST(QoIXLinTest, SetTol) { runXLinSetTol<float, 1>(); }

TEST(QoIXLinTest, SetGeb) { runXLinSetGeb<float, 1>(); }

TEST(QoIX2Test, InterpretEBAtZero) { runX2InterpretEBAtZero<float, 1>(); }

TEST(QoIX2Test, InterpretEBAtLarge) { runX2InterpretEBAtLarge<float, 1>(); }

TEST(QoIX2Test, InterpretEBCapping) { runX2InterpretEBCapping<float, 1>(); }

TEST(QoIX2Test, CheckComply) { runX2CheckComply<float, 1>(); }

TEST(QoIX2Test, SetTol) { runX2SetTol<float, 1>(); }

TEST(QoIX2Test, SetGeb) { runX2SetGeb<float, 1>(); }
