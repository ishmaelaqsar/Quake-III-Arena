#include <gtest/gtest.h>
#include <cmath>

#include "q_shared.h"


TEST(MathTest, VectorOperations) {
    vec3_t a = {1.0f, 2.0f, 3.0f};
    vec3_t b = {4.0f, 5.0f, 6.0f};
    vec3_t out;

    // VectorAdd
    VectorAdd(a, b, out);
    EXPECT_FLOAT_EQ(out[0], 5.0f);
    EXPECT_FLOAT_EQ(out[1], 7.0f);
    EXPECT_FLOAT_EQ(out[2], 9.0f);

    // VectorSubtract
    VectorSubtract(b, a, out);
    EXPECT_FLOAT_EQ(out[0], 3.0f);
    EXPECT_FLOAT_EQ(out[1], 3.0f);
    EXPECT_FLOAT_EQ(out[2], 3.0f);

    // VectorScale
    VectorScale(a, 2.5f, out);
    EXPECT_FLOAT_EQ(out[0], 2.5f);
    EXPECT_FLOAT_EQ(out[1], 5.0f);
    EXPECT_FLOAT_EQ(out[2], 7.5f);

    // VectorMA (out = a + b * scale)
    VectorMA(a, 2.0f, b, out);
    EXPECT_FLOAT_EQ(out[0], 9.0f);
    EXPECT_FLOAT_EQ(out[1], 12.0f);
    EXPECT_FLOAT_EQ(out[2], 15.0f);

    // DotProduct
    vec_t dot = DotProduct(a, b);
    // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    EXPECT_FLOAT_EQ(dot, 32.0f);

    // CrossProduct
    CrossProduct(a, b, out);
    // cx = 2*6 - 3*5 = -3
    // cy = 3*4 - 1*6 = 6
    // cz = 1*5 - 2*4 = -3
    EXPECT_FLOAT_EQ(out[0], -3.0f);
    EXPECT_FLOAT_EQ(out[1], 6.0f);
    EXPECT_FLOAT_EQ(out[2], -3.0f);
}

TEST(MathTest, VectorLengthAndNormalize) {
    vec3_t v = {3.0f, 4.0f, 0.0f};
    vec_t len = VectorLength(v);
    EXPECT_FLOAT_EQ(len, 5.0f);

    vec_t normLen = VectorNormalize(v);
    EXPECT_FLOAT_EQ(normLen, 5.0f);
    EXPECT_NEAR(v[0], 0.6f, 0.001f);
    EXPECT_NEAR(v[1], 0.8f, 0.001f);
    EXPECT_FLOAT_EQ(v[2], 0.0f);

    // Normalize zero vector
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    vec_t zeroLen = VectorNormalize(zero);
    EXPECT_FLOAT_EQ(zeroLen, 0.0f);
}

TEST(MathTest, AngleCalculations) {
    EXPECT_NEAR(AngleNormalize360(370.0f), 10.0f, 0.01f);
    EXPECT_NEAR(AngleNormalize360(-10.0f), 350.0f, 0.01f);
    EXPECT_NEAR(AngleNormalize180(190.0f), -170.0f, 0.01f);
    EXPECT_NEAR(AngleNormalize180(-190.0f), 170.0f, 0.01f);

    EXPECT_NEAR(AngleDelta(10.0f, 350.0f), 20.0f, 0.01f);
    EXPECT_NEAR(AngleDelta(350.0f, 10.0f), -20.0f, 0.01f);
}

TEST(MathTest, AnglesVectorsConversion) {
    vec3_t angles = {0.0f, 90.0f, 0.0f};
    vec3_t forward, right, up;
    AngleVectors(angles, forward, right, up);

    // Yaw 90 degrees faces +Y
    EXPECT_NEAR(forward[0], 0.0f, 0.001f);
    EXPECT_NEAR(forward[1], 1.0f, 0.001f);
    EXPECT_NEAR(forward[2], 0.0f, 0.001f);

    // Convert forward back to angles
    vec3_t calculatedAngles;
    vectoangles(forward, calculatedAngles);
    EXPECT_NEAR(calculatedAngles[PITCH], 0.0f, 0.001f);
    EXPECT_NEAR(calculatedAngles[YAW], 90.0f, 0.001f);
    EXPECT_NEAR(calculatedAngles[ROLL], 0.0f, 0.001f);
}

TEST(MathTest, FastInverseSquareRoot) {
    float values[] = {1.0f, 4.0f, 16.0f, 100.0f, 0.25f, 55.5f};
    for (float val : values) {
        float q_inv = Q_rsqrt(val);
        float std_inv = 1.0f / std::sqrt(val);
        // Fast inverse square root has ~0.2% max relative error
        float relativeError = std::abs((q_inv - std_inv) / std_inv);
        EXPECT_LT(relativeError, 0.01f);
    }
}

TEST(MathTest, ByteSwapping) {
    short s = 0x1234;
    short s_swapped = ShortSwap(s);
    EXPECT_EQ(s_swapped, (short)0x3412);
    EXPECT_EQ(ShortSwap(s_swapped), s);

    int l = 0x12345678;
    int l_swapped = LongSwap(l);
    EXPECT_EQ(l_swapped, (int)0x78563412);
    EXPECT_EQ(LongSwap(l_swapped), l);

    float f = 123.456f;
    float f_swapped = FloatSwap(&f);
    float f_restored = FloatSwap(&f_swapped);
    EXPECT_FLOAT_EQ(f, f_restored);
}
