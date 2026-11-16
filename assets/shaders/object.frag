#version 430 core
in vec3 vPos;
in vec3 vNorm;
in vec3 vTruePos;
in vec3 vTrueNorm;
out vec4 FragColor;

uniform vec3 planetCenter;
uniform float time;
uniform vec3 objColor;
uniform float objSpeed;

uniform vec2 resolution;
uniform sampler2D volumetricTex;

uniform mat4 view;
uniform mat4 proj;

#include "portal.glsl"

uniform sampler2D portalIdTex;

uniform int uClipMode;
uniform int uClipPortalIndex;
uniform int uWarpPortalIndex;

vec3 getSunDir() {
    float orbitTime = time * (6.2831853 / 1440.0);
    return normalize(vec3(cos(orbitTime), 0.05, sin(orbitTime)));
}

vec3 getSkyAmbientColor(vec3 normPos, vec3 sunDir) {
    float atmosSunDot = dot(normPos, sunDir);
    vec3 dayColor = vec3(0.3, 0.65, 1.0); vec3 nightColor = vec3(0.04, 0.1, 0.3); vec3 sunsetColor = vec3(0.95, 0.45, 0.15);
    vec3 surfaceAtmosphere = mix(nightColor, dayColor, smoothstep(0.45, 0.50, atmosSunDot));
    float sunsetBand = smoothstep(0.45, 0.0, abs(atmosSunDot - 0.45));
    return mix(surfaceAtmosphere, sunsetColor, sunsetBand);
}

