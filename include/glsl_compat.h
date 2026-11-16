#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

extern std::vector<float> cpuMacroData;
extern int cpuCubeRes;

struct vec3 {
  float x, y, z;
  vec3() : x(0), y(0), z(0) {}
  vec3(float x, float y, float z) : x(x), y(y), z(z) {}
  vec3 operator+(const vec3 &o) const {
    return vec3(x + o.x, y + o.y, z + o.z);
  }
  vec3 operator-(const vec3 &o) const {
    return vec3(x - o.x, y - o.y, z - o.z);
  }
  vec3 operator-() const { return vec3(-x, -y, -z); }
  vec3 operator*(const vec3 &o) const {
    return vec3(x * o.x, y * o.y, z * o.z);
  }
  vec3 operator*(float s) const { return vec3(x * s, y * s, z * s); }
  vec3 operator/(float s) const { return vec3(x / s, y / s, z / s); }
  vec3 &operator+=(const vec3 &o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  vec3 &operator+=(float s) {
    x += s;
    y += s;
    z += s;
    return *this;
  }
  vec3 &operator-=(const vec3 &o) {
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
  }
  vec3 &operator*=(float s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }
};
inline vec3 operator*(float s, const vec3 &v) { return v * s; }
inline vec3 operator+(const vec3 &v, float s) {
  return vec3(v.x + s, v.y + s, v.z + s);
}
inline vec3 operator+(float s, const vec3 &v) { return v + s; }
inline vec3 operator-(float s, const vec3 &v) {
  return vec3(s - v.x, s - v.y, s - v.z);
}
inline vec3 operator-(const vec3 &v, float s) {
  return vec3(v.x - s, v.y - s, v.z - s);
}

struct vec2 {
  float x, y;
  vec3 xyy, yyx, yxy, xxx;
  vec2() : x(0), y(0), xyy(0, 0, 0), yyx(0, 0, 0), yxy(0, 0, 0), xxx(0, 0, 0) {}
  vec2(float x, float y)
      : x(x), y(y), xyy(x, y, y), yyx(y, y, x), yxy(y, x, y), xxx(x, x, x) {}
};

struct vec4 {
  float x, y, z, w;
  vec4() : x(0), y(0), z(0), w(0) {}
  vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
  vec4 operator+(const vec4 &o) const {
    return vec4(x + o.x, y + o.y, z + o.z, w + o.w);
  }
  vec4 operator*(float s) const { return vec4(x * s, y * s, z * s, w * s); }
  vec4 operator/(float s) const { return vec4(x / s, y / s, z / s, w / s); }
};

inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) {
  return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline float length(vec4 v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
}
inline float length(vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

inline vec3 normalize(vec3 v) {
  float l = length(v);
  return l > 0.0001f ? v / l : vec3(0, 1, 0);
}
inline vec4 normalize(vec4 q) {
  float l = length(q);
  if (l < 0.0001f)
    return vec4(0, 0, 0, 1);
  return vec4(q.x / l, q.y / l, q.z / l, q.w / l);
}

inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline vec3 mix(const vec3 &a, const vec3 &b, float t) {
  return a + (b - a) * t;
}
inline double abs(double v) { return std::fabs(v); }
inline float abs(float v) { return std::fabs(v); }
inline vec3 abs(vec3 v) {
  return vec3(std::fabs(v.x), std::fabs(v.y), std::fabs(v.z));
}
inline vec3 floor(vec3 v) {
  return vec3(std::floor(v.x), std::floor(v.y), std::floor(v.z));
}
inline float fract(float v) { return v - std::floor(v); }
inline vec3 fract(vec3 v) { return vec3(fract(v.x), fract(v.y), fract(v.z)); }
inline float min(float a, float b) { return std::min(a, b); }
inline float max(float a, float b) { return std::max(a, b); }
inline vec3 min(vec3 a, vec3 b) {
  return vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}
inline vec3 max(vec3 a, vec3 b) {
  return vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}
inline vec3 max(vec3 a, float s) {
  return vec3(std::max(a.x, s), std::max(a.y, s), std::max(a.z, s));
}

struct MatComp {
  vec4 c0, c1, c2, c3;
  MatComp() : c0(0, 0, 0, 0), c1(0, 0, 0, 0), c2(0, 0, 0, 0), c3(0, 0, 0, 0) {}
  MatComp(vec4 a, vec4 b, vec4 c, vec4 d) : c0(a), c1(b), c2(c), c3(d) {}
};

struct SDFRes {
  float d;
  MatComp comp;
  SDFRes() : d(0) {}
  SDFRes(float dist, MatComp c) : d(dist), comp(c) {}
};

inline vec2 operator+(const vec2 &a, const vec2 &b) {
  return vec2(a.x + b.x, a.y + b.y);
}
inline vec2 operator-(const vec2 &a, const vec2 &b) {
  return vec2(a.x - b.x, a.y - b.y);
}
inline vec2 operator*(const vec2 &a, const vec2 &b) {
  return vec2(a.x * b.x, a.y * b.y);
}
inline vec2 operator/(const vec2 &a, const vec2 &b) {
  return vec2(a.x / b.x, a.y / b.y);
}

inline vec2 operator*(const vec2 &v, float s) { return vec2(v.x * s, v.y * s); }
inline vec2 operator*(float s, const vec2 &v) { return vec2(v.x * s, v.y * s); }
inline vec2 operator/(const vec2 &v, float s) { return vec2(v.x / s, v.y / s); }
inline vec2 operator+(const vec2 &v, float s) { return vec2(v.x + s, v.y + s); }
