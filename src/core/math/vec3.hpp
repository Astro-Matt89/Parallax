#pragma once
// core/math/vec3.hpp - 3D vector type used throughout Parallax

#include <cmath>
#include <ostream>

namespace parallax {

struct Vec3 {
    double x{0.0}, y{0.0}, z{0.0};

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s)       const { return {x * s,   y * s,   z * s};   }
    Vec3 operator/(double s)       const { return {x / s,   y / s,   z / s};   }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s)      { x *= s;   y *= s;   z *= s;   return *this; }

    double dot(const Vec3& o)  const { return x*o.x + y*o.y + z*o.z; }
    Vec3   cross(const Vec3& o) const {
        return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x};
    }
    double length()  const { return std::sqrt(dot(*this)); }
    double length2() const { return dot(*this); }
    Vec3   normalized() const {
        double len = length();
        return (len > 0.0) ? (*this / len) : Vec3{};
    }

    bool operator==(const Vec3& o) const { return x==o.x && y==o.y && z==o.z; }

    friend std::ostream& operator<<(std::ostream& os, const Vec3& v) {
        return os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    }
};

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

} // namespace parallax
