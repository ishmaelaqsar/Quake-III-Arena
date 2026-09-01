#include <gtest/gtest.h>
#include "../code/sys/math/vec3.hpp"

using namespace q3::math;

TEST(ModernMathTest, Vec3BasicArithmetic) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);

    Vec3 add = a + b;
    EXPECT_FLOAT_EQ(add.x, 5.0f);
    EXPECT_FLOAT_EQ(add.y, 7.0f);
    EXPECT_FLOAT_EQ(add.z, 9.0f);

    Vec3 sub = b - a;
    EXPECT_FLOAT_EQ(sub.x, 3.0f);
    EXPECT_FLOAT_EQ(sub.y, 3.0f);
    EXPECT_FLOAT_EQ(sub.z, 3.0f);

    Vec3 scaled = a * 2.0f;
    EXPECT_FLOAT_EQ(scaled.x, 2.0f);
    EXPECT_FLOAT_EQ(scaled.y, 4.0f);
    EXPECT_FLOAT_EQ(scaled.z, 6.0f);
}

TEST(ModernMathTest, Vec3DotAndCrossProduct) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);

    EXPECT_FLOAT_EQ(a.dot(b), 0.0f);

    Vec3 c = a.cross(b);
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f);
}

TEST(ModernMathTest, Vec3LengthAndNormalization) {
    Vec3 v(0.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.length(), 5.0f);

    Vec3 norm = v.normalized();
    EXPECT_FLOAT_EQ(norm.length(), 1.0f);
    EXPECT_FLOAT_EQ(norm.x, 0.0f);
    EXPECT_FLOAT_EQ(norm.y, 0.6f);
    EXPECT_FLOAT_EQ(norm.z, 0.8f);
}

TEST(ModernMathTest, LegacyVec3Interop) {
    vec3_t legacy = {10.0f, 20.0f, 30.0f};
    Vec3 modern(legacy);

    EXPECT_FLOAT_EQ(modern.x, 10.0f);
    EXPECT_FLOAT_EQ(modern.y, 20.0f);
    EXPECT_FLOAT_EQ(modern.z, 30.0f);

    modern.x += 5.0f;
    vec3_t out;
    modern.to_c_array(out);
    EXPECT_FLOAT_EQ(out[0], 15.0f);
    EXPECT_FLOAT_EQ(out[1], 20.0f);
    EXPECT_FLOAT_EQ(out[2], 30.0f);
}

TEST(ModernMathTest, AnglesToVectors) {
    Angles a(0.0f, 90.0f, 0.0f);
    Vec3 forward, right, up;
    a.vectors(&forward, &right, &up);

    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, 1.0f, 0.001f);
    EXPECT_NEAR(forward.z, 0.0f, 0.001f);
}
