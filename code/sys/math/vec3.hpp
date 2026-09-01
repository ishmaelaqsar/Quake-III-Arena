#pragma once

#include <cmath>
#include <array>
#include <string>
#include <sstream>
#include <iostream>
#include <string_view>

extern "C" {
#include "q_shared.h"
}

namespace q3::math {

struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}
    
    // Interop with legacy vec3_t
    explicit Vec3(const vec3_t v) noexcept : x(v[0]), y(v[1]), z(v[2]) {}
    
    void to_c_array(vec3_t out) const noexcept {
        out[0] = x;
        out[1] = y;
        out[2] = z;
    }

    constexpr float& operator[](std::size_t index) noexcept {
        return (&x)[index];
    }

    constexpr const float& operator[](std::size_t index) const noexcept {
        return (&x)[index];
    }

    constexpr Vec3 operator+(const Vec3& rhs) const noexcept {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }

    constexpr Vec3 operator-(const Vec3& rhs) const noexcept {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }

    constexpr Vec3 operator*(float scalar) const noexcept {
        return {x * scalar, y * scalar, z * scalar};
    }

    constexpr Vec3 operator/(float scalar) const noexcept {
        float inv = 1.0f / scalar;
        return {x * inv, y * inv, z * inv};
    }

    Vec3& operator+=(const Vec3& rhs) noexcept {
        x += rhs.x; y += rhs.y; z += rhs.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& rhs) noexcept {
        x -= rhs.x; y -= rhs.y; z -= rhs.z;
        return *this;
    }

    Vec3& operator*=(float scalar) noexcept {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;
    }

    constexpr float dot(const Vec3& rhs) const noexcept {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    constexpr Vec3 cross(const Vec3& rhs) const noexcept {
        return {
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        };
    }

    float length_squared() const noexcept {
        return dot(*this);
    }

    float length() const noexcept {
        return std::sqrt(length_squared());
    }

    float normalize() noexcept {
        float len = length();
        if (len > 0.0f) {
            float inv = 1.0f / len;
            x *= inv;
            y *= inv;
            z *= inv;
        } else {
            x = y = z = 0.0f;
        }
        return len;
    }

    Vec3 normalized() const noexcept {
        Vec3 copy = *this;
        copy.normalize();
        return copy;
    }

    constexpr bool operator==(const Vec3& rhs) const noexcept {
        return x == rhs.x && y == rhs.y && z == rhs.z;
    }

    constexpr bool operator!=(const Vec3& rhs) const noexcept {
        return !(*this == rhs);
    }

    std::string to_string() const {
        std::ostringstream ss;
        ss << "(" << x << ", " << y << ", " << z << ")";
        return ss.str();
    }
};

inline Vec3 operator*(float scalar, const Vec3& v) noexcept {
    return v * scalar;
}

struct Angles {
    float pitch{0.0f};
    float yaw{0.0f};
    float roll{0.0f};

    constexpr Angles() noexcept = default;
    constexpr Angles(float pitch_, float yaw_, float roll_) noexcept
        : pitch(pitch_), yaw(yaw_), roll(roll_) {}

    explicit Angles(const vec3_t a) noexcept : pitch(a[PITCH]), yaw(a[YAW]), roll(a[ROLL]) {}

    void to_c_array(vec3_t out) const noexcept {
        out[PITCH] = pitch;
        out[YAW] = yaw;
        out[ROLL] = roll;
    }

    void normalize180() noexcept {
        pitch = AngleNormalize180(pitch);
        yaw   = AngleNormalize180(yaw);
        roll  = AngleNormalize180(roll);
    }

    void normalize360() noexcept {
        pitch = AngleNormalize360(pitch);
        yaw   = AngleNormalize360(yaw);
        roll  = AngleNormalize360(roll);
    }

    void vectors(Vec3* forward, Vec3* right, Vec3* up) const noexcept {
        vec3_t f, r, u;
        vec3_t a{pitch, yaw, roll};
        AngleVectors(a, f, r, u);
        if (forward) *forward = Vec3(f);
        if (right)   *right = Vec3(r);
        if (up)      *up = Vec3(u);
    }

    static Angles from_vector(const Vec3& forward) noexcept {
        vec3_t in{forward.x, forward.y, forward.z};
        vec3_t out;
        vectoangles(in, out);
        return Angles(out);
    }
};

} // namespace q3::math
