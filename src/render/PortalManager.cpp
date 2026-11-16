#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>

#include "components/RigidBodyComponent.hpp"
#include "render/PortalManager.hpp"

void PortalManager::initGL() {
  if (ssbo_ == 0) {
    glGenBuffers(1, &ssbo_);
  }
}

void PortalManager::destroyGL() {
  if (ssbo_ != 0) {
    glDeleteBuffers(1, &ssbo_);
    ssbo_ = 0;
  }
}

std::pair<int, int> PortalManager::addPortalPair(vec3 posA, vec4 rotA,
                                                 vec3 posB, vec4 rotB,
                                                 float radius) {
  int idxA = static_cast<int>(portals_.size());
  portals_.emplace_back(posA, rotA, radius);
  int idxB = static_cast<int>(portals_.size());
  portals_.emplace_back(posB, rotB, radius);

  portals_[idxA].linkedIndex = idxB;
  portals_[idxA].active = true;
  portals_[idxB].linkedIndex = idxA;
  portals_[idxB].active = true;

  return {idxA, idxB};
}

void PortalManager::setPortalTransform(int index, vec3 pos, vec4 rot) {
  portals_[index].position = pos;
  portals_[index].rotation = rot;
}

void PortalManager::setActive(int index, bool active) {
  portals_[index].active = active;
}

void PortalManager::update(const double camPos[3]) {
  gpuScratch_.resize(portals_.size());

  for (size_t i = 0; i < portals_.size(); i++) {
    const Portal &p = portals_[i];
    PortalGPU &gpu = gpuScratch_[i];

    bool linked = p.active && p.linkedIndex >= 0 &&
                  static_cast<size_t>(p.linkedIndex) < portals_.size() &&
                  portals_[p.linkedIndex].active;

    if (!linked) {
      gpu.posRadius[0] = gpu.posRadius[1] = gpu.posRadius[2] = 0.0f;
      gpu.posRadius[3] = 0.0f;
      gpu.ownNormal[0] = gpu.ownNormal[1] = 0.0f;
      gpu.ownNormal[2] = 1.0f;
      gpu.ownNormal[3] = 0.0f;
      gpu.rotToLinkedC0[0] = 1.0f;
      gpu.rotToLinkedC0[1] = 0.0f;
      gpu.rotToLinkedC0[2] = 0.0f;
      gpu.rotToLinkedC0[3] = 0.0f;
      gpu.rotToLinkedC1[0] = 0.0f;
      gpu.rotToLinkedC1[1] = 1.0f;
      gpu.rotToLinkedC1[2] = 0.0f;
      gpu.rotToLinkedC1[3] = 0.0f;
      gpu.rotToLinkedC2[0] = 0.0f;
      gpu.rotToLinkedC2[1] = 0.0f;
      gpu.rotToLinkedC2[2] = 1.0f;
      gpu.rotToLinkedC2[3] = 0.0f;
      gpu.linkedIndex = -1;
      gpu.active = 0;
      gpu._pad0 = gpu._pad1 = 0;
      continue;
    }

    const Portal &dst = portals_[p.linkedIndex];

    // Same construction as the CPU teleport transform: rotate from this
    // portal's local frame into the linked portal's local frame.
    mat3 Rown = getRot3x3(p.rotation);
    mat3 R = multiplyMat3(getRot3x3(dst.rotation), transposeMat3(Rown));

    // The shader operates relative to the camera, same convention the
    // old uPortal1Pos/uPortal2Pos uniforms used. Direction vectors (the
    // normal) don't need the camera offset, only positions do.
    gpu.posRadius[0] = static_cast<float>(p.position.x - camPos[0]);
    gpu.posRadius[1] = static_cast<float>(p.position.y - camPos[1]);
    gpu.posRadius[2] = static_cast<float>(p.position.z - camPos[2]);
    gpu.posRadius[3] = p.radius;

    // Local +Z (column 2) is this portal's own forward/normal, matching
    // the convention used elsewhere (getModel, getRot3x3).
    gpu.ownNormal[0] = Rown.m[6];
    gpu.ownNormal[1] = Rown.m[7];
    gpu.ownNormal[2] = Rown.m[8];
    gpu.ownNormal[3] = 0.0f;

    // mat3.m is column-major: m[0..2] = col0, m[3..5] = col1, m[6..8] = col2
    gpu.rotToLinkedC0[0] = R.m[0];
    gpu.rotToLinkedC0[1] = R.m[1];
    gpu.rotToLinkedC0[2] = R.m[2];
    gpu.rotToLinkedC0[3] = 0.0f;
    gpu.rotToLinkedC1[0] = R.m[3];
    gpu.rotToLinkedC1[1] = R.m[4];
    gpu.rotToLinkedC1[2] = R.m[5];
    gpu.rotToLinkedC1[3] = 0.0f;
    gpu.rotToLinkedC2[0] = R.m[6];
    gpu.rotToLinkedC2[1] = R.m[7];
    gpu.rotToLinkedC2[2] = R.m[8];
    gpu.rotToLinkedC2[3] = 0.0f;

    gpu.linkedIndex = p.linkedIndex;
    gpu.active = 1;
    gpu._pad0 = gpu._pad1 = 0;
  }

  if (ssbo_ == 0) {
    initGL();
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, gpuScratch_.size() * sizeof(PortalGPU),
               gpuScratch_.data(), GL_DYNAMIC_DRAW);
}

void PortalManager::bind(unsigned int bindingPoint) const {
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, ssbo_);
}

