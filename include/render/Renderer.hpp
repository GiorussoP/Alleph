#pragma once
#define GL_GLEXT_PROTOTYPES 1
#include "MathUtil.hpp"
#include <SDL3/SDL_opengl.h>
#include <cmath>
#include <string>

class Renderer {
public:
  Renderer() = default;

  static mat4 getProj(float aspect, float fovY, float zNear, float zFar);

  static mat4 getView(vec3 right, vec3 up, vec3 forward);

  static mat4 getModel(vec3 pos, vec4 quat, vec3 scale);

  bool Initialize(float width, float height);
  void render();
  void resize(float width, float height);
  void Shutdown();

  float GetWidth() const { return mWidth; }
  float GetHeight() const { return mHeight; }

  // static GLuint loadMaterialTextureArray(const std::vector<std::string>
  // &paths);

private:
  float mWidth = 0.0f;
  float mHeight = 0.0f;
};