void main() {
    // --- PORTAL CLIPPING ---
    if (uClipMode == 0) {
        vec2 screenUV = gl_FragCoord.xy / resolution;
        vec4 meta = texture(portalIdTex, screenUV);
        int bounceCountHere = int(meta.y + 0.5);
        float firstPortalT = meta.z;
        float distFromCam = length(vPos);

        if (bounceCountHere > 0 && distFromCam > firstPortalT) discard;

        for (int i = 0; i < uPortalCount; i++) {
            if (uPortals[i].meta.y == 0) continue;
            if (length(vPos - uPortals[i].posRadius.xyz) < uPortals[i].posRadius.w) discard;
        }
    } else if (uClipMode == 1) {
        // Straddle: only the part of the object that's actually crossed
        // the source portal's boundary should show up warped (the rest
        // is drawn normally by the mode-0 pass). With the new
        // "opposite side" exit, the warped vPos always lands just past
        // the destination bubble's far surface by design — it can never
        // land inside it anymore — so we can't test vPos against the
        // destination like before. Test whether the TRUE position has
        // crossed the SOURCE portal's boundary instead; that's the same
        // criterion object.vert used to decide to warp this vertex.
        if (length(vTruePos - uPortals[uWarpPortalIndex].posRadius.xyz) >
            uPortals[uWarpPortalIndex].posRadius.w) discard;
    } else if (uClipMode == 2) {
        vec2 screenUV = gl_FragCoord.xy / resolution;
        vec4 meta = texture(portalIdTex, screenUV);
        int seenPortalId = int(meta.x + 0.5) - 1;
        int bounceCount = int(meta.y + 0.5);
        if (seenPortalId != uClipPortalIndex || bounceCount != 1) discard;
    }

    vec3 ro = vec3(0.0);
    vec3 rd = normalize(vPos);
    float t = length(vPos);
    
    // Split your normals based on the space they need to interact with
    vec3 visualN = normalize(vNorm);     // Warped: Matches 'rd' for view-dependent effects
    vec3 trueN = normalize(vTrueNorm);   // Unwarped: Matches 'vTruePos' for physical lighting

    vec3 sunDir = getSunDir();
    vec3 normPos = normalize(vTruePos - planetCenter);

    // --- Core Lighting (Apply trueN) ---
    float sunFacing = clamp(dot(normPos, sunDir) * 5.0 + 0.5, 0.0, 1.0);
    float moonFacing = clamp(dot(normPos, -sunDir) * 5.0 + 0.5, 0.0, 1.0);
    
    // Evaluate sun/moon/hemisphere against the physical unwarped normal
    float diffSun = sunFacing * clamp(dot(trueN, sunDir), 0.0, 1.0);
    float diffMoon = moonFacing * clamp(dot(trueN, -sunDir), 0.0, 1.0);
    float hemisphere = clamp(dot(trueN, normPos) * 0.5 + 0.5, 0.1, 1.0);
    
    vec3 skyColorTrue = getSkyAmbientColor(normPos, sunDir); 
    vec3 ambientSky = skyColorTrue * hemisphere * 0.55;
    
    // --- Fresnel (Keep visualN) ---
    // Fresnel relies on the view direction (rd), which is calculated from the warped vPos. 
    float fresnel = pow(1.0 - max(dot(visualN, -rd), 0.0), 3.0);
    vec3 skyReflection = skyColorTrue * fresnel * 0.25;

    vec3 finalLit = objColor * (diffSun * vec3(1.1, 1.05, 0.9) + diffMoon * vec3(0.30, 0.5, 0.8) + ambientSky) + skyReflection + vec3(0.1);

    // --- Atmospheric Entry Heating (Re-entry Glow) ---
    float distToCenter = length(vTruePos - planetCenter);
    if (distToCenter >= 6000.0 && distToCenter < 7000.0 && objSpeed > 50.0) {
        float speedFactor = smoothstep(50.0, 250.0, objSpeed);
        float densityFactor = smoothstep(7000.0, 6000.0, distToCenter);
        vec3 entryGlow = vec3(1.0, 0.45, 0.05) * speedFactor * densityFactor * 4.0;
        finalLit += entryGlow;
    }

    // --- Subterranean Override ---
    float isCave = (distToCenter < 5900.0 && distToCenter > 1000.0) ? 1.0 : 0.0;
    if (isCave > 0.5) {
        vec3 pointLightDir = normalize(-vTruePos);
        // Fix the cave point light to also use the physical normal
        float diff = max(dot(trueN, pointLightDir), 0.0);
        float attenuation = 1.0 / (1.0 + 0.005 * t * t);
        finalLit = objColor * (diff * attenuation * 2.0 + 0.05);
    }

    // --- Deep Compositing (Fog & Clouds) ---
    vec2 screenUV = gl_FragCoord.xy / resolution;
    vec4 meta = texture(portalIdTex, screenUV); // Re-fetch for distance data
    vec4 cloudData = texture(volumetricTex, screenUV);
    vec3 clouds = cloudData.rgb;
    float cloudStartDist = cloudData.a;

    // Calculate the atmosphere on the camera's side of the portal
    vec3 skyColorWarped = getSkyAmbientColor(normalize(vPos - planetCenter), sunDir);

    float firstPortalT = meta.z;
    float distToPortal = t;
    float distPastPortal = 0.0;

    // If this is a Phantom Pass (viewed through a portal), split the distances physically
    if (uClipMode == 2) {
        distToPortal = min(t, firstPortalT);
        distPastPortal = max(0.0, t - firstPortalT);
    }

    // 1. Fog the object from its true location up to the portal glass
    float fogFactorPast = clamp(1.0 - exp(-distPastPortal * 0.0025), 0.0, 1.0);
    vec3 col = mix(finalLit, skyColorTrue, fogFactorPast);

    // 2. Fog that result from the portal glass to the camera
    float fogFactorTo = clamp(1.0 - exp(-distToPortal * 0.0025), 0.0, 1.0);
    col = mix(col, skyColorWarped, fogFactorTo);

    if (t > cloudStartDist) {
        float cloudDensity = clamp(dot(clouds, vec3(0.299, 0.587, 0.114)) * 1.5, 0.0, 1.0);
        col = min(col + clouds * cloudDensity, vec3(1.0));
    }

    FragColor = vec4(col, 1.0);
}