// Portal SSBO + shared warp math, generalized from the old hardcoded
// uPortal1Pos/uPortal2Pos/uRot1To2/uRot2To1 uniforms to N portals.
// Binding point must match PortalManager::bind() on the CPU side
// (currently 1).
//
// Portals are spherical "bubbles" (matches the raySphereHit-based bend
// test and the CPU teleport threshold), not flat discs — a ray/entity is
// "in" a portal once it's within `radius` of its center, from any
// direction. That's what makes them walk-through-from-anywhere and easy
// to reposition.
//
// Portals have no navigable interior. Crossing a portal's boundary
// doesn't move you to the corresponding near-side point on the linked
// portal (portalWarpPoint — a plain rigid transform, which would let you
// dwell inside the bubble, walking from one center to the other); it
// carries you straight through to wherever that path naturally exits the
// linked portal's FAR surface instead (portalExitPoint), continuing
// outward. There is no "inside a portal" state — only outside-A and
// outside-B, and nothing positioned inside either bubble's radius is
// ever visible or reachable through it. portalWarpPoint still exists for
// mapping arbitrary, non-crossing points (e.g. the phantom object pass
// displaying distant scenery through a portal, which isn't a crossing
// event at all).

struct PortalGPU {
  vec4 posRadius;    // xyz = position, camera-relative. w = bubble radius
  vec4 ownNormal;    // xyz = this portal's own forward axis (unused by the
                      // sphere-bubble math below, kept for future use —
                      // e.g. orienting a rendered portal "window" mesh)
  vec4 rotToLinkedC0; // this->linked rotation, column 0
  vec4 rotToLinkedC1;
  vec4 rotToLinkedC2;
  ivec4 meta;         // x = linkedIndex, y = active(0/1), z/w = reserved
};

layout(std430, binding = 1) readonly buffer PortalBuffer {
  PortalGPU uPortals[];
};

uniform int uPortalCount;

// Fixed upper bound for local (non-SSBO) arrays sized by portal count —
// GLSL local arrays can't be sized dynamically. 32 covers 10 pairs with
// room to grow; keep in sync with any local `float x[MAX_PORTALS]` usage.
#define MAX_PORTALS 32

mat3 portalRotToLinked(PortalGPU p) {
  return mat3(p.rotToLinkedC0.xyz, p.rotToLinkedC1.xyz, p.rotToLinkedC2.xyz);
}

// Maps a point from portal `i`'s local frame into its linked portal's
// frame. Same formula used by the raymarcher's ray-bend and by
// object.vert's vertex warp — one source of truth for both.
vec3 portalWarpPoint(int i, vec3 pos) {
  PortalGPU p = uPortals[i];
  PortalGPU dst = uPortals[p.meta.x];
  vec3 local = pos - p.posRadius.xyz;
  return dst.posRadius.xyz + portalRotToLinked(p) * local;
}

// Same, for a direction (no translation, renormalized).
vec3 portalWarpDir(int i, vec3 dir) {
  return normalize(portalRotToLinked(uPortals[i]) * dir);
}

// Radially projects `p` onto portal `i`'s own sphere surface if `p` is
// currently inside it; otherwise returns `p` unchanged. Nothing is ever
// allowed to be treated as "inside" a portal — no ray or teleport ever
// lands there (they always jump straight from one boundary to the
// other) — so any position that would be gets pulled out to the nearest
// point on the boundary first, before it's used for anything else.
// Cheap no-op for the vast majority of callers (most positions are
// nowhere near a portal at all).
vec3 clampOutsidePortal(int i, vec3 p) {
  PortalGPU port = uPortals[i];
  vec3 local = p - port.posRadius.xyz;
  float d = length(local);
  if (d >= port.posRadius.w) return p;
  vec3 n = d > 0.0001 ? local / d : vec3(0.0, 0.0, 1.0);
  return port.posRadius.xyz + n * port.posRadius.w;
}

