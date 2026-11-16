#pragma once

#include "glsl_compat.h"

// Small 3x3 rotation-matrix helpers, column-major (matches GLSL's mat3
// layout). Pulled out of Game.cpp so PortalManager can share them instead
// of keeping a second copy in sync by hand.
struct mat3 {
  float m[9];
};

// --- Matrix Utilities for Rasterization ---
struct mat4 {
  float m[16];
};

inline mat3 getRot3x3(vec4 quat) {
  mat3 res = {};
  float x2 = quat.x + quat.x, y2 = quat.y + quat.y, z2 = quat.z + quat.z;
  float xx = quat.x * x2, xy = quat.x * y2, xz = quat.x * z2;
  float yy = quat.y * y2, yz = y2 * quat.z, zz = quat.z * z2;
  float wx = quat.w * x2, wy = quat.w * y2, wz = quat.w * z2;
  // Column-major layout for GLSL
  res.m[0] = 1.0f - (yy + zz);
  res.m[3] = xy - wz;
  res.m[6] = xz + wy;
  res.m[1] = xy + wz;
  res.m[4] = 1.0f - (xx + zz);
  res.m[7] = yz - wx;
  res.m[2] = xz - wy;
  res.m[5] = yz + wx;
  res.m[8] = 1.0f - (xx + yy);
  return res;
}

inline mat3 transposeMat3(mat3 a) {
  mat3 res = {};
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < 3; r++)
      res.m[r * 3 + c] = a.m[c * 3 + r];
  return res;
}

inline mat3 multiplyMat3(mat3 a, mat3 b) {
  mat3 res = {};
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < 3; r++)
      res.m[c * 3 + r] = a.m[0 * 3 + r] * b.m[c * 3 + 0] +
                         a.m[1 * 3 + r] * b.m[c * 3 + 1] +
                         a.m[2 * 3 + r] * b.m[c * 3 + 2];
  return res;
}

inline vec3 multMat3Vec3(mat3 R, vec3 v) {
  return vec3(R.m[0] * v.x + R.m[3] * v.y + R.m[6] * v.z,
              R.m[1] * v.x + R.m[4] * v.y + R.m[7] * v.z,
              R.m[2] * v.x + R.m[5] * v.y + R.m[8] * v.z);
}

inline vec3 rotateAxis(vec3 v, vec3 k, float theta) {
  float cosT = std::cos(theta);
  float sinT = std::sin(theta);
  return v * cosT + cross(k, v) * sinT + k * dot(k, v) * (1.0f - cosT);
}

// Inverse of getRot3x3 — standard trace-based (Shepperd's method)
// conversion, robust across all rotations. Needed to carry a rigid
// body's own orientation through a portal: we compute the body's new
// world-space rotation matrix (portal rotation composed with the body's
// existing one) and need it back as a quaternion to store in RigidBody's
// rot[4].
inline vec4 quatFromMat3(mat3 m) {
  float m00 = m.m[0], m10 = m.m[1], m20 = m.m[2];
  float m01 = m.m[3], m11 = m.m[4], m21 = m.m[5];
  float m02 = m.m[6], m12 = m.m[7], m22 = m.m[8];
  float trace = m00 + m11 + m22;
  vec4 q;
  if (trace > 0.0f) {
    float s = 0.5f / std::sqrt(trace + 1.0f);
    q.w = 0.25f / s;
    q.x = (m21 - m12) * s;
    q.y = (m02 - m20) * s;
    q.z = (m10 - m01) * s;
  } else if (m00 > m11 && m00 > m22) {
    float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
    q.w = (m21 - m12) / s;
    q.x = 0.25f * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25f * s;
    q.z = (m12 + m21) / s;
  } else {
    float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25f * s;
  }
  return normalize(q);
}

inline mat4 identity() {
  mat4 res = {};
  res.m[0] = res.m[5] = res.m[10] = res.m[15] = 1.0f;
  return res;
}

// Hamilton product: rotateQuat(v, quatMultiply(a, b)) ==
// rotateQuat(rotateQuat(v, b), a). i.e. quatMultiply(a, b) applies b first,
// then a. Used to compose a joint's local hinge rotation on top of its parent's
// world orientation for the articulated-body forward-kinematics chain.
inline vec4 quatMultiply(vec4 a, vec4 b) {
  return vec4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
              a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

// Unit quaternion for a rotation of `angle` radians about `axis`
// (assumed normalized).
inline vec4 quatFromAxisAngle(vec3 axis, float angle) {
  float h = angle * 0.5f;
  float s = std::sin(h);
  return vec4(axis.x * s, axis.y * s, axis.z * s, std::cos(h));
}
