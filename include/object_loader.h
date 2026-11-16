#ifndef OBJECT_LOADER_H
#define OBJECT_LOADER_H

#include "glsl_compat.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

struct ModelData {
  GLuint vao;
  GLuint vbo;
  int vertexCount;
  // Max distance from the model's local-space pivot to any vertex.
  // Needed so distance-based culling (portal straddle/phantom passes)
  // can tell whether *any part* of a large object — a planet-encircling
  // cage, say, whose pivot might be nowhere near the point being tested
  // — could still be relevant, instead of only ever checking the pivot.
  float boundingRadius = 0.0f;
};

struct VisualInstance {
  ModelData *model;
  double pos[3];
  float rot[4];
  double scale[3];
  float color[3];
  bool alwaysLoaded;
  double maxLoadDist;
};

ModelData loadOBJ(const std::string &path) {
  std::vector<vec3> temp_vertices;
  std::vector<vec3> temp_normals;
  std::vector<float> vboData;

  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open model: " << path << std::endl;
    return {0, 0, 0, 0.0f};
  }

  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string type;
    iss >> type;

    if (type == "v") {
      vec3 v;
      iss >> v.x >> v.y >> v.z;
      temp_vertices.push_back(v);
    } else if (type == "vn") {
      vec3 vn;
      iss >> vn.x >> vn.y >> vn.z;
      temp_normals.push_back(vn);
    } else if (type == "f") {
      for (int i = 0; i < 3; ++i) {
        std::string vertexData;
        iss >> vertexData;

        std::replace(vertexData.begin(), vertexData.end(), '/', ' ');
        std::istringstream viss(vertexData);

        int vIdx = 0, tIdx = 0, nIdx = 0;
        viss >> vIdx;

        if (viss.peek() == ' ') {
          viss.ignore();
          if (viss.peek() != ' ')
            viss >> tIdx;
        }
        if (viss.peek() == ' ') {
          viss.ignore();
          viss >> nIdx;
        }

        if (vIdx > 0 && vIdx <= temp_vertices.size()) {
          vec3 v = temp_vertices[vIdx - 1];
          vboData.push_back(v.x);
          vboData.push_back(v.y);
          vboData.push_back(v.z);
        }

        if (nIdx > 0 && nIdx <= temp_normals.size()) {
          vec3 vn = temp_normals[nIdx - 1];
          vboData.push_back(vn.x);
          vboData.push_back(vn.y);
          vboData.push_back(vn.z);
        } else {
          vboData.push_back(0.0f);
          vboData.push_back(1.0f);
          vboData.push_back(0.0f);
        }
      }
    }
  }

  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float), vboData.data(),
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  float boundingRadiusSq = 0.0f;
  for (const vec3 &v : temp_vertices) {
    float distSq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (distSq > boundingRadiusSq)
      boundingRadiusSq = distSq;
  }

  return {vao, vbo, (int)(vboData.size() / 6), std::sqrt(boundingRadiusSq)};
}

#endif // OBJECT_LOADER_H