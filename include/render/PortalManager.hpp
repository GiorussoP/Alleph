#pragma once

#include <utility>
#include <vector>

#include "render/MathUtil.hpp"
#include "render/Portal.hpp"

// Owns every portal mouth in the world (both ends of every pair, e.g. 20
// entries for 10 repositionable pairs) and is the single source of truth
// consumed by:
//   - the CPU camera/body-teleport checks below, and
//   - the GPU raymarcher / phantom object pass, via the SSBO bound in
//     assets/shaders/portal.glsl.
//
// Nothing here is hardcoded to a fixed portal count. Adding an 11th pair
// is just another addPortalPair() call.
//
// Portals have no navigable interior: crossing a portal's boundary
// (`radius` from its center, from any direction) carries you straight
// through, in your direction of travel, to wherever that path naturally
// exits the *far* surface of its linked portal — not to the
// corresponding near-side point, and not to a point-reflected position
// either (that would flip handedness and look mirrored). See
// exitPointFor()/portalExitPoint() in portal.glsl. There is no "inside a
// portal" state to be in, and nothing positioned inside either bubble's
// radius is ever visible or reachable through it.
class PortalManager {
public:
  PortalManager() = default;

  // Call once, after the GL context exists, before the first update().
  void initGL();
  void destroyGL();

  // Registers a new linked pair and returns the two indices assigned to
  // it (portal[first] <-> portal[second]).
  std::pair<int, int> addPortalPair(vec3 posA, vec4 rotA, vec3 posB, vec4 rotB,
                                    float radius);

  // Repositions/reorients an existing portal in place (used for
  // "repositionable" portals). The paired portal's transform-to-linked
  // matrix is recomputed automatically on the next update().
  void setPortalTransform(int index, vec3 pos, vec4 rot);
  void setActive(int index, bool active);

  int count() const { return static_cast<int>(portals_.size()); }
  const Portal &get(int index) const { return portals_[index]; }

  // Recomputes camera-relative positions and this->linked rotation
  // matrices for every active, linked portal, then re-uploads the SSBO.
  // Call once per frame, after the camera position for the frame is
  // finalized, before the raymarch/object draw calls.
  void update(const double camPos[3]);

  // Binds the portal SSBO for shader consumption. Binding point must
  // match `layout(std430, binding = N)` in portal.glsl (default 1).
  void bind(unsigned int bindingPoint = 1) const;

  // Checks every active, linked portal for camera proximity (full
  // `radius`, matching exactly what the renderer bends rays at) and, if
  // the camera has crossed one, relocates it and rotates its basis
  // vectors to the *far* side of the linked portal via exitPointFor() —
  // the camera can never end up "inside" a portal's interior. Excludes
  // whichever portal the camera just exited from (getCameraInsidePortalIndex())
  // so it can walk away from the exit without immediately re-triggering.
  void checkTeleport(double camPos[3], vec3 &camForward, vec3 &camRight,
                     vec3 &camUp);
  int getCameraInsidePortalIndex() const { return mCameraLastPortal; }

  // Same idea, but for physics bodies: position, velocity, and the
  // body's own orientation are all carried through the portal's
  // rotation, landing past the linked portal's far surface exactly like
  // the camera does. Call once per physics step, after integration.
  void teleportBodies(std::vector<class RigidBodyComponent *> &bodies) const;

  // True if `pos` is within any active portal's boundary. Used to know
  // when a camera/body has fully cleared a portal and its exclusion
  // (see checkTeleport/teleportBodies) can be reset.
  bool isInsideAnyPortal(vec3 pos) const;

  // Index of the first active, linked portal whose boundary (`radius`)
  // contains `pos`, excluding `excludePortalIndex` (the portal something
  // just exited from, so it doesn't immediately re-trigger on the way
  // out) — or -1 if none.
  int findTriggeredPortal(vec3 pos, int excludePortalIndex) const;

  // The rotation that carries a vector/orientation from portal
  // `portalIndex`'s local frame into its linked portal's frame — same
  // matrix PortalManager uploads to the GPU for that portal.
  mat3 rotationToLinked(int portalIndex) const;
  vec3 portalPosition(int portalIndex) const {
    return portals_[portalIndex].position;
  }
  vec3 linkedPosition(int portalIndex) const {
    return portals_[portals_[portalIndex].linkedIndex].position;
  }

  // Where a point crossing INTO portal `portalIndex` at `p`, moving in
  // direction `dir` (un-rotated — this rotates it internally), actually
  // ends up: mapped directly (chirality-preserving) onto the linked
  // portal's near side, then followed straight through, in the rotated
  // direction, to wherever it naturally exits the linked portal's FAR
  // surface — exact CPU-side mirror of portalExitPoint() in portal.glsl.
  // Used by checkTeleport/teleportBodies and available for
  // physics/collision queries (e.g. testing a collision point that's
  // inside a portal against the *other* side's terrain).
  vec3 exitPointFor(int portalIndex, vec3 p, vec3 dir) const;

  // For physics: if `p` is inside any portal's boundary (other than
  // `excludePortalIndex`), returns where it actually is per
  // exitPointFor() (using `dir`, or the portal's own outward normal at
  // `p` if `dir` is ~zero, as the direction of travel); otherwise
  // returns `p` unchanged. Intended for SDF collision queries — e.g.
  // testing whether there's ground under a point that's geometrically
  // inside a portal should test the *other* side's terrain, not empty
  // space that happens to numerically exist inside the bubble.
  vec3 warpPhysicsPoint(vec3 p, vec3 dir = vec3(0.0f, 0.0f, 0.0f),
                        int excludePortalIndex = -1) const;

private:
  std::vector<Portal> portals_;
  mutable std::vector<PortalGPU> gpuScratch_;
  unsigned int ssbo_ = 0;
  int mCameraLastPortal = -1;
};
