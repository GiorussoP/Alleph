#include "render/Renderer.hpp"

mat4 Renderer::getProj(float aspect, float fovY, float zNear, float zFar) {

  mat4 res = {};
  float f = 1.0f / std::tan(fovY * 0.5f);
  res.m[0] = f / aspect;
  res.m[5] = f;
  res.m[10] = -(zFar + zNear) / (zFar - zNear);
  res.m[11] = -1.0f;
  res.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
  return res;
}

mat4 Renderer::getView(vec3 right, vec3 up, vec3 forward) {
  mat4 res = identity();
  res.m[0] = right.x;
  res.m[4] = right.y;
  res.m[8] = right.z;
  res.m[1] = up.x;
  res.m[5] = up.y;
  res.m[9] = up.z;
  res.m[2] = -forward.x;
  res.m[6] = -forward.y;
  res.m[10] = -forward.z;
  return res;
}

mat4 Renderer::getModel(vec3 pos, vec4 quat, vec3 scale) {
  mat4 res = identity();
  float x2 = quat.x + quat.x, y2 = quat.y + quat.y, z2 = quat.z + quat.z;
  float xx = quat.x * x2, xy = quat.x * y2, xz = quat.x * z2;
  float yy = quat.y * y2, yz = quat.y * z2, zz = quat.z * z2;
  float wx = quat.w * x2, wy = quat.w * y2, wz = quat.w * z2;

  res.m[0] = (1.0f - (yy + zz)) * scale.x;
  res.m[1] = (xy + wz) * scale.x;
  res.m[2] = (xz - wy) * scale.x;

  res.m[4] = (xy - wz) * scale.y;
  res.m[5] = (1.0f - (xx + zz)) * scale.y;
  res.m[6] = (yz + wx) * scale.y;

  res.m[8] = (xz + wy) * scale.z;
  res.m[9] = (yz - wx) * scale.z;
  res.m[10] = (1.0f - (xx + yy)) * scale.z;

  res.m[12] = pos.x;
  res.m[13] = pos.y;
  res.m[14] = pos.z;
  return res;
}

bool Renderer::Initialize(float width, float height) {
  mWidth = width;
  mHeight = height;

  return true;
}

void Renderer::Shutdown() {}

void Renderer::render() {
  // Rendering logic would go here
}

void Renderer::resize(float width, float height) {
  mWidth = width;
  mHeight = height;
}