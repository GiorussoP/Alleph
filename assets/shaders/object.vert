#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

#include "portal.glsl"

// -1 = draw at the object's true position, unwarped.
// >= 0 = warp every vertex through uPortals[uWarpPortalIndex] into its
// linked portal's frame — same transform the raymarcher uses to bend
// rays, via the same SSBO. Used both for objects straddling a portal
// (warpIndex = the portal they're touching) and for the phantom pass
// that shows objects near a portal's destination as if they were at its
// mouth (warpIndex = that destination portal).
uniform int uWarpPortalIndex;
// Needed here (not just in the fragment stage) to pick which warp
// formula applies — same uniform object.frag uses for clipping, shared
// across both stages of the linked program.
uniform int uClipMode;
// Straddle pass only: the crossing object's direction of travel (world/
// camera-relative, unrotated — portalExitPoint rotates it internally),
// used the same way rd is for ray bends: to find where the straight line
// through this vertex actually exits the linked portal's far side.
// Ignored for the phantom pass (plain warp, no direction needed).
uniform vec3 uWarpDirection;

out vec3 vPos;
out vec3 vNorm;
out vec3 vTrueNorm;
// The object's real, un-warped position. Needed because the raymarcher's
// depth for portal-bent content is computed from the *actual* far-away
// destination point (e.g. genuinely ~12000 units away near the other
// pole), not from where that content gets drawn on screen. A phantom-pass
// object's warped position is deliberately near the *source* portal (so
// it rasterizes at the right screen location) — numerically much closer
// to the camera than the terrain depth it needs to sort against. See
// object.frag for how this gets used.
out vec3 vTruePos;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    vec3 norm = mat3(model) * aNorm; 
    
    vTruePos = worldPos.xyz;
    vTrueNorm = norm; // SAVE the unwarped normal for environment lighting

    if (uWarpPortalIndex >= 0) {
        if (uClipMode == 1) {
            worldPos.xyz = portalExitPoint(uWarpPortalIndex, worldPos.xyz, uWarpDirection);
        } else {
            worldPos.xyz = portalWarpPoint(uWarpPortalIndex, worldPos.xyz);
        }
       
        // FIX: Remove the minus sign to prevent the normal from flipping inside out
        norm = portalWarpDir(uWarpPortalIndex, norm); 
    }

    vPos = worldPos.xyz;
    vNorm = norm;
    gl_Position = proj * view * worldPos;
}