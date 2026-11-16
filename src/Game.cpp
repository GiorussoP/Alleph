#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "AssetLoader.hpp"
#include "Game.hpp"
#include "actors/Actor.hpp"
#include "assets/shaders/sdf.glsl"
#include "components/ColliderComponent.hpp"
#include "components/MeshComponent.hpp"
#include "components/RigidBodyComponent.hpp"
#include "creatures/Brain.hpp"
#include "creatures/CreatureComponent.hpp"
#include "glsl_compat.h"
#include "object_loader.h"
#include "render/MathUtil.hpp"
#include "render/PortalManager.hpp"
#include "render/Renderer.hpp"

vec3 planetCenter;
float cpuTime = 0.0f;
int cpuCubeRes = 2048;
std::vector<float> cpuMacroData;

// --- Matrix Utilities for Rasterization ---
/*
mat4 getProj(float aspect, float fovY, float zNear, float zFar) {
  mat4 res = {};
  float f = 1.0f / std::tan(fovY * 0.5f);
  res.m[0] = f / aspect;
  res.m[5] = f;
  res.m[10] = -(zFar + zNear) / (zFar - zNear);
  res.m[11] = -1.0f;
  res.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
  return res;
}

mat4 getView(vec3 right, vec3 up, vec3 forward) {
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

// Matrix utilities for portals now live in render/MathUtil.hpp so
// PortalManager can share them instead of keeping a second copy.

mat4 getModel(vec3 pos, vec4 quat, vec3 scale) {
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
  */

