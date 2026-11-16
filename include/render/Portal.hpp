#pragma once

#include <cstdint>

#include "glsl_compat.h"

// One portal "mouth". Portals live in a flat array; a pair is just two
// entries whose linkedIndex point at each other. Nothing about rendering
// or teleport logic is hardcoded to "portal 1" / "portal 2" anymore, so
// this scales to as many pairs as we want (10 pairs = 20 entries) without
// touching the CPU or GPU code that consumes them.
struct Portal {
  vec3 position{0.0f, 0.0f, 0.0f}; // world-space (planet-space) position
  vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // orientation quaternion; local +Z
                                         // is the portal's forward/normal
  float radius = 8.0f;
  int32_t linkedIndex = -1; // index of the paired portal, -1 = unpaired
  bool active = false;      // inactive portals are skipped everywhere

  Portal() = default;
  Portal(vec3 pos, vec4 rot, float r) : position(pos), rotation(rot), radius(r) {}
};

// GPU-visible packed form, std430 layout. Every field is a vec4/ivec4 so
// there is no compiler-dependent padding ambiguity (a raw mat3 in std430
// pads each column to 16 bytes anyway, so we just store the columns as
// vec4 ourselves and skip the guesswork). 64 bytes per portal.
//
// Mirrors the `Portal` struct declared in assets/shaders/portal.glsl —
// keep the two in sync if either changes.
struct PortalGPU {
  float posRadius[4];   // xyz = position (camera-relative, set per frame), w = radius
  float ownNormal[4];   // xyz = this portal's own forward/normal axis
                        // (world-space direction, no camera offset needed
                        // since it's a direction, not a position), w unused
  float rotToLinkedC0[4]; // column 0 of the rotation that maps this portal's
                          // local frame to the linked portal's local frame
  float rotToLinkedC1[4];
  float rotToLinkedC2[4];
  int32_t linkedIndex;
  int32_t active;
  int32_t _pad0;
  int32_t _pad1;
};

static_assert(sizeof(PortalGPU) == 96,
              "PortalGPU must match the std430 layout assumed by portal.glsl");
