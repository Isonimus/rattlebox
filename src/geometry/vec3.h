#ifndef VEC3_H
#define VEC3_H

#include <math.h>

struct Vec3 {
    float x;
    float y;
    float z;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(Vec3 v, float scale) { return {v.x * scale, v.y * scale, v.z * scale}; }
constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) { return sqrtf(dot(v, v)); }
inline Vec3 normalized(Vec3 v) { return v * (1.0f / length(v)); }

#endif // VEC3_H