// --- Mesh Generation ---
void generateMeshes(std::vector<float> &vbo, int &cubeOffset, int &cubeCount,
                    int &sphereOffset, int &sphereCount) {
  cubeOffset = 0;
  vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {-1, 0, 0},
               {1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
  vec3 p[6][4] = {{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
                  {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
                  {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
                  {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
                  {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
                  {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}};
  int indices[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 6; j++) {
      vec3 v = p[i][indices[j]];
      vbo.push_back(v.x);
      vbo.push_back(v.y);
      vbo.push_back(v.z);
      vbo.push_back(n[i].x);
      vbo.push_back(n[i].y);
      vbo.push_back(n[i].z);
    }
  }
  cubeCount = 36;
  sphereOffset = vbo.size() / 6;

  int rings = 16, sectors = 16;
  for (int r = 0; r < rings; r++) {
    for (int s = 0; s < sectors; s++) {
      float theta1 = r * 3.1415926f / rings,
            theta2 = (r + 1) * 3.1415926f / rings;
      float phi1 = s * 2 * 3.1415926f / sectors,
            phi2 = (s + 1) * 2 * 3.1415926f / sectors;

      auto getP = [](float t, float ph) {
        return vec3(sin(t) * cos(ph), cos(t), sin(t) * sin(ph));
      };
      vec3 v0 = getP(theta1, phi1);
      vec3 v1 = getP(theta2, phi1);
      vec3 v2 = getP(theta2, phi2);
      vec3 v3 = getP(theta1, phi2);
      vec3 pts[6] = {v0, v1, v2, v0, v2, v3};
      for (int k = 0; k < 6; k++) {
        vbo.push_back(pts[k].x);
        vbo.push_back(pts[k].y);
        vbo.push_back(pts[k].z);
        vbo.push_back(pts[k].x);
        vbo.push_back(pts[k].y);
        vbo.push_back(pts[k].z);
      }
    }
  }
  sphereCount = (vbo.size() / 6) - sphereOffset;
}

GLuint loadMaterialTextureArray(const std::vector<std::string> &paths) {
  constexpr int TEX_SIZE = 1024;
  constexpr int LAYERS = 16;

  if ((int)paths.size() != LAYERS) {
    std::cerr << "Expected exactly " << LAYERS << " material textures."
              << std::endl;
    return 0;
  }

  GLuint textureArray = 0;
  glGenTextures(1, &textureArray);
  glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, TEX_SIZE, TEX_SIZE, LAYERS);

  for (int layer = 0; layer < LAYERS; ++layer) {
    SDL_Surface *loaded = IMG_Load(paths[layer].c_str());
    if (!loaded)
      return 0;
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba)
      return 0;

    glTextureSubImage3D(textureArray, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    SDL_DestroySurface(rgba);
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  return textureArray;
}

enum CameraMode { FREE_CAM = 0, PLANET_ALIGNED, SURFACE_ALIGNED };

struct RigidBodyGPU {
  float pos[3];
  float type;
  float extents[3];
  float mass;
  float vel[3];
  float restitution;
  float rot[4];
  float ang_vel[3];
  float pad;
};

std::string loadShaderSource(const std::string &filepath) {
  std::ifstream file(filepath);
  std::stringstream buffer;
  std::string line;
  while (std::getline(file, line)) {
    // Recognizes `#include "name.glsl"` for any local file under
    // shaders/, not just sdf.glsl, so render.frag can also
    // `#include "portal.glsl"`.
    const std::string prefix = "#include \"";
    if (line.rfind(prefix, 0) == 0) {
      size_t start = prefix.size();
      size_t end = line.find('"', start);
      if (end != std::string::npos) {
        std::string includeName = line.substr(start, end - start);
        buffer << loadShaderSource(getAssetPath("shaders/" + includeName))
               << "\n";
        continue;
      }
    }
    buffer << line << "\n";
  }
  return buffer.str();
}

GLuint compileShader(GLenum type, const std::string &source) {
  GLuint shader = glCreateShader(type);
  const char *src = source.c_str();
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

    std::vector<char> infoLog(logLength);
    glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());

    std::cerr << "ERROR::SHADER::COMPILATION_FAILED (Type: " << type << ")\n"
              << infoLog.data() << std::endl;

    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

void spawnBody(Game *game, std::vector<Actor *> &bodyActors,
               std::vector<RigidBodyComponent *> &bodies) {
  auto *actor = new Actor(game);
  auto *body = new RigidBodyComponent(actor, 1.0f, 0.0f, false);
  body->type = (rand() % 2 == 0) ? 1.0f : 0.0f;
  float theta = (rand() % 360) * 3.1415f / 180.0f;
  float phi = (rand() % 360) * 3.1415f / 180.0f;
  float r = 5.0f + (rand() % 2);
  body->pos[0] = r * sin(theta) * cos(phi);
  body->pos[1] = r * sin(theta) * sin(phi);
  body->pos[2] = r * cos(theta);
  body->extents[0] = 0.25f + 3.0f * (rand() % 100) / 100.0f;
  body->extents[1] = 0.25f + 3.0f * (rand() % 100) / 100.0f;
  body->extents[2] = 0.25f + 3.0f * (rand() % 100) / 100.0f;
  body->mMass = body->extents[0] * body->extents[1] * body->extents[2];
  body->mass = body->mMass;
  body->mVelocity = vec3(0.0f, 0.0f, 0.0f);
  body->vel[0] = body->vel[1] = body->vel[2] = 0.0f;
  body->restitution = 0.5f;
  body->rot[0] = body->rot[1] = body->rot[2] = 0.0f;
  body->rot[3] = 1.0f;
  body->ang_vel[0] = ((rand() % 100) / 50.0f) - 1.0f;
  body->ang_vel[1] = ((rand() % 100) / 50.0f) - 1.0f;
  body->ang_vel[2] = ((rand() % 100) / 50.0f) - 1.0f;
  actor->SetPosition(vec3(body->pos[0], body->pos[1], body->pos[2]));
  actor->SetRotation(
      vec4(body->rot[0], body->rot[1], body->rot[2], body->rot[3]));

  if (body->type < 0.5f) {
    new SphereCollider(actor, ColliderLayer::World, vec3(0.0f, 0.0f, 0.0f),
                       (body->extents[0]), false);
  } else {
    new OBBCollider(actor, ColliderLayer::World, vec3(0.0f, 0.0f, 0.0f),
                    vec3(body->extents[0], body->extents[1], body->extents[2]),
                    false);
  }

  bodyActors.push_back(actor);
  bodies.push_back(body);
}

void removeBody(std::vector<Actor *> &bodyActors,
                std::vector<RigidBodyComponent *> &bodies, size_t index) {
  if (index < bodies.size()) {
    delete bodyActors[index];
    bodies.erase(bodies.begin() + index);
    bodyActors.erase(bodyActors.begin() + index);
  }
}

// =============================================================================
// LIFE SIMULATION ECOSYSTEM (see life_simulator_plan.md)
// =============================================================================
//
// Everything below is CPU-side, driven directly from the same per-frame
// loop that already runs updatePhysicsCPU — it doesn't go through
// Game::UpdateActors()/Component::Update() because RunLoop never calls
// that path today (bodies are managed as flat vectors, mirroring
// updatePhysicsCPU's own style).
//
// Two-tier population, so the planet can hold thousands of creatures
// while only ~kMaxActiveCreatures are ever actually ticked:
//
//   - `life::CreatureRecord`: always resident (DNA, brains, energy,
//     world-space pos/rot, tag). The planet-wide population lives here.
//   - Live Actor tree (root Actor + one Actor per limb, each with a
//     RigidBodyComponent/OBBCollider/LimbComponent, plus a
//     CreatureComponent on the root): constructed by activateCreature()
//     only for records within kActivationRadius of the camera, and torn
//     back down by deactivateCreature() once a creature drifts past
//     kDeactivationRadius. Only active creatures appear in `bodies` and
//     therefore only they get physics, brains, or the energy economy —
//     see updateActivation() below, called once per frame from RunLoop.
//
// Articulated bodies: each creature is a DAG of OBBs (CreatureDNA::limbs,
// limb 0 = root). The root is a normal free RigidBodyComponent (gravity,
// terrain collision, and the original locomotion brain drive it exactly
// like the single-OBB version of this simulation did). Every other limb
// is positioned purely kinematically each frame by updateLimbKinematics():
// parent transform + a per-joint hinge angle that a tiny per-joint brain
// drives toward a target via an actual PD controller (kJointKp/kJointKd),
// exactly matching Step 2.3 ("Target_Angle passed to the local physical
// PD Controller") — the PD just integrates a scalar angle instead of
// feeding into a 6DOF constraint solver. This trades true unconstrained
// rigid-body articulation for stability and simplicity: no risk of limbs
// drifting apart or a soft-constraint solver blowing up, at the cost of
// limbs not independently colliding/bouncing off obstacles (they still
// register hits for predation/scavenging via updatePhysicsCPU's
// body-to-body pass, just don't independently free-fall or ragdoll apart
// from momentum, though a dead creature's own root still does).
//
// LOD simplification, stated plainly: a dormant CreatureRecord is frozen
// — it does not age, eat, move, or think while outside the activation
// radius, and resumes exactly where it left off once reactivated. This
// is a deliberate scope decision rather than an attempt at an offline
// population model; updateActivation() is where a cheap "catch-up" model
// (e.g. approximate energy decay from elapsed time) could be added later
// without touching anything else.

int dominantMaterialCPU(const SDFResult &pd) {
  int domMat = 0;
  float maxW = -1.0f;
  for (int i = 0; i < 16; i++) {
    if (pd.mat[i] > maxW) {
      if (i >= 12 && pd.mat[i] < 0.9f)
        continue; // Skip liquid if it isn't REALLY liquid
      maxW = pd.mat[i];
      domMat = i;
    }
  }
  return domMat;
}

void updateLimbKinematics(std::vector<RigidBodyComponent *> &bodies, float dt);

// Nearest other creature ROOT within a forward vision cone (Step 2.2's
// Raycast_Sensor). Only root bodies are considered (not every limb) so a
// creature is "seen" once, from its torso, rather than once per limb.
// This is a nearest-neighbour scan rather than a true raycast against
// OBBs/terrain — cheap and good enough at the (now capped, thanks to the
// LOD system) handful of simultaneously-active creatures, but is O(n)
// per creature per step.
struct CreatureVision {
  float dist = 1.0f; // normalized [0,1], 1.0 = nothing in range
  bool sawLiving = false;
  bool sawCorpse = false;
  float relVel = 0.0f; // normalized closing speed
};

CreatureVision senseVision(const std::vector<RigidBodyComponent *> &bodies,
                           size_t selfIdx, vec3 forward) {
  CreatureVision result;
  float bestRawDist = life::kVisionRange;
  bool found = false;

  const auto &self = *bodies[selfIdx];
  vec3 selfPos(self.pos[0], self.pos[1], self.pos[2]);
  vec3 selfVel(self.vel[0], self.vel[1], self.vel[2]);

  for (size_t j = 0; j < bodies.size(); j++) {
    if (j == selfIdx)
      continue;
    auto *otherLimb =
        bodies[j]->GetOwner()->GetComponent<life::LimbComponent>();
    if (!otherLimb || otherLimb->limbIndex != 0)
      continue; // only root bodies count as "a creature" for vision
    auto *other =
        bodies[j]->GetOwner()->GetComponent<life::CreatureComponent>();
    if (!other)
      continue;

    vec3 otherPos(bodies[j]->pos[0], bodies[j]->pos[1], bodies[j]->pos[2]);
    vec3 toOther = otherPos - selfPos;
    float d = length(toOther);
    if (d < 0.001f || d > life::kVisionRange)
      continue;

    vec3 dir = toOther / d;
    if (dot(dir, forward) < life::kVisionCosHalfAngle)
      continue;

    if (d < bestRawDist) {
      bestRawDist = d;
      found = true;
      vec3 otherVel(bodies[j]->vel[0], bodies[j]->vel[1], bodies[j]->vel[2]);
      result.dist = d / life::kVisionRange;
      result.sawLiving = other->IsAlive();
      result.sawCorpse = !other->IsAlive();
      result.relVel = std::clamp(
          dot(selfVel - otherVel, dir) / life::kMaxForwardSpeed, -1.0f, 1.0f);
    }
  }

  if (!found)
    result = CreatureVision{};
  return result;
}

// Builds the live Actor tree (root + every limb) for a CreatureRecord
// and adds it to `bodies`/`bodyActors`, converting the record's
// world-space transform into the camera-relative frame everything in
// `bodies` uses. Leaves `record->active = true` and
// `record->rootActor` pointing at the new root.
void activateCreature(Game *game, life::CreatureRecord *record,
                      vec3 camPosWorld, std::vector<Actor *> &bodyActors,
                      std::vector<RigidBodyComponent *> &bodies) {
  if (record->jointBrains.size() != record->dna.limbs.size())
    record->jointBrains.resize(record->dna.limbs.size());
  if (record->limbAngles.size() != record->dna.limbs.size())
    record->limbAngles.assign(record->dna.limbs.size(), 0.0f);

  vec3 localPos = record->pos - camPosWorld;
  vec4 rot = record->rot;
  const life::LimbDNA &rootDna = record->dna.limbs[0];

  auto *rootActor = new Actor(game);
  auto *rootBody =
      new RigidBodyComponent(rootActor, /*mass=*/1.0f, 0.0f, false);
  rootBody->type = 1.0f; // OBB
  rootBody->pos[0] = localPos.x;
  rootBody->pos[1] = localPos.y;
  rootBody->pos[2] = localPos.z;
  rootBody->extents[0] = rootDna.extents.x;
  rootBody->extents[1] = rootDna.extents.y;
  rootBody->extents[2] = rootDna.extents.z;
  rootBody->mMass = std::max(0.05f, 8.0f * rootDna.extents.x *
                                        rootDna.extents.y * rootDna.extents.z);
  rootBody->mass = rootBody->mMass;
  rootBody->vel[0] = rootBody->vel[1] = rootBody->vel[2] = 0.0f;
  rootBody->restitution = 0.15f;
  rootBody->rot[0] = rot.x;
  rootBody->rot[1] = rot.y;
  rootBody->rot[2] = rot.z;
  rootBody->rot[3] = rot.w;
  rootBody->ang_vel[0] = rootBody->ang_vel[1] = rootBody->ang_vel[2] = 0.0f;
  rootActor->SetPosition(localPos);
  rootActor->SetRotation(rot);
  new OBBCollider(rootActor, ColliderLayer::Creature, vec3(0.0f, 0.0f, 0.0f),
                  rootDna.extents, false);

  auto *creatureComp = new life::CreatureComponent(rootActor, record);
  new life::LimbComponent(rootActor, rootActor, 0);
  creatureComp->limbActors.push_back(rootActor);
  bodyActors.push_back(rootActor);
  bodies.push_back(rootBody);

  for (size_t i = 1; i < record->dna.limbs.size(); i++) {
    const life::LimbDNA &limbDna = record->dna.limbs[i];
    Actor *parentActor = creatureComp->limbActors[limbDna.parentIndex];
    auto *parentBody = parentActor->GetComponent<RigidBodyComponent>();

    auto *limbActor = new Actor(game);
    auto *limbBody =
        new RigidBodyComponent(limbActor, /*mass=*/1.0f, 0.0f, false);
    limbBody->type = 1.0f;
    limbBody->extents[0] = limbDna.extents.x;
    limbBody->extents[1] = limbDna.extents.y;
    limbBody->extents[2] = limbDna.extents.z;
    limbBody->mMass =
        std::max(0.02f, 8.0f * limbDna.extents.x * limbDna.extents.y *
                            limbDna.extents.z);
    limbBody->mass = limbBody->mMass;
    limbBody->vel[0] = limbBody->vel[1] = limbBody->vel[2] = 0.0f;
    limbBody->ang_vel[0] = limbBody->ang_vel[1] = limbBody->ang_vel[2] = 0.0f;
    limbBody->restitution = 0.15f;
    // Seeded from the parent so nothing is uninitialized before
    // updateLimbKinematics (called at the bottom of this function)
    // places it correctly.
    if (parentBody) {
      limbBody->pos[0] = parentBody->pos[0];
      limbBody->pos[1] = parentBody->pos[1];
      limbBody->pos[2] = parentBody->pos[2];
      limbBody->rot[0] = parentBody->rot[0];
      limbBody->rot[1] = parentBody->rot[1];
      limbBody->rot[2] = parentBody->rot[2];
      limbBody->rot[3] = parentBody->rot[3];
    }

    new OBBCollider(limbActor, ColliderLayer::Creature, vec3(0.0f, 0.0f, 0.0f),
                    limbDna.extents, false);
    auto *limbComp = new life::LimbComponent(limbActor, rootActor, (int)i);
    limbComp->parentBody = parentBody;
    limbComp->currentAngle = record->limbAngles[i];
    limbComp->angularRate = 0.0f;

    creatureComp->limbActors.push_back(limbActor);
    bodyActors.push_back(limbActor);
    bodies.push_back(limbBody);
  }

  record->active = true;
  record->rootActor = rootActor;

  // Snap every limb straight to its correct pose immediately so the
  // creature doesn't render collapsed at the root for one frame.
  updateLimbKinematics(bodies, 0.0f);
}

// Writes a creature's current live state back into its CreatureRecord
// (world-space position, rotation, and each limb's hinge angle) and
// tears down its Actor tree. The record survives, dormant, in
// `population`.
void deactivateCreature(life::CreatureRecord *record, vec3 camPosWorld,
                        std::vector<Actor *> &bodyActors,
                        std::vector<RigidBodyComponent *> &bodies) {
  Actor *rootActor = record->rootActor;
  if (!rootActor) {
    record->active = false;
    return;
  }
  auto *creatureComp = rootActor->GetComponent<life::CreatureComponent>();
  auto *rootBody = rootActor->GetComponent<RigidBodyComponent>();

  if (creatureComp && rootBody) {
    record->pos = vec3(rootBody->pos[0], rootBody->pos[1], rootBody->pos[2]) +
                  camPosWorld;
    record->rot = vec4(rootBody->rot[0], rootBody->rot[1], rootBody->rot[2],
                       rootBody->rot[3]);
    for (size_t i = 1; i < creatureComp->limbActors.size(); i++) {
      auto *limbComp =
          creatureComp->limbActors[i]->GetComponent<life::LimbComponent>();
      if (limbComp && i < record->limbAngles.size())
        record->limbAngles[i] = limbComp->currentAngle;
    }

    // Copy first: `creatureComp` (and the vector it owns) is destroyed
    // partway through the deletions below, once we reach the root actor.
    std::vector<Actor *> limbActorsCopy = creatureComp->limbActors;
    for (Actor *limbActor : limbActorsCopy) {
      for (size_t idx = 0; idx < bodyActors.size(); idx++) {
        if (bodyActors[idx] == limbActor) {
          delete bodyActors[idx];
          bodyActors.erase(bodyActors.begin() + idx);
          bodies.erase(bodies.begin() + idx);
          break;
        }
      }
    }
  }

  record->active = false;
  record->rootActor = nullptr;
}

// Permanently removes a creature: tears down its live Actor tree (if
// any) and erases its CreatureRecord from the planet-wide population.
// Used when a corpse is fully scavenged (Step 1.5) — as opposed to
// deactivateCreature(), which keeps the record around, dormant.
void destroyCreature(life::CreatureRecord *record,
                     std::vector<life::CreatureRecord *> &population,
                     std::vector<Actor *> &bodyActors,
                     std::vector<RigidBodyComponent *> &bodies) {
  if (record->active && record->rootActor) {
    auto *creatureComp =
        record->rootActor->GetComponent<life::CreatureComponent>();
    if (creatureComp) {
      std::vector<Actor *> limbActorsCopy = creatureComp->limbActors;
      for (Actor *limbActor : limbActorsCopy) {
        for (size_t idx = 0; idx < bodyActors.size(); idx++) {
          if (bodyActors[idx] == limbActor) {
            delete bodyActors[idx];
            bodyActors.erase(bodyActors.begin() + idx);
            bodies.erase(bodies.begin() + idx);
            break;
          }
        }
      }
    }
  }
  auto it = std::find(population.begin(), population.end(), record);
  if (it != population.end())
    population.erase(it);
  delete record;
}

// Scatters `count` generation-0 CreatureRecords across the planet
// surface (world space), all dormant. Snaps each spawn point onto the
// (non-spherical) terrain via a few Newton steps along the SDF normal,
// the same trick RunLoop already uses to depenetrate the camera. This is
// the ONLY place the whole planet's population is touched at once —
// it runs once at startup, so its cost doesn't matter.
void seedPlanetPopulation(std::vector<life::CreatureRecord *> &population,
                          int count, uint32_t &rngState) {
  for (int i = 0; i < count; i++) {
    vec3 dir = normalize(vec3(life::randfRange(rngState, -1.0f, 1.0f),
                              life::randfRange(rngState, -1.0f, 1.0f),
                              life::randfRange(rngState, -1.0f, 1.0f)));
    vec3 pos = dir * 6300.0f;
    for (int iter = 0; iter < 10; iter++) {
      SDFResult pd = mapPlanet(pos, cpuTime);
      vec3 n = calcPlanetNormal(pos, cpuTime);
      pos = pos + n * (pd.dist - 1.0f);
    }

    auto *record = new life::CreatureRecord();
    record->dna = life::makeDefaultCreatureDNA(rngState);
    life::randomizeBrain(record->rootBrain, rngState);
    record->jointBrains.resize(record->dna.limbs.size());
    record->limbAngles.assign(record->dna.limbs.size(), 0.0f);
    for (size_t j = 1; j < record->dna.limbs.size(); j++)
      life::randomizeJointBrain(record->jointBrains[j], rngState);
    record->energy = life::kStartEnergy;
    record->tag = life::CreatureTag::Living;
    record->generation = 0;
    record->rngState = life::xorshift32(rngState);
    record->pos = pos;
    record->rot = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    record->cpgPhase = life::randfRange(rngState, 0.0f, 6.28318530718f);
    population.push_back(record);
  }
}

// The simulation-LOD manager: activates dormant records that drifted
// within kActivationRadius of the camera (up to the kMaxActiveCreatures
// cap) and deactivates active ones that drifted past
// kDeactivationRadius. Called once per frame from RunLoop, before
// physics — see the integration note at the top of this section.
void updateActivation(Game *game,
                      std::vector<life::CreatureRecord *> &population,
                      vec3 camPosWorld, std::vector<Actor *> &bodyActors,
                      std::vector<RigidBodyComponent *> &bodies) {
  int activeCount = 0;
  for (auto *record : population)
    if (record->active)
      activeCount++;

  for (auto *record : population) {
    float d = length(record->pos - camPosWorld);
    if (record->active) {
      if (d > life::kDeactivationRadius) {
        deactivateCreature(record, camPosWorld, bodyActors, bodies);
        activeCount--;
      }
    } else if (activeCount < life::kMaxActiveCreatures &&
               d < life::kActivationRadius) {
      activateCreature(game, record, camPosWorld, bodyActors, bodies);
      activeCount++;
    }
  }
}

// Forward-kinematic pass for every non-root limb: world transform =
// parent's transform, offset by a hinge rotation (about the joint axis,
// expressed in the parent's frame) of `LimbComponent::currentAngle`
// radians. Requires parents to appear before children within each
// creature's contiguous run of bodies, which activateCreature() (the
// only thing that ever appends limb bodies) guarantees by always
// building the DAG in increasing limb-index order.
void updateLimbKinematics(std::vector<RigidBodyComponent *> &bodies, float dt) {
  for (auto *bodyPtr : bodies) {
    auto &body = *bodyPtr;
    auto *limbComp = body.GetOwner()->GetComponent<life::LimbComponent>();
    if (!limbComp || limbComp->limbIndex == 0)
      continue; // root, or not part of a creature at all
    RigidBodyComponent *parentBody = limbComp->parentBody;
    if (!parentBody)
      continue;
    auto *creatureComp =
        limbComp->rootActor->GetComponent<life::CreatureComponent>();
    if (!creatureComp)
      continue;
    const life::LimbDNA &limbDna =
        creatureComp->record->dna.limbs[limbComp->limbIndex];

    vec3 parentPos(parentBody->pos[0], parentBody->pos[1], parentBody->pos[2]);
    vec4 parentRot(parentBody->rot[0], parentBody->rot[1], parentBody->rot[2],
                   parentBody->rot[3]);

    vec3 jointAxisWorld =
        normalize(rotateQuat(limbDna.jointAxisLocal, parentRot));
    vec4 hinge = quatFromAxisAngle(jointAxisWorld, limbComp->currentAngle);
    vec4 childRot = normalize(quatMultiply(hinge, parentRot));

    vec3 parentAnchorWorld =
        parentPos + rotateQuat(limbDna.parentAnchorLocal, parentRot);
    vec3 childAnchorOffset = rotateQuat(limbDna.childAnchorLocal, childRot);
    vec3 childPos = parentAnchorWorld - childAnchorOffset;

    vec3 oldPos(body.pos[0], body.pos[1], body.pos[2]);
    vec3 finiteVel =
        dt > 1e-5f ? (childPos - oldPos) / dt : vec3(0.0f, 0.0f, 0.0f);

    body.pos[0] = childPos.x;
    body.pos[1] = childPos.y;
    body.pos[2] = childPos.z;
    body.rot[0] = childRot.x;
    body.rot[1] = childRot.y;
    body.rot[2] = childRot.z;
    body.rot[3] = childRot.w;
    body.vel[0] = finiteVel.x;
    body.vel[1] = finiteVel.y;
    body.vel[2] = finiteVel.z;
    // Approximate: parent's own spin plus this joint's own hinge rate.
    body.ang_vel[0] =
        parentBody->ang_vel[0] + jointAxisWorld.x * limbComp->angularRate;
    body.ang_vel[1] =
        parentBody->ang_vel[1] + jointAxisWorld.y * limbComp->angularRate;
    body.ang_vel[2] =
        parentBody->ang_vel[2] + jointAxisWorld.z * limbComp->angularRate;

    body.GetOwner()->SetPosition(childPos);
    body.GetOwner()->SetRotation(childRot);
  }
}

// Step 1.4 (Predation) and Step 1.5 (Scavenging): a pairwise pass over
// every active body, reusing the same bounding-radius broad-phase test
// updatePhysicsCPU's body-to-body pass already uses. A creature's own
// limbs never interact with each other (same rootActor). Scavenging now
// operates at the whole-creature level: the first hit on a given corpse
// credits the eater the corpse's TOTAL volume (summed over all its
// limbs) and the whole creature — every one of its limb bodies, plus its
// CreatureRecord — is destroyed, not just the touched limb.
void resolvePredationAndScavenging(
    std::vector<Actor *> &bodyActors, std::vector<RigidBodyComponent *> &bodies,
    std::vector<life::CreatureRecord *> &population) {
  std::vector<life::CreatureRecord *> toDestroy;
  const int n = (int)bodies.size();

  for (int i = 0; i < n; i++) {
    auto *limbA = bodies[i]->GetOwner()->GetComponent<life::LimbComponent>();
    if (!limbA)
      continue;

    for (int j = i + 1; j < n; j++) {
      auto *limbB = bodies[j]->GetOwner()->GetComponent<life::LimbComponent>();
      if (!limbB || limbA->rootActor == limbB->rootActor)
        continue; // not a creature, or the same creature's own body

      RigidBodyComponent &bodyA = *bodies[i];
      RigidBodyComponent &bodyB = *bodies[j];
      vec3 posA(bodyA.pos[0], bodyA.pos[1], bodyA.pos[2]);
      vec3 posB(bodyB.pos[0], bodyB.pos[1], bodyB.pos[2]);
      vec3 extA(bodyA.extents[0], bodyA.extents[1], bodyA.extents[2]);
      vec3 extB(bodyB.extents[0], bodyB.extents[1], bodyB.extents[2]);
      if (length(posA - posB) > length(extA) + length(extB))
        continue; // not touching

      auto *creatureA =
          limbA->rootActor->GetComponent<life::CreatureComponent>();
      auto *creatureB =
          limbB->rootActor->GetComponent<life::CreatureComponent>();
      if (!creatureA || !creatureB)
        continue;
      life::CreatureRecord *recA = creatureA->record;
      life::CreatureRecord *recB = creatureB->record;

      bool aliveA = recA->IsAlive();
      bool aliveB = recB->IsAlive();

      if (aliveA && aliveB) {
        // --- Step 1.4: Predation. Thinner OBB pierces the thicker one. ---
        vec3 velA(bodyA.vel[0], bodyA.vel[1], bodyA.vel[2]);
        vec3 velB(bodyB.vel[0], bodyB.vel[1], bodyB.vel[2]);
        float vRel = length(velA - velB);
        float thickA = std::min({extA.x, extA.y, extA.z});
        float thickB = std::min({extB.x, extB.y, extB.z});

        life::CreatureRecord *piercer = nullptr;
        life::CreatureRecord *pierced = nullptr;
        RigidBodyComponent *piercerBody = nullptr;
        float tThin = 0.0f, tThick = 0.0f;
        if (thickA < thickB) {
          piercer = recA;
          pierced = recB;
          piercerBody = &bodyA;
          tThin = thickA;
          tThick = thickB;
        } else if (thickB < thickA) {
          piercer = recB;
          pierced = recA;
          piercerBody = &bodyB;
          tThin = thickB;
          tThick = thickA;
        }

        if (piercer) {
          float transfer = life::kPredationAlpha *
                           (0.5f * piercerBody->mass * vRel * vRel) *
                           (tThick - tThin);
          transfer = std::max(0.0f, transfer);
          piercer->energy += transfer;
          pierced->energy -= transfer;
        }
      } else if (aliveA != aliveB) {
        // --- Step 1.5: Scavenging. ---
        life::CreatureRecord *eater = aliveA ? recA : recB;
        life::CreatureRecord *corpse = aliveA ? recB : recA;
        if (std::find(toDestroy.begin(), toDestroy.end(), corpse) ==
            toDestroy.end()) {
          float totalVolume = 0.0f;
          for (const auto &limb : corpse->dna.limbs)
            totalVolume +=
                8.0f * limb.extents.x * limb.extents.y * limb.extents.z;
          eater->energy += totalVolume * life::kScavengeBeta;
          toDestroy.push_back(corpse);
        }
      }
    }
  }

  for (auto *record : toDestroy)
    destroyCreature(record, population, bodyActors, bodies);
}

// The continuous per-tick ecosystem update for every ACTIVE creature:
// metabolism, grazing, the root's sense->brain->act loop, every joint's
// own local sense->brain->PD-update loop, the shared reward/PPO buffers,
// and asexual reproduction (Steps 1.1-1.3, 2.2-2.3, 4.1-4.2, 5.1-5.4).
// Predation and scavenging (Step 1.4/1.5) are handled separately in
// resolvePredationAndScavenging(), which should run once earlier in the
// same frame so this step's delta-energy reward reflects them too.
//
// All of a creature's brains (root + every joint) share ONE reward
// signal per tick — whether the whole creature's energy pool went up or
// down — rather than each having its own private notion of progress.
// That's a deliberate choice: it frames the joints as a team of
// decentralized controllers being collectively rewarded for keeping the
// shared creature alive, which is what should encourage them to
// coordinate into something that looks like locomotion rather than each
// joint twitching to optimize something only it can see.
void stepEcosystem(Game *game, std::vector<Actor *> &bodyActors,
                   std::vector<RigidBodyComponent *> &bodies,
                   std::vector<life::CreatureRecord *> &population,
                   vec3 camPosWorld, float dt) {
  struct PendingBirth {
    vec3 posWorld;
    vec4 rot;
    life::CreatureDNA dna;
    life::BrainWeights rootBrain;
    std::vector<life::JointBrainWeights> jointBrains;
    std::vector<float> limbAngles;
    float startEnergy;
    int generation;
    uint32_t seed;
  };
  std::vector<PendingBirth> births;

  int activeCount = 0;
  for (auto *record : population)
    if (record->active)
      activeCount++;

  const size_t bodyCountAtStart = bodies.size();
  for (size_t idx = 0; idx < bodyCountAtStart; idx++) {
    RigidBodyComponent *rootBody = bodies[idx];
    auto *rootLimbComp =
        rootBody->GetOwner()->GetComponent<life::LimbComponent>();
    if (!rootLimbComp || rootLimbComp->limbIndex != 0)
      continue; // process each creature exactly once, starting at its root
    auto *creature =
        rootBody->GetOwner()->GetComponent<life::CreatureComponent>();
    if (!creature)
      continue;
    life::CreatureRecord *record = creature->record;
    if (!record->IsAlive())
      continue;

    float energyAtStepStart = record->energy;
    record->ageSeconds += dt;

    // --- Step 1.1: Basal Metabolic Rate ---
    record->energy -= rootBody->mass * life::kBasalMetabolicConst * dt;

    // --- Grounding + grazing sensors, and Step 1.3: Grazing ---
    vec3 pos(rootBody->pos[0], rootBody->pos[1], rootBody->pos[2]);
    vec3 groundNormal = calcPlanetNormal(pos, cpuTime);
    vec3 footPoint = pos - groundNormal * (rootBody->extents[1] + 0.05f);
    SDFResult footSDF = mapPlanet(footPoint, cpuTime);
    bool grounded = footSDF.dist < 0.6f;
    bool onGrass = grounded && dominantMaterialCPU(footSDF) == MAT_GRASS;
    creature->lastOnGrass = onGrass;
    if (onGrass)
      record->energy += life::kGrassEnergyRate * dt;

    // --- Step 2.2: root sensory state (orientation needed either way,
    // for reproduction placement below; the rest only matters for
    // mobile creatures) ---
    vec4 rot(rootBody->rot[0], rootBody->rot[1], rootBody->rot[2],
             rootBody->rot[3]);
    vec3 forward = rotateQuat(vec3(0.0f, 0.0f, 1.0f), rot);
    vec3 up = rotateQuat(vec3(0.0f, 1.0f, 0.0f), rot);
    vec3 right = rotateQuat(vec3(1.0f, 0.0f, 0.0f), rot);
    vec3 vel(rootBody->vel[0], rootBody->vel[1], rootBody->vel[2]);
    vec3 angVel(rootBody->ang_vel[0], rootBody->ang_vel[1],
                rootBody->ang_vel[2]);

    record->cpgPhase += dt;
    float cpgSignal =
        std::sin(6.28318530718f * life::kCPGFrequency * record->cpgPhase);

    float shapingReward = 0.0f;

    // Everything below — vision, the root brain, and every joint's own
    // brain — only runs for the "animal" lineage (dna.isMobile). Plants
    // never query a brain at all: they just sit, graze, age, and
    // reproduce through the GA layer alone. This is also what makes them
    // cheap to simulate in bulk and defenseless against predation — no
    // fleeing, no counter-maneuvering, just the passive thinness-based
    // spiking predation already gives any OBB "for free".
    if (record->dna.isMobile) {
      CreatureVision vision = senseVision(bodies, idx, forward);

      float state[life::kBrainInputs];
      state[life::kStateForwardSpeed] =
          std::clamp(dot(vel, forward) / life::kMaxForwardSpeed, -1.0f, 1.0f);
      state[life::kStateAngularSpeed] =
          std::clamp(length(angVel) / life::kMaxAngularSpeed, 0.0f, 1.0f);
      state[life::kStateContact] = grounded ? 1.0f : 0.0f;
      state[life::kStateCPG] = cpgSignal;
      state[life::kStateEnergy] =
          std::clamp(record->energy / life::kMaxEnergyForSensor, 0.0f, 1.0f);
      state[life::kStateVisionGrass] = onGrass ? 1.0f : 0.0f;
      state[life::kStateVisionLiving] = vision.sawLiving ? 1.0f : 0.0f;
      state[life::kStateVisionCorpse] = vision.sawCorpse ? 1.0f : 0.0f;
      state[life::kStateVisionRelVel] = vision.relVel;
      state[life::kStateVisionDist] = vision.dist;
      state[life::kStateBias] = 1.0f;

      // --- Phase 3: root locomotion brain ---
      float mean[life::kBrainOutputs];
      life::brainForward(record->rootBrain, state, mean);
      float action[life::kBrainOutputs];
      float logProb =
          life::sampleAction(record->rootBrain, mean, action, record->rngState);

      float thrust = std::clamp(action[0], -1.5f, 1.5f);
      float yawTorque = std::clamp(action[1], -1.5f, 1.5f);
      float pitchTorque = std::clamp(action[2], -1.5f, 1.5f);

      const float kMaxThrustAccel = 4.0f;
      vel = vel + forward * (thrust * kMaxThrustAccel * dt);
      angVel = angVel + up * (yawTorque * dt) + right * (pitchTorque * dt);
      rootBody->vel[0] = vel.x;
      rootBody->vel[1] = vel.y;
      rootBody->vel[2] = vel.z;
      rootBody->ang_vel[0] = angVel.x;
      rootBody->ang_vel[1] = angVel.y;
      rootBody->ang_vel[2] = angVel.z;

      float rootMoveCost =
          rootBody->mass * life::kMovementCostConst *
          (std::fabs(thrust) + std::fabs(yawTorque) + std::fabs(pitchTorque)) *
          dt;
      record->energy -= rootMoveCost;

      // Dense shaping reward (item 3): actually covering ground earns a
      // small bonus regardless of whether it paid off in energy yet.
      shapingReward = life::kMovementShapingWeight * length(vel) * dt;

      // --- Phase 2/3: every non-root joint's own decentralized brain ---
      struct JointStep {
        life::LimbComponent *comp;
        int limbIndex;
        float state[life::kJointBrainInputs];
        float action[life::kJointBrainOutputs];
        float logProb;
      };
      std::vector<JointStep> jointSteps;
      jointSteps.reserve(creature->limbActors.size());

      for (size_t i = 1; i < creature->limbActors.size(); i++) {
        Actor *limbActor = creature->limbActors[i];
        auto *lc = limbActor->GetComponent<life::LimbComponent>();
        auto *limbBody = limbActor->GetComponent<RigidBodyComponent>();
        if (!lc || !limbBody)
          continue;
        const life::LimbDNA &limbDna = record->dna.limbs[i];

        vec3 limbPos(limbBody->pos[0], limbBody->pos[1], limbBody->pos[2]);
        vec3 limbGroundNormal = calcPlanetNormal(limbPos, cpuTime);
        SDFResult limbSDF =
            mapPlanet(limbPos - limbGroundNormal * 0.05f, cpuTime);
        bool limbGrounded = limbSDF.dist < 0.5f;
        lc->lastContact = limbGrounded;

        float jState[life::kJointBrainInputs];
        jState[life::kJointStateAngle] =
            std::clamp(lc->currentAngle / std::max(limbDna.jointLimit, 0.01f),
                       -1.0f, 1.0f);
        jState[life::kJointStateRate] = std::clamp(
            lc->angularRate / life::kMaxJointAngularRate, -1.0f, 1.0f);
        jState[life::kJointStateContact] = limbGrounded ? 1.0f : 0.0f;
        jState[life::kJointStateCPG] = cpgSignal;
        jState[life::kJointStateEnergy] =
            std::clamp(record->energy / life::kMaxEnergyForSensor, 0.0f, 1.0f);
        jState[life::kJointStateBias] = 1.0f;

        float jMean[life::kJointBrainOutputs];
        life::jointBrainForward(record->jointBrains[i], jState, jMean);
        float jAction[life::kJointBrainOutputs];
        float jLogProb = life::sampleJointAction(record->jointBrains[i], jMean,
                                                 jAction, record->rngState);
        float targetAngle =
            std::clamp(jAction[0], -1.0f, 1.0f) * limbDna.jointLimit;

        // PD controller (Step 2.3): drive currentAngle toward targetAngle.
        float angAccel = life::kJointKp * (targetAngle - lc->currentAngle) -
                         life::kJointKd * lc->angularRate;
        lc->angularRate += angAccel * dt;
        lc->currentAngle = std::clamp(lc->currentAngle + lc->angularRate * dt,
                                      -limbDna.jointLimit, limbDna.jointLimit);

        // Step 1.2, per-joint: m_child * |commanded accel| * |rate| * dt.
        float jointMoveCost = limbBody->mass * life::kJointMoveCostConst *
                              std::fabs(angAccel) * std::fabs(lc->angularRate) *
                              dt;
        record->energy -= jointMoveCost;

        JointStep step;
        step.comp = lc;
        step.limbIndex = (int)i;
        for (int k = 0; k < life::kJointBrainInputs; k++)
          step.state[k] = jState[k];
        step.action[0] = jAction[0];
        step.logProb = jLogProb;
        jointSteps.push_back(step);
      }

      // --- Step 4.1: shared reward = this tick's total delta-energy,
      // plus the movement-shaping bonus (mobile creatures only) ---
      float reward = (record->energy - energyAtStepStart) + shapingReward;

      // --- Step 4.2: buffer + discounted-return PPO for the root ---
      life::BrainTransition rootT;
      for (int i = 0; i < life::kBrainInputs; i++)
        rootT.state[i] = state[i];
      for (int o = 0; o < life::kBrainOutputs; o++)
        rootT.action[o] = action[o];
      rootT.logProb = logProb;
      rootT.reward = reward;
      creature->rootBuffer.push_back(rootT);
      if ((int)creature->rootBuffer.size() >= life::kPPOBufferMaxSize) {
        life::computeDiscountedReturns(creature->rootBuffer,
                                       life::kDiscountGamma);
        life::ppoUpdate(record->rootBrain, creature->rootBuffer);
        creature->rootBuffer.clear();
      }

      // --- Step 4.2: buffer + discounted-return PPO for every joint ---
      for (auto &js : jointSteps) {
        life::JointBrainTransition jt;
        for (int k = 0; k < life::kJointBrainInputs; k++)
          jt.state[k] = js.state[k];
        jt.action[0] = js.action[0];
        jt.logProb = js.logProb;
        jt.reward = reward;
        js.comp->jointBuffer.push_back(jt);
        if ((int)js.comp->jointBuffer.size() >= life::kPPOBufferMaxSize) {
          life::computeDiscountedReturns(js.comp->jointBuffer,
                                         life::kDiscountGamma);
          life::jointPpoUpdate(record->jointBrains[js.limbIndex],
                               js.comp->jointBuffer);
          js.comp->jointBuffer.clear();
        }
      }
    }

    // --- Step 5.1: Death ---
    if (record->energy <= 0.0f) {
      record->energy = 0.0f;
      record->tag = life::CreatureTag::Corpse;
      rootBody->vel[0] = rootBody->vel[1] = rootBody->vel[2] = 0.0f;
      rootBody->ang_vel[0] = rootBody->ang_vel[1] = rootBody->ang_vel[2] = 0.0f;
      if (auto *col = rootBody->GetOwner()->GetComponent<OBBCollider>())
        col->SetLayer(ColliderLayer::Corpse);
      // Joint brains simply stop being queried next tick (IsAlive() is
      // now false), so no more active torque drives the pose — but
      // updateLimbKinematics keeps positioning limbs from the root's
      // (now passive, physically-ragdolling) transform, so the corpse
      // settles as one loose skeleton instead of exploding apart or
      // freezing mid-stride.
      continue;
    }

    // --- Steps 5.2/5.3/5.4: Reproduction ---
    if (record->energy >= life::kReproduceThreshold) {
      record->energy *= life::kReproduceOffspringShare;

      life::DNAMutationResult mutated =
          life::mutateCreatureDNA(record->dna, record->rngState);

      PendingBirth birth;
      vec3 offset = right * life::randfRange(record->rngState, -1.5f, 1.5f) +
                    forward * life::randfRange(record->rngState, -1.5f, 1.5f);
      birth.posWorld = (pos + offset + groundNormal * 0.5f) + camPosWorld;
      birth.rot = rot;
      birth.dna = mutated.dna;
      birth.generation = record->generation + 1;
      birth.seed = life::xorshift32(record->rngState);
      birth.startEnergy = record->energy; // parent's other (equal) half

      // Lamarckian inheritance (Step 5.3), remapped through the
      // mutation: limbs that still exist keep the parent's trained
      // joint brain; brand-new limbs (sourceLimbIndex == -1) get a
      // fresh random one.
      birth.rootBrain = record->rootBrain;
      birth.jointBrains.resize(mutated.dna.limbs.size());
      birth.limbAngles.assign(mutated.dna.limbs.size(), 0.0f);
      for (size_t i = 1; i < mutated.dna.limbs.size(); i++) {
        int src = mutated.sourceLimbIndex[i];
        if (src > 0 && src < (int)record->jointBrains.size()) {
          birth.jointBrains[i] = record->jointBrains[src];
          if (src < (int)record->limbAngles.size())
            birth.limbAngles[i] = record->limbAngles[src];
        } else {
          life::randomizeJointBrain(birth.jointBrains[i], record->rngState);
        }
      }
      births.push_back(birth);
    }
  }

  // Materialize births as new CreatureRecords, and activate them
  // immediately if there's room, so newborns are visible right away
  // instead of waiting for the next LOD pass to notice them.
  for (auto &birth : births) {
    auto *record = new life::CreatureRecord();
    record->dna = birth.dna;
    record->rootBrain = birth.rootBrain;
    record->jointBrains = birth.jointBrains;
    record->limbAngles = birth.limbAngles;
    record->energy = birth.startEnergy;
    record->tag = life::CreatureTag::Living;
    record->generation = birth.generation;
    record->rngState = birth.seed;
    record->pos = birth.posWorld;
    record->rot = birth.rot;
    record->cpgPhase = life::randfRange(record->rngState, 0.0f, 6.28318530718f);
    population.push_back(record);

    if (activeCount < life::kMaxActiveCreatures) {
      activateCreature(game, record, camPosWorld, bodyActors, bodies);
      activeCount++;
    }
  }
}

float getFrictionCPU(int matIndex) {
  if (matIndex == MAT_SAND)
    return 0.8f;
  if (matIndex == MAT_GRASS || matIndex == MAT_DIRT)
    return 0.6f;
  if (matIndex == MAT_SNOW)
    return 0.1f;
  if (matIndex == MAT_ICE)
    return 0.05f;
  return 0.7f;
}

float getRestitutionCPU(int matIndex) {
  if (matIndex == MAT_SNOW || matIndex == MAT_GRASS || matIndex == MAT_MAGMA ||
      matIndex == MAT_SAND)
    return 0.01f;
  else
    return 0.5f;
}

void applyImpulseCPU(RigidBodyComponent &body, vec3 contactPoint, vec3 normal,
                     float penetration, float friction,
                     float surfaceRestitution) {
  vec3 pos = vec3(body.pos[0], body.pos[1], body.pos[2]);
  vec3 vel = vec3(body.vel[0], body.vel[1], body.vel[2]);
  vec3 angVel = vec3(body.ang_vel[0], body.ang_vel[1], body.ang_vel[2]);
  vec3 r = contactPoint - pos;
  vec3 v = vel + cross(angVel, r);
  float vn = dot(v, normal);

  // Positional depenetration
  pos = pos + normal * (-penetration * 0.5f);
  body.pos[0] = pos.x;
  body.pos[1] = pos.y;
  body.pos[2] = pos.z;

  if (vn > 0.0f)
    return;

  float invMass = 1.0f / body.mass;
  if (invMass <= 0.000001f)
    return;

  // Compute proxy inverse moment of inertia (solid sphere approx)
  vec3 ext = vec3(body.extents[0], body.extents[1], body.extents[2]);
  float radius = (body.type < 0.5f) ? ext.x : length(ext);
  float invI = invMass * 2.5f / (radius * radius);

  float e = std::min(body.restitution, surfaceRestitution);

  // 1. Calculate true physical normal impulse (j)
  vec3 rn = cross(r, normal);
  float kNormal = invMass + dot(rn, rn) * invI;
  float j = -(1.0f + e) * vn / kNormal;

  vec3 impulse = normal * j;

  // 2. Tangential Friction
  vec3 tangent = v - normal * vn;
  float tLen = length(tangent);
  if (tLen > 0.0001f) {
    vec3 tDir = tangent / tLen;
    vec3 rt = cross(r, tDir);
    float kTangent = invMass + dot(rt, rt) * invI;
    float jt = -dot(v, tDir) / kTangent;

    float maxFriction = j * friction;
    jt = std::clamp(jt, -maxFriction, maxFriction);
    impulse = impulse + tDir * jt;
  }

  // 3. Apply changes generated by the final combined impulse vector
  vel = vel + impulse * invMass;
  angVel = angVel + cross(r, impulse) * invI;

  body.vel[0] = vel.x;
  body.vel[1] = vel.y;
  body.vel[2] = vel.z;
  body.ang_vel[0] = angVel.x;
  body.ang_vel[1] = angVel.y;
  body.ang_vel[2] = angVel.z;
}
void updatePhysicsCPU(std::vector<RigidBodyComponent *> &bodies, float dt,
                      vec3 camDelta, const PortalManager &portalManager) {
  const int n = (int)bodies.size();

  for (auto *bodyPtr : bodies) {
    auto &body = *bodyPtr;
    auto *limbComp = body.GetOwner()->GetComponent<life::LimbComponent>();
    if (limbComp && limbComp->limbIndex > 0)
      continue; // non-root limbs are positioned kinematically, see
                // updateLimbKinematics()

    vec3 pos = vec3(body.pos[0], body.pos[1], body.pos[2]) - camDelta;
    vec3 gravityDir = normalize(planetCenter - pos);

    vec3 vel =
        vec3(body.vel[0], body.vel[1], body.vel[2]) + gravityDir * (19.6f * dt);

    // ==========================================
    // Altitude-Based Atmospheric Drag Calculation
    // ==========================================
    float distFromCenter = length(pos - planetCenter);
    float surfaceRadius = 6000.0f;
    float height = distFromCenter - surfaceRadius;
    float atmosphereHeight = 1000.0f;

    float density = 0.0f;
    if (height < atmosphereHeight) {
      density = std::clamp(1.0f - (height / atmosphereHeight), 0.0f, 1.0f);
    }

    // 1. Apply Linear Drag
    float linDamping = 0.3f * density;
    if (linDamping > 0.0f) {
      vel = vel * std::max(1.0f - linDamping * dt, 0.0f);
    }

    pos = pos + vel * dt;
    body.pos[0] = pos.x;
    body.pos[1] = pos.y;
    body.pos[2] = pos.z;
    body.vel[0] = vel.x;
    body.vel[1] = vel.y;
    body.vel[2] = vel.z;

    // 2. Apply Angular Drag
    float angDamping = 0.5f * density;
    vec4 q = vec4(body.rot[0], body.rot[1], body.rot[2], body.rot[3]);
    vec4 w = vec4(body.ang_vel[0], body.ang_vel[1], body.ang_vel[2], 0.0f);

    if (angDamping > 0.0f) {
      w.x *= std::max(1.0f - angDamping * dt, 0.0f);
      w.y *= std::max(1.0f - angDamping * dt, 0.0f);
      w.z *= std::max(1.0f - angDamping * dt, 0.0f);
      body.ang_vel[0] = w.x;
      body.ang_vel[1] = w.y;
      body.ang_vel[2] = w.z;
    }

    vec4 dq = vec4(w.w * q.x + w.x * q.w + w.y * q.z - w.z * q.y,
                   w.w * q.y + w.y * q.w + w.z * q.x - w.x * q.z,
                   w.w * q.z + w.z * q.w + w.x * q.y - w.y * q.x,
                   w.w * q.w - w.x * q.x - w.y * q.y - w.z * q.z) *
              (0.5f * dt);
    vec4 newQ = normalize(q + dq);
    body.rot[0] = newQ.x;
    body.rot[1] = newQ.y;
    body.rot[2] = newQ.z;
    body.rot[3] = newQ.w;
  }

  // =========================================================================
  // Mass-Weighted Body-to-Body Collision Resolution
  // =========================================================================
  for (int i = 0; i < n; i++) {
    auto &bodyA = *bodies[i];
    float invMassA = 1.0f / bodyA.mass;
    auto *limbA = bodyA.GetOwner()->GetComponent<life::LimbComponent>();

    vec3 posA = vec3(bodyA.pos[0], bodyA.pos[1], bodyA.pos[2]);
    vec3 extA = vec3(bodyA.extents[0], bodyA.extents[1], bodyA.extents[2]);
    float radiusA = (bodyA.type < 0.5f) ? extA.x : length(extA);

    for (int j = i + 1; j < n; j++) {
      auto &bodyB = *bodies[j];
      auto *limbB = bodyB.GetOwner()->GetComponent<life::LimbComponent>();
      if (limbA && limbB && limbA->rootActor == limbB->rootActor)
        continue; // a creature's own limbs never collide with each other

      float invMassB = 1.0f / bodyB.mass;

      float totalInvMass = invMassA + invMassB;
      if (totalInvMass <= 1e-6f)
        continue;

      vec3 posB = vec3(bodyB.pos[0], bodyB.pos[1], bodyB.pos[2]);
      vec3 extB = vec3(bodyB.extents[0], bodyB.extents[1], bodyB.extents[2]);
      float radiusB = (bodyB.type < 0.5f) ? extB.x : length(extB);

      vec3 delta = posA - posB;
      float dist = length(delta);

      if (dist < (radiusA + radiusB) && dist > 0.0001f) {
        float overlap = (radiusA + radiusB) - dist;
        vec3 nrm = delta / dist;

        // 1. Mass-Weighted Positional Depenetration
        posA = posA + nrm * (overlap * (invMassA / totalInvMass));
        posB = posB - nrm * (overlap * (invMassB / totalInvMass));

        bodyA.pos[0] = posA.x;
        bodyA.pos[1] = posA.y;
        bodyA.pos[2] = posA.z;
        bodyB.pos[0] = posB.x;
        bodyB.pos[1] = posB.y;
        bodyB.pos[2] = posB.z;

        // 2. Mass-Weighted Impulse Resolution
        vec3 velA = vec3(bodyA.vel[0], bodyA.vel[1], bodyA.vel[2]);
        vec3 velB = vec3(bodyB.vel[0], bodyB.vel[1], bodyB.vel[2]);
        vec3 relVel = velA - velB;
        float vn = dot(relVel, nrm);

        if (vn < 0.0f) {
          float combinedRestitution =
              std::min(bodyA.restitution, bodyB.restitution);

          float jImp = -(1.0f + combinedRestitution) * vn / totalInvMass;

          velA = velA + nrm * (jImp * invMassA);
          velB = velB - nrm * (jImp * invMassB);

          bodyA.vel[0] = velA.x;
          bodyA.vel[1] = velA.y;
          bodyA.vel[2] = velA.z;
          bodyB.vel[0] = velB.x;
          bodyB.vel[1] = velB.y;
          bodyB.vel[2] = velB.z;
        }
      }
    }
  }

  for (auto *bodyPtr : bodies) {
    auto &body = *bodyPtr;
    auto *limbComp = body.GetOwner()->GetComponent<life::LimbComponent>();
    if (limbComp && limbComp->limbIndex > 0)
      continue; // non-root limbs: no independent terrain depenetration

    vec3 pos = vec3(body.pos[0], body.pos[1], body.pos[2]);
    vec3 ext = vec3(body.extents[0], body.extents[1], body.extents[2]);
    float boundingRadius = (body.type < 0.5f) ? ext.x : length(ext);

    SDFResult pd = mapPlanet(pos, cpuTime);
    if (pd.dist > boundingRadius)
      continue;

    if (body.type < 0.5f) {
      float pen = pd.dist - ext.x;
      int domMat = 0;
      float maxW = -1.0f;
      for (int i = 0; i < 16; i++) {
        if (pd.mat[i] > maxW) {
          if (i >= 12 && pd.mat[i] < 0.9f) {
            continue; // Skip liquid  if it isn't REALLY liquid
          }

          maxW = pd.mat[i];
          domMat = i;
        }
      }
      if (pen < 0.0f) {
        if (domMat >= 12) {
          float depth = -pen;
          vec3 waterNormal = calcWaterNormal(pos, cpuTime);
          vec3 vel = vec3(body.vel[0], body.vel[1], body.vel[2]);

          // Buoyant push strictly along the water normal
          vel = vel + waterNormal * (depth * 5.0f * dt);

          // Anisotropic drag relative to the water normal
          float vn = dot(vel, waterNormal);
          vec3 vNormal = waterNormal * vn;
          vec3 vTangent = vel - vNormal;

          vNormal = vNormal * std::max(0.0f, 1.0f - 3.0f * dt);
          vTangent = vTangent * std::max(0.0f, 1.0f - 1.0f * dt);
          vel = vNormal + vTangent;

          body.vel[0] = vel.x;
          body.vel[1] = vel.y;
          body.vel[2] = vel.z;

          vec3 angVel = vec3(body.ang_vel[0], body.ang_vel[1], body.ang_vel[2]);
          angVel = angVel * std::max(1.0f - 2.0f * dt, 0.0f);
          body.ang_vel[0] = angVel.x;
          body.ang_vel[1] = angVel.y;
          body.ang_vel[2] = angVel.z;
        } else {
          vec3 n = calcPlanetNormal(pos, cpuTime);
          vec3 cp = pos - n * ext.x;
          applyImpulseCPU(body, cp, n, pen, getFrictionCPU(domMat),
                          getRestitutionCPU(domMat));
        }
      }
    } else {
      vec4 rot = vec4(body.rot[0], body.rot[1], body.rot[2], body.rot[3]);
      vec3 corners[8] = {
          vec3(ext.x, ext.y, ext.z),   vec3(ext.x, ext.y, -ext.z),
          vec3(ext.x, -ext.y, ext.z),  vec3(ext.x, -ext.y, -ext.z),
          vec3(-ext.x, ext.y, ext.z),  vec3(-ext.x, ext.y, -ext.z),
          vec3(-ext.x, -ext.y, ext.z), vec3(-ext.x, -ext.y, -ext.z)};

      for (int i = 0; i < 8; i++) {
        vec3 worldCorner = vec3(body.pos[0], body.pos[1], body.pos[2]) +
                           rotateQuat(corners[i], rot);
        SDFResult pdCorn = mapPlanet(worldCorner, cpuTime);
        if (pdCorn.dist < 0.0f) {
          int domMat = 0;
          float maxW = -1.0f;
          for (int i = 0; i < 16; i++) {
            if (pd.mat[i] > maxW) {
              if (i >= 12 && pd.mat[i] <= 0.9f) {
                continue; // Skip liquid  if it isn't REALLY liquid
              }

              maxW = pd.mat[i];
              domMat = i;
            }
          }
          if (domMat >= 12) {
            float depth = -pdCorn.dist;
            vec3 waterNormal = calcWaterNormal(worldCorner, cpuTime);

            vec3 pos = vec3(body.pos[0], body.pos[1], body.pos[2]);
            vec3 vel = vec3(body.vel[0], body.vel[1], body.vel[2]);
            vec3 angVel =
                vec3(body.ang_vel[0], body.ang_vel[1], body.ang_vel[2]);

            vec3 r = worldCorner - pos;
            vec3 buoyantAcc = waterNormal * (depth * 15.0f * dt);

            vel = vel + buoyantAcc;
            vec3 torque = cross(r, buoyantAcc);
            angVel = angVel + torque * 0.5f;

            // Decompose corner velocity relative to water normal for
            // directional hydrodynamic drag
            float vn = dot(vel, waterNormal);
            vec3 vNormal = waterNormal * vn;
            vec3 vTangent = vel - vNormal;

            vNormal = vNormal * std::max(0.0f, 1.0f - 2.0f * dt);
            vTangent = vTangent * std::max(0.0f, 1.0f - 0.5f * dt);
            vel = vNormal + vTangent;

            angVel = angVel * std::max(1.0f - 0.5f * dt, 0.0f);

            body.vel[0] = vel.x;
            body.vel[1] = vel.y;
            body.vel[2] = vel.z;
            body.ang_vel[0] = angVel.x;
            body.ang_vel[1] = angVel.y;
            body.ang_vel[2] = angVel.z;
          } else {
            vec3 n = calcPlanetNormal(worldCorner, cpuTime);
            applyImpulseCPU(body, worldCorner, n, pdCorn.dist,
                            getFrictionCPU(domMat), getRestitutionCPU(domMat));
          }
        }
      }
    }
  }

  for (auto *bodyPtr : bodies) {
    auto &body = *bodyPtr;
    body.GetOwner()->SetPosition(vec3(body.pos[0], body.pos[1], body.pos[2]));
    body.GetOwner()->SetRotation(
        vec4(body.rot[0], body.rot[1], body.rot[2], body.rot[3]));
  }
}

void updateCullingCPU(const std::vector<RigidBodyComponent *> &bodies,
                      vec3 camForward, vec3 camRight, vec3 camUp, float aspect,
                      float maxDist, std::vector<uint32_t> &visibleOut) {
  visibleOut.clear();
  for (uint32_t idx = 0; idx < bodies.size(); idx++) {
    const auto &b = *bodies[idx];
    vec3 pos = vec3(b.pos[0], b.pos[1], b.pos[2]);
    vec3 ext = vec3(b.extents[0], b.extents[1], b.extents[2]);
    float radius = (b.type < 0.5f) ? ext.x : length(ext);
    float distZ = dot(pos, camForward);
    float distX = dot(pos, camRight);
    float distY = dot(pos, camUp);
    if (distZ < -radius || distZ - radius > maxDist)
      continue;
    if (std::fabs(distX) > (std::fabs(distZ) * aspect / 1.5f) + radius ||
        std::fabs(distY) > (std::fabs(distZ) * 1.0f / 1.5f) + radius)
      continue;
    visibleOut.push_back(idx);
  }
}

void uploadBodies(GLuint ssbo,
                  const std::vector<RigidBodyComponent *> &bodies) {
  static size_t lastCount = (size_t)-1;
  static std::vector<RigidBodyGPU> packedBodies;
  packedBodies.clear();
  packedBodies.reserve(bodies.size());
  for (const auto *body : bodies) {
    RigidBodyGPU gpu{};
    gpu.pos[0] = body->pos[0];
    gpu.pos[1] = body->pos[1];
    gpu.pos[2] = body->pos[2];
    gpu.type = body->type;
    gpu.extents[0] = body->extents[0];
    gpu.extents[1] = body->extents[1];
    gpu.extents[2] = body->extents[2];
    gpu.mass = body->mMass;
    gpu.vel[0] = body->vel[0];
    gpu.vel[1] = body->vel[1];
    gpu.vel[2] = body->vel[2];
    gpu.restitution = body->restitution;
    gpu.rot[0] = body->rot[0];
    gpu.rot[1] = body->rot[1];
    gpu.rot[2] = body->rot[2];
    gpu.rot[3] = body->rot[3];
    gpu.ang_vel[0] = body->ang_vel[0];
    gpu.ang_vel[1] = body->ang_vel[1];
    gpu.ang_vel[2] = body->ang_vel[2];
    gpu.pad = body->pad;
    packedBodies.push_back(gpu);
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
  size_t bytes = packedBodies.size() * sizeof(RigidBodyGPU);
  if (bodies.size() != lastCount) {
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes,
                 packedBodies.empty() ? nullptr : packedBodies.data(),
                 GL_DYNAMIC_DRAW);
    lastCount = bodies.size();
  } else if (!packedBodies.empty()) {
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, packedBodies.data());
  }
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
}

void uploadVisible(GLuint visibleSSBO, const std::vector<uint32_t> &visible) {
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibleSSBO);
  uint32_t count = (uint32_t)visible.size();
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &count);
  if (count > 0)
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t),
                    count * sizeof(uint32_t), visible.data());
}

