#pragma once

#include <cmath>
#include <string>
#include <cstdio>

namespace npc {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }

    float distanceTo(const Vec2& o) const { return (*this - o).length(); }
    float distanceSquaredTo(const Vec2& o) const { return (*this - o).lengthSquared(); }

    Vec2 normalized() const {
        float len = length();
        if (len < 1e-6f) return {0.0f, 0.0f};
        return *this / len;
    }

    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    float cross(const Vec2& o) const { return x * o.y - y * o.x; }

    float angleTo(const Vec2& o) const {
        return std::atan2(cross(o), dot(o));
    }

    Vec2 rotated(float radians) const {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return {x * c - y * s, x * s + y * c};
    }

    Vec2 lerp(const Vec2& target, float t) const {
        return *this + (target - *this) * t;
    }

    std::string toString() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "(%.1f, %.1f)", x, y);
        return std::string(buf);
    }

    // Grid coordinate helpers
    int gridX() const { return static_cast<int>(std::round(x)); }
    int gridY() const { return static_cast<int>(std::round(y)); }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

} // namespace npc