// Where something crossing INTO portal `i` at point `p`, moving in
// direction `dir` (already in the SAME frame as `p`, i.e. NOT yet
// rotated — this function rotates it), actually ends up. Two wrong ways
// to do this, for context:
//
//   1. portalWarpPoint(i, p) alone — the corresponding point on the
//      linked portal's NEAR side. Correct positionally, but lets you
//      dwell inside the bubble: you'd land just past the linked
//      portal's entry and have to walk all the way through its interior
//      to come back out, same problem we're trying to remove.
//   2. Reflecting the entry point through the linked portal's center
//      (an earlier version of this function did this) — does skip the
//      interior, but a point reflection in 3D is a parity-flipping
//      (mirror) transform, not a rotation. Every direction seen through
//      the portal ends up mirrored, which shows up as an inverted,
//      distance-dependent-looking image — wrong.
//
// The correct way: map the entry point and direction directly
// (chirality-preserving, same as portalWarpPoint/portalWarpDir — no
// reflection), then follow that straight line through to wherever it
// naturally exits the linked portal's sphere (ray-sphere intersection,
// far root). That's still a pure rotation + translation, just evaluated
// a bit further along — so it skips the interior without mirroring
// anything.
//
// One consequence worth knowing: since both rays and teleporting
// entities always jump straight from the near surface to the far one,
// nothing positioned *inside* either portal's bubble radius is ever
// visible through it or reachable by crossing it — there's no way to
// see or land "inside" the geometry itself, by design.
vec3 portalExitPoint(int i, vec3 p, vec3 dir) {
  // A near-zero direction (e.g. a static mesh with no real velocity)
  // has nothing meaningful to follow through — fall back to the
  // direction pointing away from the source portal's center, which is
  // always well-defined for a point that's actually crossing.
  vec3 safeDir = dot(dir, dir) > 0.000001 ? dir : (p - uPortals[i].posRadius.xyz);

  // 3. CRITICAL: Normalize so the ray-sphere math (a=1) functions correctly!
  safeDir = normalize(safeDir);

  vec3 entry = portalWarpPoint(i, p);

  // For debugging, remove
  //return uPortals[uPortals[i].meta.x].posRadius.xyz + uPortals[uPortals[i].meta.x].posRadius.xyz - entry + 0.01 * portalWarpDir(i, safeDir); // offset to avoid re-triggering

  vec3 dirB = portalWarpDir(i, safeDir);

  // For debugging, remove
  return uPortals[uPortals[i].meta.x].posRadius.xyz + uPortals[uPortals[i].meta.x].posRadius.xyz - entry + 0.01 * dirB; // offset to avoid re-triggering

  PortalGPU dst = uPortals[uPortals[i].meta.x];
  vec3 oc = entry - dst.posRadius.xyz;
  float b = dot(oc, dirB);
  float c = dot(oc, oc) - dst.posRadius.w * dst.posRadius.w;
  // entry is on/inside the destination sphere by construction (it's the
  // direct image of a point on/inside the source sphere), so h should
  // never be negative in practice; clamp defensively against float error
  // when entry is essentially exactly on the surface.
  float h = max(b * b - c, 0.0);
  float tFar = -b + sqrt(h);

  return entry + dirB * tFar + dirB * 0.01; // offset to avoid re-triggering
}

// Ray-sphere hit distance, or -1.0 if the ray misses / points away.
// (Same shape as render.frag's existing raySphereHit — duplicated here
// so portal.glsl has no ordering dependency on being #included after it.)
float portalSphereHit(vec3 ro, vec3 rd, vec3 center, float radius) {
  vec3 oc = ro - center;
  float b = dot(oc, rd);
  float c = dot(oc, oc) - radius * radius;
  float h = b * b - c;
  if (h < 0.0) return -1.0;
  float tHit = -b - sqrt(h);
  return tHit > 0.0 ? tHit : -1.0;
}

// Bends a ray through portal `i`, exactly reproducing the old per-portal
// branch in render.frag's march loop (snap to the exact bubble-entry
// distance, nudge past it, then re-derive `ro` so `ro + rd * t` keeps
// landing on the correct point for all future `t` along the new
// direction — this is what lets `t` stay a single continuously
// accumulating value across bends, which is also what keeps
// gl_FragDepth's `proj * view * vec4(ro + rd * t, 1.0)` correct without
// any special-casing after a bend).
void portalBendRay(int i, float tHit, inout vec3 ro, inout vec3 rd, inout float t) {
  
  vec3 hitPos = ro + rd * tHit;

  vec3 portalCenter = uPortals[i].posRadius.xyz;
  float portalRadius = uPortals[i].posRadius.w;

  vec3 planeNormal = normalize(portalCenter - ro);

  vec3 reflectedHitPos = hitPos - 2.0 * dot(hitPos - portalCenter, planeNormal) * planeNormal;




  vec3 virtualCameraPos = portalCenter + portalRadius * planeNormal - planeNormal * length(portalCenter - ro - portalRadius * planeNormal);
  ro = portalWarpPoint(i,virtualCameraPos);

  vec3 virtualR = reflectedHitPos - virtualCameraPos;
  
  t = length(virtualR) + 0.01;
  rd = portalWarpDir(i, normalize(virtualR));

  //vec3 enterPos = portalWarpPoint(i, hitPos);
  

  //vec3 exitPos = portalExitPoint(i, hitPos, rd);
  //
  //
  //
  //rd = portalWarpDir(i, rd);
  //
  ////vec3 travelVector = (exitPos - enterPos);
  //
  //exitPos += rd * 0.01;
  //t = tHit + 0.01;
  //ro = exitPos - rd * t;
}