Game::Game()
    : mRenderer(nullptr), mCamera(nullptr), mCurrentScene(nullptr),
      mPendingScene(nullptr), mTicksCount(0), mIsRunning(true),
      mIsDebugging(false), mIsPaused(false), mUpdatingActors(false) {}

bool Game::Initialize() { return true; }

void Game::LoadScene(Scene *scene) { mPendingScene = scene; }

void Game::AddActor(Actor *actor) {
  if (mUpdatingActors) {
    mPendingActors.emplace_back(actor);
  } else {
    mActors.emplace_back(actor);
  }
}

void Game::RemoveActor(Actor *actor) {
  auto removeFrom = [actor](std::vector<Actor *> &list) {
    auto iter = std::find(list.begin(), list.end(), actor);
    if (iter != list.end()) {
      list.erase(iter);
    }
  };

  removeFrom(mActors);
  removeFrom(mPendingActors);
  mAlwaysActiveActors.erase(actor);
  removeFrom(mActiveActors);
}

void Game::AddAlwaysActive(Actor *actor) { mAlwaysActiveActors.insert(actor); }

void Game::RemoveAlwaysActive(Actor *actor) {
  mAlwaysActiveActors.erase(actor);
}

void Game::FindActiveActors() {
  mActiveActors.clear();
  for (auto actor : mActors) {
    if (actor && actor->GetState() == ActorState::Active) {
      mActiveActors.push_back(actor);
    }
  }
}