mat3 PortalManager::rotationToLinked(int portalIndex) const {
  const Portal &p = portals_[portalIndex];
  const Portal &dst = portals_[p.linkedIndex];
  return multiplyMat3(getRot3x3(dst.rotation),
                      transposeMat3(getRot3x3(p.rotation)));
}

// CPU mirror of portalExitPoint() in portal.glsl — see that function and
// PortalManager.hpp's class comment for the full explanation of why this
// isn't a point reflection.
vec3 PortalManager::exitPointFor(int portalIndex, vec3 p, vec3 dir) const {
  const Portal &src = portals_[portalIndex];
  const Portal &dst = portals_[src.linkedIndex];

  // A near-zero direction (e.g. a resting body, or a caller that didn't
  // have a meaningful velocity to supply) has nothing to follow through
  // — fall back to the direction pointing away from the source portal's
  // center, always well-defined for a point that's actually crossing.
  vec3 safeDir = length(dir) > 0.001f ? dir : (p - src.position);

  mat3 R = rotationToLinked(portalIndex);
  vec3 entry = dst.position + multMat3Vec3(R, p - src.position);
  vec3 dirB = normalize(multMat3Vec3(R, safeDir));

  // For debugging, remove
  return dst.position + dst.position - entry +
         0.01 * dirB; // offset to avoid re-triggering

  vec3 oc = entry - dst.position;
  float b = dot(oc, dirB);
  float c = dot(oc, oc) - dst.radius * dst.radius;
  float h = std::max(b * b - c, 0.0f);
  float tFar = -b + std::sqrt(h);

  return entry + dirB * tFar + dirB * 0.01f; // offset to avoid re-triggering
}

bool PortalManager::isInsideAnyPortal(vec3 pos) const {
  for (const Portal &p : portals_) {
    if (p.active && length(pos - p.position) <
                        p.radius) // slightly inside to avoid jitter at the edge
      return true;
  }
  return false;
}

int PortalManager::findTriggeredPortal(vec3 pos, int excludePortalIndex) const {
  for (size_t i = 0; i < portals_.size(); i++) {
    const Portal &p = portals_[i];
    if (!p.active || p.linkedIndex < 0)
      continue;
    if (static_cast<int>(i) == excludePortalIndex)
      continue; // just exited this one; don't immediately re-trigger

    if (length(pos - p.position) < p.radius)
      return static_cast<int>(i);
  }
  return -1;
}

void PortalManager::checkTeleport(double camPos[3], vec3 &camForward,
                                  vec3 &camRight, vec3 &camUp) {
  vec3 camG(static_cast<float>(camPos[0]), static_cast<float>(camPos[1]),
            static_cast<float>(camPos[2]));

  int idx = findTriggeredPortal(camG, mCameraLastPortal);

  if (idx >= 0) {
    mat3 R = rotationToLinked(idx);
    vec3 newG = exitPointFor(idx, camG, camForward);

    camForward = normalize(multMat3Vec3(R, camForward));
    camRight = normalize(multMat3Vec3(R, camRight));
    camUp = normalize(multMat3Vec3(R, camUp));

    camPos[0] = newG.x;
    camPos[1] = newG.y;
    camPos[2] = newG.z;

    // Landing already puts us past the linked portal's far surface, but
    // keep the exclusion as a safety net against any edge-case jitter
    // right at the boundary.
    mCameraLastPortal = portals_[idx].linkedIndex;
  } else if (!isInsideAnyPortal(camG)) {
    mCameraLastPortal = -1;
  }
}

void PortalManager::teleportBodies(
    std::vector<RigidBodyComponent *> &bodies) const {
  for (auto *bodyPtr : bodies) {
    auto &b = *bodyPtr;
    vec3 pos(b.pos[0], b.pos[1], b.pos[2]);

    int idx = findTriggeredPortal(pos, b.lastPortalIndex);

    if (idx >= 0) {
      mat3 R = rotationToLinked(idx);

      vec3 vel(b.vel[0], b.vel[1], b.vel[2]);
      vec3 newPos = exitPointFor(idx, pos, vel);

      vel = multMat3Vec3(R, vel);

      // Carry the body's own orientation through the same rotation, so
      // it doesn't visually "un-tumble" or snap to a different facing
      // the instant it crosses.
      vec4 q(b.rot[0], b.rot[1], b.rot[2], b.rot[3]);
      mat3 newOrientation = multiplyMat3(R, getRot3x3(q));
      vec4 newQ = quatFromMat3(newOrientation);

      b.pos[0] = newPos.x;
      b.pos[1] = newPos.y;
      b.pos[2] = newPos.z;
      b.vel[0] = vel.x;
      b.vel[1] = vel.y;
      b.vel[2] = vel.z;
      b.rot[0] = newQ.x;
      b.rot[1] = newQ.y;
      b.rot[2] = newQ.z;
      b.rot[3] = newQ.w;

      b.lastPortalIndex = portals_[idx].linkedIndex;
    } else if (!isInsideAnyPortal(pos)) {
      b.lastPortalIndex = -1;
    }
  }
}

vec3 PortalManager::warpPhysicsPoint(vec3 p, vec3 dir,
                                     int excludePortalIndex) const {
  int idx = findTriggeredPortal(p, excludePortalIndex);
  if (idx < 0)
    return p; // not inside any (other) portal's boundary

  return exitPointFor(idx, p, dir); // offset to avoid re-triggering
}