void Game::UpdateActors(float deltaTime) {
  mUpdatingActors = true;
  for (auto actor : mActiveActors) {
    if (actor) {
      actor->Update(deltaTime);
    }
  }
  mUpdatingActors = false;

  for (auto actor : mPendingActors) {
    mActors.push_back(actor);
  }
  mPendingActors.clear();
}

void Game::ProcessInput() {}
void Game::UpdateGame(float deltaTime) { UpdateActors(deltaTime); }
void Game::GenerateOutput() {}
void Game::CheckCollisions() {}

void Game::Shutdown() {
  auto actors = mActors;
  mActors.clear();
  mPendingActors.clear();
  mActiveActors.clear();
  mAlwaysActiveActors.clear();
  for (auto actor : actors) {
    delete actor;
  }
}

void Game::RunLoop() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window *window = SDL_CreateWindow("Floating Origin Space Engine", 1280,
                                        720, SDL_WINDOW_OPENGL);
  SDL_GLContext glContext = SDL_GL_CreateContext(window);

  std::vector<std::string> materialTexturePaths = {
      getAssetPath("textures/stone.png"),
      getAssetPath("textures/sand.png"),
      getAssetPath("textures/grass.png"),
      getAssetPath("textures/dirt.png"),
      getAssetPath("textures/snow.png"),
      getAssetPath("textures/sandstone.png"),
      getAssetPath("textures/ice.png"),
      getAssetPath("textures/glowing_crystal.png"),
      getAssetPath("textures/magma.png"),
      getAssetPath("textures/moonrock.png"),
      getAssetPath("textures/sunmagma.png"),
      getAssetPath("textures/mineral.png"),
      getAssetPath("textures/water.png"),
      getAssetPath("textures/lava.png"),
      getAssetPath("textures/mud.png"),
      getAssetPath("textures/acid.png")};

  GLuint materialTextureArray = loadMaterialTextureArray(materialTexturePaths);
  if (!materialTextureArray)
    return;

  const char *vertCode = R"(
        #version 430 core
        out vec2 TexCoords;
        void main() {
            vec2 vertices[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
            gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
            TexCoords = (vertices[gl_VertexID] + 1.0) * 0.5;
        }
    )";

  // --- Shader Compilation ---
  GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertCode);
  GLuint fragShader =
      compileShader(GL_FRAGMENT_SHADER,
                    loadShaderSource(getAssetPath("shaders/render.frag")));
  GLuint renderProgram = glCreateProgram();
  glAttachShader(renderProgram, vertShader);
  glAttachShader(renderProgram, fragShader);
  glLinkProgram(renderProgram);

  GLuint objVertShader = compileShader(
      GL_VERTEX_SHADER, loadShaderSource(getAssetPath("shaders/object.vert")));
  GLuint objFragShader =
      compileShader(GL_FRAGMENT_SHADER,
                    loadShaderSource(getAssetPath("shaders/object.frag")));
  GLuint objectProgram = glCreateProgram();
  glAttachShader(objectProgram, objVertShader);
  glAttachShader(objectProgram, objFragShader);
  glLinkProgram(objectProgram);

  auto checkGLError = [](const char *location) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
      std::cerr << "OpenGL Error at " << location << ": 0x" << std::hex << err
                << std::dec << std::endl;
    }
  };

  // --- Bake Logic ---
  GLuint bakeShader =
      compileShader(GL_COMPUTE_SHADER,
                    loadShaderSource(getAssetPath("shaders/bake_macro.comp")));
  GLuint bakeProgram = glCreateProgram();
  glAttachShader(bakeProgram, bakeShader);
  glLinkProgram(bakeProgram);

  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  const int CUBE_RES = 2048;
  GLuint macroCubeMap;
  glGenTextures(1, &macroCubeMap);
  glBindTexture(GL_TEXTURE_CUBE_MAP, macroCubeMap);

  int mipLevels = 1 + (int)std::floor(std::log2(CUBE_RES));

  glTexStorage2D(GL_TEXTURE_CUBE_MAP, mipLevels, GL_RGBA16F, CUBE_RES,
                 CUBE_RES);
  checkGLError("glTexStorage2D");

  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  glUseProgram(bakeProgram);

  glBindImageTexture(0, macroCubeMap, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

  glDispatchCompute(CUBE_RES / 16, CUBE_RES / 16, 6);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_TEXTURE_FETCH_BARRIER_BIT);

  glBindTexture(GL_TEXTURE_CUBE_MAP, macroCubeMap);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  checkGLError("glGenerateMipmap");

  // --- DOWNLOAD MACRO MAP FOR CPU PHYSICS ---
  cpuCubeRes = CUBE_RES;
  cpuMacroData.resize(CUBE_RES * CUBE_RES * 6 * 4);
  for (int i = 0; i < 6; i++) {
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, GL_FLOAT,
                  cpuMacroData.data() + i * CUBE_RES * CUBE_RES * 4);
  }
  checkGLError("glGetTexImage CPU Sync");

  GLuint bakeGyroidShader =
      compileShader(GL_COMPUTE_SHADER,
                    loadShaderSource(getAssetPath("shaders/bake_gyroid.comp")));
  GLuint bakeGyroidProgram = glCreateProgram();
  glAttachShader(bakeGyroidProgram, bakeGyroidShader);
  glLinkProgram(bakeGyroidProgram);
  checkGLError("glLinkProgram");
  GLuint gyroidTex;
  glGenTextures(1, &gyroidTex);
  glBindTexture(GL_TEXTURE_3D, gyroidTex);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, 64, 64, 64, 0, GL_RED, GL_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  glUseProgram(bakeGyroidProgram);
  glBindImageTexture(1, gyroidTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
  glDispatchCompute(64 / 8, 64 / 8, 64 / 8);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  glBindTexture(GL_TEXTURE_3D, gyroidTex);
  glGenerateMipmap(GL_TEXTURE_3D);

  // --- Rasterization Meshes VAO/VBO Setup ---
  std::vector<float> vboData;
  int cubeOffset, cubeCount, sphereOffset, sphereCount;
  generateMeshes(vboData, cubeOffset, cubeCount, sphereOffset, sphereCount);
  GLuint objVAO, objVBO;
  glGenVertexArrays(1, &objVAO);
  glGenBuffers(1, &objVBO);
  glBindVertexArray(objVAO);
  glBindBuffer(GL_ARRAY_BUFFER, objVBO);
  glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float), vboData.data(),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Render Uniforms
  GLint uRenderResolution = glGetUniformLocation(renderProgram, "resolution");
  GLint uRenderPlanetCenter =
      glGetUniformLocation(renderProgram, "planetCenter");
  GLint uRenderCamForward = glGetUniformLocation(renderProgram, "camForward");
  GLint uRenderCamRight = glGetUniformLocation(renderProgram, "camRight");
  GLint uRenderCamUp = glGetUniformLocation(renderProgram, "camUp");

  // Assuming you have access to your PortalManager instance and shader program
  // ID:
  GLint uCamPortalLoc =
      glGetUniformLocation(renderProgram, "uCameraInsidePortalIndex");
  GLint uRenderTime = glGetUniformLocation(renderProgram, "time");
  GLint uRenderVirtualRes = glGetUniformLocation(renderProgram, "virtualRes");
  GLint uRenderMaxIter = glGetUniformLocation(renderProgram, "maxIterations");
  GLint uMacroMapLoc = glGetUniformLocation(renderProgram, "macroElevationMap");
  GLint uGyroidTexLoc = glGetUniformLocation(renderProgram, "gyroidTex");
  GLint uMaterialTexturesLoc =
      glGetUniformLocation(renderProgram, "materialTextures");
  GLint uRenderView = glGetUniformLocation(renderProgram, "view");
  GLint uRenderProj = glGetUniformLocation(renderProgram, "proj");

  std::vector<Actor *> bodyActors;
  std::vector<RigidBodyComponent *> bodies;
  // for (int i = 0; i < 10; i++)
  // spawnBody(this, bodyActors, bodies);

  // --- Life simulation: scatter the planet-wide population (dormant).
  // Only kMaxActiveCreatures of these are ever actually simulated at
  // once — see updateActivation() in the main loop below. ---
  uint32_t ecosystemRng = 0x5eed1234u;
  std::vector<life::CreatureRecord *> creaturePopulation;
  seedPlanetPopulation(creaturePopulation, /*count=*/25000, ecosystemRng);
  GLuint ssbo, vao, visibleSSBO;
  glGenBuffers(1, &ssbo);
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &visibleSSBO);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibleSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               sizeof(uint32_t) + 65536 * sizeof(uint32_t), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, visibleSSBO);

  std::vector<Actor *> visualActors;
  std::vector<MeshComponent *> visualInstances;

  // --- Load External Models ---
  ModelData towerModel = loadOBJ(getAssetPath("meshes/tower.obj"));

  // Example 1: Instantiate an always-loaded towers, one in each pole
  auto *towerActor = new Actor(this);
  auto *tower = new MeshComponent(towerActor, &towerModel, true);
  tower->pos[0] = 225.0f;
  tower->pos[1] = 5800.0f;
  tower->pos[2] = 0.0f; // Placed on surface
  tower->rot[0] = 0.0f;
  tower->rot[1] = 0.0f;
  tower->rot[2] = 0.0f;
  tower->rot[3] = 1.0f; // Identity Quat
  tower->scale[0] = 4.0f;
  tower->scale[1] = 4.0f;
  tower->scale[2] = 4.0f;
  tower->color[0] = 0.1f;
  tower->color[1] = 0.1f;
  tower->color[2] = 0.1f;
  tower->alwaysLoaded = true;
  towerActor->SetPosition(vec3(tower->pos[0], tower->pos[1], tower->pos[2]));
  towerActor->SetRotation(
      vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]));
  towerActor->SetScale(vec3(tower->scale[0], tower->scale[1], tower->scale[2]));
  AddAlwaysActive(towerActor);
  visualActors.push_back(towerActor);
  visualInstances.push_back(tower);

  auto *towerActor2 = new Actor(this);
  auto *tower2 = new MeshComponent(towerActor2, &towerModel, true);
  tower2->pos[0] = -225.0f;
  tower2->pos[1] = -5800.0f;
  tower2->pos[2] = 0.0f; // Placed on surface

  // Rotated 180 degrees in relation to tower 1
  tower2->rot[0] = 0.0f;
  tower2->rot[1] = 0.0f;
  tower2->rot[2] = 1.0f;
  tower2->rot[3] = 0.0f;

  tower2->scale[0] = 4.0f;
  tower2->scale[1] = 4.0f;
  tower2->scale[2] = 4.0f;
  tower2->color[0] = 0.1f;
  tower2->color[1] = 0.1f;
  tower2->color[2] = 0.1f;
  tower2->alwaysLoaded = true;
  towerActor2->SetPosition(
      vec3(tower2->pos[0], tower2->pos[1], tower2->pos[2]));
  towerActor2->SetRotation(
      vec4(tower2->rot[0], tower2->rot[1], tower2->rot[2], tower2->rot[3]));
  towerActor2->SetScale(
      vec3(tower2->scale[0], tower2->scale[1], tower2->scale[2]));
  AddAlwaysActive(towerActor2);
  visualActors.push_back(towerActor2);
  visualInstances.push_back(tower2);

  // large always active grid object
  ModelData gridModel = loadOBJ(getAssetPath("meshes/grade.obj"));
  auto *gridActor = new Actor(this);
  auto *grid = new MeshComponent(gridActor, &gridModel, true);
  grid->pos[0] = 0.0f;
  grid->pos[1] = 0.0f;
  grid->pos[2] = 0.0f;
  grid->rot[0] = 0.0f;
  grid->rot[1] = 0.0f;
  grid->rot[2] = 0.0f;
  grid->rot[3] = 0.0f;
  grid->scale[0] = 6550.0f;
  grid->scale[1] = 6550.0f;
  grid->scale[2] = 6550.0f;
  grid->color[0] = 0.1f;
  grid->color[1] = 0.1f;
  grid->color[2] = 0.1f;
  grid->alwaysLoaded = true;
  gridActor->SetPosition(vec3(grid->pos[0], grid->pos[1], grid->pos[2]));
  gridActor->SetRotation(
      vec4(grid->rot[0], grid->rot[1], grid->rot[2], grid->rot[3]));
  gridActor->SetScale(vec3(grid->scale[0], grid->scale[1], grid->scale[2]));
  AddAlwaysActive(gridActor);
  visualActors.push_back(gridActor);
  visualInstances.push_back(grid);

  // Example 2: Instantiate a scatter object that unloads when far away
  // VisualInstance smallRock;
  // smallRock.model = &cidagonoModel;
  // smallRock.pos[0] = 50.0f;
  // smallRock.pos[1] = 0.0f;
  // smallRock.pos[2] = 6010.0f;
  // smallRock.rot[0] = 0.0f;
  // smallRock.rot[1] = 0.0f;
  // smallRock.rot[2] = 0.0f;
  // smallRock.rot[3] = 1.0f;
  // smallRock.scale[0] = 1.0f;
  // smallRock.scale[1] = 1.0f;
  // smallRock.scale[2] = 1.0f;
  // smallRock.color[0] = 0.5f;
  // smallRock.color[1] = 0.5f;
  // smallRock.color[2] = 0.5f;
  // smallRock.alwaysLoaded = false;
  // smallRock.maxLoadDist = 500.0f; // Will "unload" if distance > 500
  // visualInstances.push_back(smallRock);

  double camPos[3] = {0.0, 6500.0, 0.0};
  vec3 camForward = vec3(0, -1, -1);
  vec3 camUp = vec3(0, 1, 0);
  vec3 camRight = vec3(1, 0, 0);
  CameraMode currentMode = PLANET_ALIGNED;
  float yaw = -1.5708f, pitch = 0.0f;

  for (auto *inst : visualInstances) {
    if (mAlwaysActiveActors.find(inst->GetOwner()) !=
        mAlwaysActiveActors.end()) {
      continue;
    }

    inst->pos[0] -= camPos[0];
    inst->pos[1] -= camPos[1];
    inst->pos[2] -= camPos[2];
  }

  float avgFrameTime = 1.0f / 60.0f;
  float qualityScale = 1.0f;
  std::vector<uint32_t> visibleIndices;
  SDL_GL_SetSwapInterval(0);
  Uint64 lastTime = SDL_GetTicks();
  bool running = true;
  SDL_Event event;
  bool fullscreen = false;

  glEnable(GL_DEPTH_TEST);

  // Added virtualVolumetric target
  GLuint virtualFBO = 0, virtualColor = 0, virtualVolumetric = 0,
         virtualPortalMeta = 0, virtualDepth = 0;
  glGenFramebuffers(1, &virtualFBO);
  glGenTextures(1, &virtualColor);
  glGenTextures(1, &virtualVolumetric);
  glGenTextures(1, &virtualPortalMeta);
  glGenTextures(1, &virtualDepth);
  int currentVirtW = 0, currentVirtH = 0;

  glEnable(GL_DEPTH_TEST);

  // Portals now live in PortalManager instead of hardcoded globals, so
  // adding more pairs later is just more addPortalPair() calls.
  //
  // Testing scaffold: 10 pairs, each still connecting roughly north pole
  // to south pole (same idea as the original single tower/tower2 pair),
  // spaced out sideways so all 10 are visible and walkable near each
  // other instead of scattered across the planet. NOTE: this offsets
  // them along X in a straight line rather than snapping each to the
  // actual terrain surface at that offset (would need a mapPlanet/normal
  // query per portal) — fine for testing bend/warp plumbing, but expect
  // some of them to float above or sink into the ground until that's
  // added.
  PortalManager portalManager;
  portalManager.initGL();
  // constexpr int kTestPortalPairs = 10;
  // constexpr float kTestPortalSpacing = 30.0f;
  // for (int i = 0; i < kTestPortalPairs; i++) {
  //   float offset = i * kTestPortalSpacing;
  //   portalManager.addPortalPair(
  //       vec3(200.0f + offset, 6050.0f, 0.0f),
  //       vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]),
  //       vec3(200.0f + offset, -6050.0f, 0.0f),
  //       vec4(tower2->rot[0], tower2->rot[1], tower2->rot[2], tower2->rot[3]),
  //       /*radius=*/1.0f);
  // }

  // manually adding some portal pairs for testing

  // Portal from north pole to south pole
  portalManager.addPortalPair(
      vec3(210.0f, 5925.0f, 0.0f),
      vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]),
      vec3(-225.0f, -6066.0f, 0.0f),
      vec4(tower2->rot[0], tower2->rot[1], tower2->rot[2], tower2->rot[3]),
      /*radius=*/5.0f);

  // Portal from north pole to space
  portalManager.addPortalPair(
      vec3(205.0f, 5916.0f, 0.0f),
      vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]),
      vec3(0.0f, 30000.0f, 0.0f), vec4(0.0f, 0.7071f, -0.7071f, 0.0f),
      /*radius=*/2.0f);

  // Portal from north pole to sea level, rotated sideways
  portalManager.addPortalPair(
      vec3(200.0f, 5916.5f, 0.0f),
      vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]),
      vec3(5995.0f, 200.0f, 0.0f),
      // aligned to the ground, upside down
      vec4(0.7071f, 0.7071f, 0.0f, 0.0f),
      /*radius=*/2.0f);

  // Portal from north pole to grass field. rotated sideways inverted
  portalManager.addPortalPair(
      vec3(195.0f, 5917.5f, 0.0f),
      vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]),
      vec3(-6026.5f, -200.0f, 0.0f),
      // aligned to the ground, upside down
      vec4(0.7071f, -0.7071f, 0.0f, 0.0f),
      /*radius=*/2.0f);

  /// Portal from north pole to desert
  portalManager.addPortalPair(
      vec3(190.0f, 5918.5f, 0.0f),
      vec4(tower->rot[0], tower->rot[1], tower->rot[2], tower->rot[3]),
      vec3(5858.0f, -1642.0f, -319.0f),
      // aligned to the ground, upside down
      vec4(-0.7071f, -0.7071f, 0.0f, 0.0f),
      /*radius=*/2.0f);

  while (running) {
    Uint64 currentTime = SDL_GetTicks();
    float dt = (currentTime - lastTime) / 1000.0f;
    lastTime = currentTime;
    if (dt > 0.05f)
      dt = 0.05f;

    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
      else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_SPACE)
          spawnBody(this, bodyActors, bodies);
        else if (event.key.key == SDLK_BACKSPACE && !bodies.empty())
          removeBody(bodyActors, bodies, bodies.size() - 1);
        else if (event.key.key == SDLK_L) {
          // Drop one more generation-0 creature right next to the
          // camera and activate it immediately.
          vec3 camPosWorldNow((float)camPos[0], (float)camPos[1],
                              (float)camPos[2]);
          vec3 dir = normalize(camPosWorldNow - planetCenter);
          vec3 spawnPos = camPosWorldNow - dir * 5.0f;
          for (int iter = 0; iter < 10; iter++) {
            SDFResult pd = mapPlanet(spawnPos, cpuTime);
            vec3 n = calcPlanetNormal(spawnPos, cpuTime);
            spawnPos = spawnPos + n * (pd.dist - 1.0f);
          }
          auto *record = new life::CreatureRecord();
          record->dna = life::makeDefaultCreatureDNA(ecosystemRng);
          life::randomizeBrain(record->rootBrain, ecosystemRng);
          record->jointBrains.resize(record->dna.limbs.size());
          record->limbAngles.assign(record->dna.limbs.size(), 0.0f);
          for (size_t j = 1; j < record->dna.limbs.size(); j++)
            life::randomizeJointBrain(record->jointBrains[j], ecosystemRng);
          record->energy = life::kStartEnergy;
          record->rngState = life::xorshift32(ecosystemRng);
          record->pos = spawnPos;
          record->rot = vec4(0.0f, 0.0f, 0.0f, 1.0f);
          creaturePopulation.push_back(record);
          activateCreature(this, record, camPosWorldNow, bodyActors, bodies);
        } else if (event.key.key == SDLK_C) {
          currentMode = (CameraMode)((currentMode + 1) % 3);
          if (currentMode == FREE_CAM) {
            pitch = std::asin(std::clamp(camForward.y, -1.0f, 1.0f));
            yaw = std::atan2(camForward.z, camForward.x);
          }
        } else if (event.key.key == SDLK_F11) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(window, fullscreen);
        }
      }
    }

    const bool *keys = SDL_GetKeyboardState(NULL);
    float moveSpeed = (keys[SDL_SCANCODE_LSHIFT] ? 1600.0f : 10.0f) * dt;
    float lookSpeed = 1.8f * dt;

    if (currentMode == FREE_CAM) {
      if (keys[SDL_SCANCODE_UP])
        pitch += lookSpeed;
      if (keys[SDL_SCANCODE_DOWN])
        pitch -= lookSpeed;
      if (keys[SDL_SCANCODE_LEFT])
        yaw -= lookSpeed;
      if (keys[SDL_SCANCODE_RIGHT])
        yaw += lookSpeed;
      if (pitch > 1.5f)
        pitch = 1.5f;
      if (pitch < -1.5f)
        pitch = -1.5f;
      camForward =
          vec3(cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch));
      camRight = vec3(-sin(yaw), 0.0f, cos(yaw));
      camUp = cross(camRight, camForward);
    } else {
      vec3 pVec = vec3((float)camPos[0], (float)camPos[1], (float)camPos[2]);
      vec3 targetUp = (currentMode == PLANET_ALIGNED)
                          ? normalize(pVec)
                          : calcPlanetNormal(pVec, cpuTime);
      camUp = normalize(camUp + (targetUp - camUp) * (dt * 5.0f));
      vec3 refRight = normalize(cross(camForward, camUp));
      vec3 refForward = normalize(cross(camUp, refRight));
      float dYaw = 0.0f, dPitch = 0.0f;
      if (keys[SDL_SCANCODE_UP])
        dPitch += lookSpeed;
      if (keys[SDL_SCANCODE_DOWN])
        dPitch -= lookSpeed;
      if (keys[SDL_SCANCODE_LEFT])
        dYaw += lookSpeed;
      if (keys[SDL_SCANCODE_RIGHT])
        dYaw -= lookSpeed;
      if (dYaw != 0.0f) {
        refForward = rotateAxis(refForward, camUp, dYaw);
        refRight = normalize(cross(refForward, camUp));
      }
      float currentPitch =
          std::asin(std::clamp(dot(camForward, camUp), -1.0f, 1.0f));
      float newPitch = std::clamp(currentPitch + dPitch, -1.5f, 1.5f);
      camForward = rotateAxis(refForward, refRight, newPitch);
      camUp = normalize(cross(refRight, camForward));
      camRight = refRight;
    }

    double deltaX = 0, deltaY = 0, deltaZ = 0;
    if (keys[SDL_SCANCODE_W]) {
      deltaX += camForward.x * moveSpeed;
      deltaY += camForward.y * moveSpeed;
      deltaZ += camForward.z * moveSpeed;
    }
    if (keys[SDL_SCANCODE_S]) {
      deltaX -= camForward.x * moveSpeed;
      deltaY -= camForward.y * moveSpeed;
      deltaZ -= camForward.z * moveSpeed;
    }
    if (keys[SDL_SCANCODE_A]) {
      deltaX -= camRight.x * moveSpeed;
      deltaY -= camRight.y * moveSpeed;
      deltaZ -= camRight.z * moveSpeed;
    }
    if (keys[SDL_SCANCODE_D]) {
      deltaX += camRight.x * moveSpeed;
      deltaY += camRight.y * moveSpeed;
      deltaZ += camRight.z * moveSpeed;
    }
    double deltaLen =
        std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    if (deltaLen > 50.0) {
      double scale = 50.0 / deltaLen;
      deltaX *= scale;
      deltaY *= scale;
      deltaZ *= scale;
    }
    camPos[0] += deltaX;
    camPos[1] += deltaY;
    camPos[2] += deltaZ;

    // Checks every active, linked portal for camera proximity and
    // teleports through the first one it's inside of. Replaces the old
    // two-branch if/else, and scales to as many pairs as we register.
    double oldCamPos[3] = {camPos[0], camPos[1], camPos[2]};

    // 2. Check and execute teleport (updates camPos if crossed)
    portalManager.checkTeleport(camPos, camForward, camRight, camUp);

    // 3. Calculate the teleport delta vector
    vec3 teleportDelta(camPos[0] - oldCamPos[0], camPos[1] - oldCamPos[1],
                       camPos[2] - oldCamPos[2]);

    // 4. If a teleport actually happened, shift all objects and bodies
    // by the inverse of the delta so they stay locked in their world positions.
    if (dot(teleportDelta, teleportDelta) > 0.0001f) {
      // Shift visual instances
      for (auto *inst : visualInstances) {
        inst->pos[0] -= teleportDelta.x;
        inst->pos[1] -= teleportDelta.y;
        inst->pos[2] -= teleportDelta.z;
      }

      // Shift physics bodies
      for (auto *b : bodies) {
        b->pos[0] -= teleportDelta.x;
        b->pos[1] -= teleportDelta.y;
        b->pos[2] -= teleportDelta.z;
      }
    }

    // Recomputes camera-relative positions + this->linked rotations for
    // every portal and re-uploads the SSBO the shaders read from.
    portalManager.update(camPos);
    // Binding point 1 must match `layout(std430, binding = 1)` in
    // portal.glsl. SSBO bindings are global GL state, not per-program, so
    // one bind() covers the raymarch pass, the object pass, and the
    // upcoming phantom object pass without rebinding per program.
    portalManager.bind(1);

    vec3 camDeltaVec((float)deltaX, (float)deltaY, (float)deltaZ);

    double worldPlanetCenter[3] = {0.0, 0.0, 0.0};
    float planetCenterArr[3] = {(float)(worldPlanetCenter[0] - camPos[0]),
                                (float)(worldPlanetCenter[1] - camPos[1]),
                                (float)(worldPlanetCenter[2] - camPos[2])};
    planetCenter =
        vec3(planetCenterArr[0], planetCenterArr[1], planetCenterArr[2]);

    // Keep visual instances grounded in the floating origin system
    for (auto *inst : visualInstances) {
      inst->pos[0] -= camDeltaVec.x;
      inst->pos[1] -= camDeltaVec.y;
      inst->pos[2] -= camDeltaVec.z;
    }

    for (int i = 0; i < 3; i++) {
      // 1. Evaluate the EXACT high-resolution SDF
      SDFResult pd = mapPlanet(vec3(0.0f, 0.0f, 0.0f), cpuTime);

      if (pd.dist < 1.5f) {
        // 2. Calculate the EXACT normal using the high-res mapPlanet geometry
        // (calcPlanetNormal usually defaults to the fast pass, so we inline it
        // here)
        float e = 0.01f;
        vec3 normal =
            normalize(vec3(mapPlanet(vec3(e, 0.0f, 0.0f), cpuTime).dist -
                               mapPlanet(vec3(-e, 0.0f, 0.0f), cpuTime).dist,
                           mapPlanet(vec3(0.0f, e, 0.0f), cpuTime).dist -
                               mapPlanet(vec3(0.0f, -e, 0.0f), cpuTime).dist,
                           mapPlanet(vec3(0.0f, 0.0f, e), cpuTime).dist -
                               mapPlanet(vec3(0.0f, 0.0f, -e), cpuTime).dist));

        // 3. Depenetrate the camera
        float penetration = 1.5f - pd.dist;

        camPos[0] += normal.x * penetration;
        camPos[1] += normal.y * penetration;
        camPos[2] += normal.z * penetration;

        camDeltaVec.x += normal.x * penetration;
        camDeltaVec.y += normal.y * penetration;
        camDeltaVec.z += normal.z * penetration;

        planetCenterArr[0] = (float)(worldPlanetCenter[0] - camPos[0]);
        planetCenterArr[1] = (float)(worldPlanetCenter[1] - camPos[1]);
        planetCenterArr[2] = (float)(worldPlanetCenter[2] - camPos[2]);

        planetCenter =
            vec3(planetCenterArr[0], planetCenterArr[1], planetCenterArr[2]);
      }
    }

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    float aspect = (float)w / (float)h;

    cpuTime = (float)SDL_GetTicks() / 1000.0f;

    // 1. Update the global time variable visible to the CPU-compiled sdf.glsl
    // code
    vec3 camPosWorld((float)camPos[0], (float)camPos[1], (float)camPos[2]);

    // --- Life simulation LOD: activate/deactivate creatures based on
    // distance from the camera BEFORE physics runs, so anything that
    // just came alive this frame still gets simulated this frame. ---
    updateActivation(this, creaturePopulation, camPosWorld, bodyActors, bodies);

    if (!bodies.empty())
      updatePhysicsCPU(bodies, dt, camDeltaVec, portalManager);
    portalManager.teleportBodies(bodies);

    // --- Articulated bodies: position every non-root limb from its
    // parent chain now that the root has settled for this frame. ---
    updateLimbKinematics(bodies, dt);

    // --- Life simulation ecosystem: predation/scavenging first (Steps
    // 1.4/1.5), then metabolism/grazing/brain/reproduction (everything
    // else) so this tick's reward reflects the full delta-energy. ---
    resolvePredationAndScavenging(bodyActors, bodies, creaturePopulation);
    stepEcosystem(this, bodyActors, bodies, creaturePopulation, camPosWorld,
                  dt);

    updateCullingCPU(bodies, camForward, camRight, camUp, aspect, 300.0f,
                     visibleIndices);
    uploadBodies(ssbo, bodies);
    uploadVisible(visibleSSBO, visibleIndices);

    avgFrameTime = avgFrameTime * 0.9f + dt * 0.1f;
    if (avgFrameTime > 1.15f / 60.0f)
      qualityScale = std::max(0.5f, qualityScale - 0.01f);
    else if (avgFrameTime < 0.9f / 60.0f)
      qualityScale = std::min(1.0f, qualityScale + 0.01f);

    // --- Calculate Universal Matrices ---
    mat4 viewMat = Renderer::getView(camRight, camUp, camForward);
    mat4 projMat = Renderer::getProj(aspect, 2.0f * std::atan(1.0f / 1.5f),
                                     0.1f, 30000.0f);

    int virtH = (int)(90.0f + 45.0f * qualityScale);
    int virtW = (int)(virtH * aspect);

    if (virtW != currentVirtW || virtH != currentVirtH) {
      currentVirtW = virtW;
      currentVirtH = virtH;

      glBindTexture(GL_TEXTURE_2D, virtualColor);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, virtW, virtH, 0, GL_RGB,
                   GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

      // Setup secondary virtualVolumetric target
      glBindTexture(GL_TEXTURE_2D, virtualVolumetric);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, virtW, virtH, 0, GL_RGBA,
                   GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

      // Portal-id/bounce-count target the object phantom pass reads back.
      // RGBA16F (not RGBA8) so small integers like portal index and
      // bounce count round-trip exactly instead of being clamped/
      // quantized to [0,1].
      glBindTexture(GL_TEXTURE_2D, virtualPortalMeta);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, virtW, virtH, 0, GL_RGBA,
                   GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

      glBindTexture(GL_TEXTURE_2D, virtualDepth);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, virtW, virtH, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

      glBindFramebuffer(GL_FRAMEBUFFER, virtualFBO);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, virtualColor, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                             GL_TEXTURE_2D, virtualVolumetric,
                             0); // Attach volumetric texture
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                             GL_TEXTURE_2D, virtualPortalMeta, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                             virtualDepth, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, virtualFBO);
    glViewport(0, 0, virtW, virtH);

    // ==========================================
    // 1. Raymarcher
    // ==========================================
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);

    // Enable all three color targets so the raymarcher can write to
    // FragColor, VolumetricTex, and PortalMeta
    GLuint raymarchDrawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                     GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, raymarchDrawBuffers);

    glUseProgram(renderProgram);
    glUniform1i(glGetUniformLocation(renderProgram, "uPortalCount"),
                portalManager.count());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, macroCubeMap);
    glUniform1i(uMacroMapLoc, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, gyroidTex);
    glUniform1i(uGyroidTexLoc, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialTextureArray);
    glUniform1i(uMaterialTexturesLoc, 2);

    glUniform2f(uRenderResolution, (float)virtW, (float)virtH);
    glUniform3fv(uRenderPlanetCenter, 1, planetCenterArr);
    glUniform3f(uRenderCamForward, camForward.x, camForward.y, camForward.z);
    glUniform3f(uRenderCamRight, camRight.x, camRight.y, camRight.z);
    glUniform3f(uRenderCamUp, camUp.x, camUp.y, camUp.z);
    glUniform2f(uRenderVirtualRes, 160.0f + 80.0f * qualityScale,
                90.0f + 45.0f * qualityScale);
    glUniform1i(uRenderMaxIter, (int)(60.0f + 60.0f * qualityScale));
    glUniform1f(uRenderTime, (float)SDL_GetTicks() / 1000.0f);
    glUniformMatrix4fv(uRenderView, 1, GL_FALSE, viewMat.m);
    glUniformMatrix4fv(uRenderProj, 1, GL_FALSE, projMat.m);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ==========================================
    // 2. Rasterized Physics Objects
    // ==========================================
    glDepthFunc(GL_LESS);

    // Switch back to drawing only on FragColor for the rasterized objects
    GLuint objectDrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, objectDrawBuffers);

    glUseProgram(objectProgram);
    glUniformMatrix4fv(glGetUniformLocation(objectProgram, "view"), 1, GL_FALSE,
                       viewMat.m);
    glUniformMatrix4fv(glGetUniformLocation(objectProgram, "proj"), 1, GL_FALSE,
                       projMat.m);
    glUniform3fv(glGetUniformLocation(objectProgram, "planetCenter"), 1,
                 planetCenterArr);
    glUniform1f(glGetUniformLocation(objectProgram, "time"), (float)cpuTime);
    glUniform1i(glGetUniformLocation(objectProgram, "uPortalCount"),
                portalManager.count());

    glUniform1i(uCamPortalLoc, portalManager.getCameraInsidePortalIndex());

    // Pass the active screen resolution for proper volumetric UV sampling
    glUniform2f(glGetUniformLocation(objectProgram, "resolution"), (float)virtW,
                (float)virtH);

    // Bind the virtualVolumetric texture for object.frag to use
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, virtualVolumetric);
    glUniform1i(glGetUniformLocation(objectProgram, "volumetricTex"), 3);

    // Bind the portal-id buffer the raymarch pass just wrote, for the
    // phantom pass's per-pixel mask test.
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, virtualPortalMeta);
    glUniform1i(glGetUniformLocation(objectProgram, "portalIdTex"), 4);

    GLint uModel = glGetUniformLocation(objectProgram, "model");
    GLint uColor = glGetUniformLocation(objectProgram, "objColor");
    GLint uWarpPortalIndex =
        glGetUniformLocation(objectProgram, "uWarpPortalIndex");
    GLint uClipMode = glGetUniformLocation(objectProgram, "uClipMode");
    GLint uClipPortalIndex =
        glGetUniformLocation(objectProgram, "uClipPortalIndex");

    glBindVertexArray(objVAO);

    // Draws every body + visual instance with whatever warp/clip uniforms
    // are currently set. `filterRadius <= 0` draws everything (used for
    // the normal pass); otherwise only objects within `filterRadius` of
    // `filterCenter` (camera-relative) are drawn — used by the straddle
    // and phantom passes so we're not re-issuing every object in the
    // world for every portal.
    auto drawAllObjects = [&](vec3 filterCenter, float filterRadius) {
      bool useFilter = filterRadius > 0.0f;

      // FIX: If using a filter (portal passes), check ALL bodies so distant
      // objects near portal destinations aren't restricted by the main camera's
      // 300u culling list.
      size_t bodyCount = useFilter ? bodies.size() : visibleIndices.size();
      for (size_t i = 0; i < bodyCount; i++) {
        uint32_t idx = useFilter ? i : visibleIndices[i];
        const auto &b = *bodies[idx];
        vec3 pos(b.pos[0], b.pos[1], b.pos[2]);
        vec3 ext(b.extents[0], b.extents[1], b.extents[2]);

        if (useFilter) {
          float boundRadius = b.type < 0.5f ? ext.x : length(ext);
          float effRadius = filterRadius + boundRadius;
          vec3 posDiff = pos - filterCenter;
          if (dot(posDiff, posDiff) > effRadius * effRadius)
            continue;
        }

        vec4 quat(b.rot[0], b.rot[1], b.rot[2], b.rot[3]);

        mat4 modelMat = Renderer::getModel(
            pos, quat, b.type < 0.5f ? vec3(ext.x, ext.x, ext.x) : ext);
        glUniformMatrix4fv(uModel, 1, GL_FALSE, modelMat.m);

        if (b.type < 0.5f) {
          glUniform3f(uColor, 0.85f, 0.75f, 0.55f);
          glDrawArrays(GL_TRIANGLES, sphereOffset, sphereCount);
        } else {
          glUniform3f(uColor, 0.40f, 0.20f, 0.50f);
          glDrawArrays(GL_TRIANGLES, cubeOffset, cubeCount);
        }

        float speed = std::sqrt(b.vel[0] * b.vel[0] + b.vel[1] * b.vel[1] +
                                b.vel[2] * b.vel[2]);
        glUniform1f(glGetUniformLocation(objectProgram, "objSpeed"), speed);
      }

      for (const auto *inst : visualInstances) {
        vec3 p(inst->pos[0], inst->pos[1], inst->pos[2]);
        if (mAlwaysActiveActors.find(inst->GetOwner()) !=
            mAlwaysActiveActors.end()) {
          p = vec3(inst->GetOwner()->GetPosition().x - camPos[0],
                   inst->GetOwner()->GetPosition().y - camPos[1],
                   inst->GetOwner()->GetPosition().z - camPos[2]);
        }

        if (useFilter) {
          float boundRadius =
              inst->model ? inst->model->boundingRadius *
                                (float)std::max({inst->scale[0], inst->scale[1],
                                                 inst->scale[2]})
                          : 0.0f;
          float effRadius = filterRadius + boundRadius;
          vec3 pDiff = p - filterCenter;
          if (dot(pDiff, pDiff) > effRadius * effRadius)
            continue;

          // FIX: Also check maxLoadDist relative to the destination portal!
          vec3 checkPos = p - filterCenter;
          float distFromDest = length(checkPos);
          if (!inst->alwaysLoaded && distFromDest > inst->maxLoadDist)
            continue;

        } else {
          // Culling & Unloading logic for the normal pass
          float dist = length(p);
          if (!inst->alwaysLoaded && dist > inst->maxLoadDist)
            continue;

          float distZ = dot(p, camForward);
          float boundRadius = inst->model ? inst->model->boundingRadius : 1.0f;
          float cullRadius =
              boundRadius *
              std::max({inst->scale[0], inst->scale[1], inst->scale[2]});
          if (distZ < -cullRadius)
            continue;
        }

        vec4 q(inst->rot[0], inst->rot[1], inst->rot[2], inst->rot[3]);
        vec3 s(inst->scale[0], inst->scale[1], inst->scale[2]);

        mat4 modelMat = Renderer::getModel(p, q, s);
        glUniformMatrix4fv(uModel, 1, GL_FALSE, modelMat.m);
        glUniform3f(uColor, inst->color[0], inst->color[1], inst->color[2]);
        glUniform1f(glGetUniformLocation(objectProgram, "objSpeed"), 0.0f);

        glBindVertexArray(inst->model->vao);
        glDrawArrays(GL_TRIANGLES, 0, inst->model->vertexCount);
        glBindVertexArray(objVAO);
      }
    };

    // --- Pass 0: normal, true positions, hidden inside any portal bubble
    glUniform1i(uWarpPortalIndex, -1);
    glUniform1i(uClipMode, 0);
    drawAllObjects(vec3(0.0f, 0.0f, 0.0f), 0.0f);

    // --- Pass 1: straddle — objects actually touching a portal render
    // warped through it too, so they appear on both sides while crossing.
    constexpr float kStraddleMargin = 3.0f; // bubble radius + slack
    for (int i = 0; i < portalManager.count(); i++) {
      const Portal &p = portalManager.get(i);
      if (!p.active || p.linkedIndex < 0)
        continue;
      vec3 pCamRel(p.position.x - camPos[0], p.position.y - camPos[1],
                   p.position.z - camPos[2]);

      glUniform1i(uWarpPortalIndex, i);
      glUniform1i(uClipMode, 1);
      drawAllObjects(pCamRel, p.radius + kStraddleMargin);
    }

    // --- Pass 2: phantom — objects near a portal's destination are drawn
    // warped onto our side, masked to exactly the pixels the raymarcher
    // says are showing that portal's mouth. This is the "see the far
    // side's scenery through the portal from a distance" case, distinct
    // from straddling.
    //
    // Visibility check first: with up to 20 mouths, most aren't in view
    // any given frame, so skip the draw entirely for ones that can't be
    // — same frustum-plane test updateCullingCPU already uses for
    // bodies, just applied to the portal's own bubble instead. This can
    // false-positive (a mouth that's in-frustum but actually occluded by
    // terrain still gets drawn) but never false-negatives, so it's a
    // pure win: no visual difference, fewer wasted draws.
    constexpr float kPhantomViewDistance = 2000.0f; // tune to taste/perf
    for (int i = 0; i < portalManager.count(); i++) {
      const Portal &p = portalManager.get(i);
      if (!p.active || p.linkedIndex < 0)
        continue;

      vec3 pCamRel2(p.position.x - camPos[0], p.position.y - camPos[1],
                    p.position.z - camPos[2]);
      float distZ = dot(pCamRel2, camForward);
      float distX = dot(pCamRel2, camRight);
      float distY = dot(pCamRel2, camUp);
      if (distZ < -p.radius)
        continue; // fully behind the camera
      if (std::fabs(distX) > (std::fabs(distZ) * aspect / 1.5f) + p.radius ||
          std::fabs(distY) > (std::fabs(distZ) * 1.0f / 1.5f) + p.radius)
        continue; // outside the view frustum

      const Portal &dst = portalManager.get(p.linkedIndex);
      vec3 dstCamRel(dst.position.x - camPos[0], dst.position.y - camPos[1],
                     dst.position.z - camPos[2]);

      glUniform1i(uWarpPortalIndex, p.linkedIndex); // warp FROM dst TO here
      glUniform1i(uClipMode, 2);
      glUniform1i(uClipPortalIndex, i); // which mouth to match on screen
      drawAllObjects(dstCamRel, kPhantomViewDistance);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, virtualFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    glBlitFramebuffer(0, 0, virtW, virtH, 0, 0, w, h, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    SDL_GL_SwapWindow(window);
  }
  glDeleteTextures(1, &materialTextureArray);
  glDeleteTextures(1, &virtualVolumetric);
  glDeleteTextures(1, &virtualPortalMeta);
  portalManager.destroyGL();
  SDL_GL_DestroyContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return;
}