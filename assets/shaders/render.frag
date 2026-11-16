#version 430 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 VolumetricTex;
// x = portal index the primary ray was bent through, +1 (0 = no bend).
// y = number of bends the ray took to reach what it's showing.
// Consumed by object.frag's phantom pass to mask which screen pixels are
// allowed to show objects warped through a given portal.
layout(location = 2) out vec4 PortalMeta;

in vec2 TexCoords;

#include "sdf.glsl"
#include "portal.glsl"

#define MAX_RAYMARCH_ITERATIONS_CAP 100

uniform vec2 resolution;
uniform vec3 camForward;
uniform vec3 camRight;
uniform vec3 camUp;
uniform int uCameraInsidePortalIndex;

uniform vec2 virtualRes;
uniform int  maxIterations;
uniform sampler2DArray materialTextures;

// Provided by C++ for writing gl_FragDepth cleanly
uniform mat4 view;
uniform mat4 proj;

// Portal colors
vec3 getPortalPairColor(int portalIndex) {
    int pairId = portalIndex / 2; // Assuming portals are created in adjacent pairs
    vec3 pairColors[6] = vec3[](
        vec3(0.0, 0.85, 1.0), // Cyan (Pair 0)
        vec3(1.0, 0.45, 0.1), // Orange (Pair 1)
        vec3(1.0, 0.2, 0.7),  // Magenta (Pair 2)
        vec3(0.2, 1.0, 0.3),  // Lime (Pair 3)
        vec3(1.0, 0.9, 0.1),  // Yellow (Pair 4)
        vec3(0.6, 0.2, 1.0)   // Purple (Pair 5)
    );
    return pairColors[pairId % 6];
}

float calculateDepth(vec3 worldPos, mat4 viewMat, mat4 projMat) {
    vec4 clipPos = projMat * viewMat * vec4(worldPos, 1.0);
    float ndcDepth = clipPos.z / clipPos.w;
    return (ndcDepth * 0.5) + 0.5; // Map NDC range [-1, 1] to OpenGL depth [0, 1]
}

vec3 getSunDir() {
    float orbitTime = time * (6.2831853 / 1440.0);
    return normalize(vec3(cos(orbitTime), 0.15, sin(orbitTime)));
}

vec3 getSkyAmbientColor(vec3 normPos, vec3 sunDir) {
    float atmosSunDot = dot(normPos, sunDir);
    vec3 dayColor = vec3(0.3, 0.65, 1.0);
    vec3 nightColor = vec3(0.04, 0.1, 0.3);
    vec3 sunsetColor = vec3(0.95, 0.45, 0.15);
    vec3 surfaceAtmosphere = mix(nightColor, dayColor, smoothstep(0.45, 0.50, atmosSunDot));
    float sunsetBand = smoothstep(0.45, 0.0, abs(atmosSunDot - 0.45));
    return mix(surfaceAtmosphere, sunsetColor, sunsetBand);
}

SDFResult mapScene(vec3 p) {
    SDFResult planet = mapPlanet(p, time);

    vec3 sunDir = getSunDir();
    vec3 sunPos = planetCenter + sunDir * 12000.0;
    float dSun = sunSDF(p - sunPos);
    if (dSun < planet.dist) planet = makeSDF(dSun, MAT_SUNMAGMA);

    vec3 moonPos = planetCenter - sunDir * 12000.0;
    float dMoon = moonSDF(p - moonPos);
    if (dMoon < planet.dist) planet = makeSDF(dMoon, MAT_MOONROCK);

    return planet;
}

float mapSceneDistFast(vec3 p) {
    float d = mapPlanetDistanceFast(p, time);

    vec3 sunDir = getSunDir();
    vec3 sunPos = planetCenter + sunDir * 12000.0;
    d = min(d, sunSDF(p - sunPos));

    vec3 moonPos = planetCenter - sunDir * 12000.0;
    d = min(d, moonSDF(p - moonPos));

    return d;
}

vec3 calcNormal(vec3 p, float eps) {
    const vec2 k = vec2(1, -1);
    return normalize(
        k.xyy * mapSceneDistFast(p + k.xyy * eps) +
        k.yyx * mapSceneDistFast(p + k.yyx * eps) +
        k.yxy * mapSceneDistFast(p + k.yxy * eps) +
        k.xxx * mapSceneDistFast(p + k.xxx * eps)
    );
}

vec3 getMaterialBaseColor(int matID) {
    vec3 colors[16] = vec3[](
        vec3(0.40, 0.40, 0.40), // MAT_STONE
        vec3(0.90, 0.90, 0.60), // MAT_SAND
        vec3(0.15, 0.45, 0.15), // MAT_GRASS
        vec3(0.24, 0.16, 0.09), // MAT_DIRT
        vec3(0.95, 0.95, 0.98), // MAT_SNOW
        vec3(0.85, 0.85, 0.55), // MAT_SANDSTONE
        vec3(0.25, 0.335, 0.475), // MAT_ICE
        vec3(0.10, 0.80, 0.60), // MAT_GLOWING_CRYSTAL
        vec3(0.95, 0.35, 0.10), // MAT_MAGMA
        vec3(0.60, 0.60, 0.65), // MAT_MONROCK
        vec3(1.00, 0.95, 0.80), // MAT_SUNMAGMA
        vec3(0.40, 0.20, 0.50), // MAT_MINERAL

        vec3(0.15, 0.45, 0.85), // MAT_WATER
        vec3(0.90, 0.20, 0.05), // MAT_LAVA
        vec3(0.35, 0.25, 0.15), // MAT_MUD
        vec3(0.20, 0.90, 0.10) // MAT_ACID
    );
    return colors[matID];
}

float getTriplanarTexture(vec3 p, vec3 n, float layer) {
    vec3 blend = max(abs(n) - 0.2, 0.0);
    blend /= dot(blend, vec3(1.0));
    float scale = 0.005;

    const float AXIS_EPS = 0.02;
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float result = 0.0;

    if (blend.x > AXIS_EPS) result += dot(textureLod(materialTextures, vec3(p.yz * scale, layer), 0.0).rgb, luma) * blend.x;
    if (blend.y > AXIS_EPS) result += dot(textureLod(materialTextures, vec3(p.xz * scale, layer), 0.0).rgb, luma) * blend.y;
    if (blend.z > AXIS_EPS) result += dot(textureLod(materialTextures, vec3(p.xy * scale, layer), 0.0).rgb, luma) * blend.z;

    return result;
}

vec3 getWindOffset(vec3 p, float t) {
    vec3 wind = vec3(
        fbm(p * 0.05 + vec3(t * 0.5, 0.0, 0.0)),
        fbm(p * 0.05 + vec3(t * 0.5 + 10.0, 0.0, 0.0)),
        fbm(p * 0.05 + vec3(t * 0.5 + 20.0, 0.0, 0.0))
    );
    return normalize(wind); 
}

vec3 applyTriplanarBump(vec3 p, vec3 n, float rayDist, float matID, out float texVal) {
    vec2 e = vec2(0.05, 0.0);
    if (matID == MAT_GRASS || matID == MAT_SAND) {
        vec3 wind = getWindOffset(p, time);
        n = normalize(n + 0.5* wind);
    }
    texVal = getTriplanarTexture(p, n, matID);
    float hX = getTriplanarTexture(p + e.xyy, n, matID);
    float hY = getTriplanarTexture(p + e.yxy, n, matID);
    float hZ = getTriplanarTexture(p + e.yyx, n, matID);
    vec3 grad = vec3(hX - texVal, hY - texVal, hZ - texVal) / e.x;
    grad = grad - n * dot(grad, n);
    float distanceFade = clamp(1.0f - (rayDist / 100.0), 0.0, 1.0);
    return normalize(n - grad * (0.75 * distanceFade));
}

vec3 calculateLiquid(float liquidFactor, int matID, vec3 pos, vec3 baseNormal, vec3 rd, vec3 sunDir,float rayDist) {
    vec3 planetLocalPos = pos - planetCenter;
    vec3 normPos = normalize(planetLocalPos);

    float sunFacing = clamp(dot(normPos, sunDir) * 5.0 + 0.7, 0.0, 1.0);
    float moonFacing = clamp(dot(normPos, -sunDir) * 5.0 + 0.7, 0.0, 1.0);

    // --- Waterfall blend factor -------------------------------------------------
    // facingOut ~ 1 : surface points straight away from the core (flat ground / lakebed)
    // facingOut ~ 0 : surface is vertical relative to the core
    // facingOut < 0 : surface overhangs / faces back toward the core (cave ceiling, underside of a ledge)
    // poolFactor -> 1 on flat ground: normal standing-liquid look.
    // fallFactor -> 1 everywhere else: waterfall look.
    float facingOut = dot(normPos, baseNormal);
    float poolFactor = smoothstep(0.35, 0.85, facingOut * facingOut);
    float fallFactor = 1.0 - poolFactor;

    // --- One texture, two ways of sampling it ------------------------------
    // Exactly the water.gdshader trick: it's the SAME noise pattern used for
    // the still-water ripple everywhere. On flat ground the sample position
    // is static (calm pool). As the surface tilts away from "facing outward"
    // the sample position scrolls downhill over time - the pattern itself
    // never changes shape, it just slides, which reads as flowing/falling
    // without needing any separate stretched noise field or scale tuning.
    //
    // Important: the scroll direction is the RADIAL "down" projected onto
    // this surface's own tangent plane, not raw -normPos. Using raw -normPos
    // means part of the offset points into/out of the surface (along its
    // normal) on anything that isn't perfectly vertical, which drags the
    // sample point through different depth-slices of the 3D noise field
    // instead of sliding it across the visible surface - that mismatch is
    // what read as fast, warping, distorted noise on tilted ground.
    vec3 downTangent = -normPos + baseNormal * facingOut; // "down," with the along-normal part removed
    float downTangentLen = length(downTangent);
    vec3 flowDir = normalize((downTangentLen > 1e-4) ? downTangent / downTangentLen : vec3(0.0));

    float fallSpeed = (matID == MAT_WATER) ? 8.0 :
                       (matID == MAT_ACID)  ? 8.2 :
                       (matID == MAT_LAVA)  ? 0.9 : 0.6; // MAT_MUD
    // Sampling at (pos - flowDir*t) makes the pattern visually scroll in the
    // +flowDir direction (downhill) over time - see note above, this sign
    // was flipped before, which is why it was scrolling uphill.
    vec3 flowOffset = -flowDir * time  * 0.01 * fallSpeed * max(smoothstep(0.1, 0.9, fallFactor)- 0.1, 0.00);

    float scaleFactor =  rayDist > 500 ? 0.1 : rayDist > 200 ? 0.25 : rayDist > 100 ? 0.5 : 1.0;
    vec3 wavePos = (planetLocalPos + flowOffset) * 0.08;
    float speedMod = (matID == MAT_WATER) ? 1.0 : 0.4;
    float wave1 = ridgedNoise(wavePos * (1.0  + time * 0.00025 * speedMod)* scaleFactor);
    float wave2 = ridgedNoise(wavePos * (2.0 + time * 0.004 * speedMod) * scaleFactor);
    float wave3 = ridgedNoise(wavePos * (3.0  - time * 0.0015 * speedMod)* scaleFactor);
    float totalWave = (wave1 + wave2 + wave3) * 0.333;

    // --- Normal perturbation ----------------------------------------------------

    float distanceFade = clamp(1.0f - (rayDist / 100.0), 0.0, 1.0);
    float bumpiness = 0.5;
    vec3 grad = normalize(vec3(wave1, wave2, wave3)) * bumpiness * distanceFade;
    vec3 liquidNormal = normalize(baseNormal + grad - baseNormal * dot(baseNormal, grad));


    vec3 baseCol = getMaterialBaseColor(matID);
    vec3 skyColor = getSkyAmbientColor(normPos, sunDir);
    float fresnel = pow(1.0 - max(dot(liquidNormal, -rd), 0.0), 5.0);

    vec3 liquidShade = mix(baseCol * 0.5, baseCol, smoothstep(0.2, 0.8, totalWave));

    liquidShade = min(mix(0.25 * (liquidShade +  skyColor),skyColor,fresnel), vec3(1.0));
    

    // Foam
    if (matID != MAT_MUD) {
        vec3 foamCol = (matID == MAT_WATER) ? vec3(0.85, 0.95, 1.0) :
                       (matID == MAT_ACID)  ? vec3(0.9, 1.0, 0.9)   :
                       (matID == MAT_LAVA)  ? vec3(1.0, 0.8, 0.2)   : vec3(0.8, 0.8, 0.8);
        float coastalNoise = fbm(planetLocalPos  +  normalize(mix(vec3(1.0),flowOffset, fallFactor)) * time);
        float edgeProximity = smoothstep(0.9, 0.5, liquidFactor) * smoothstep(0.05, 0.3, liquidFactor);
        liquidShade = mix(liquidShade, foamCol, smoothstep(0.85, 0.95, totalWave) * smoothstep(0.3, 0.4, liquidFactor) * coastalNoise);
         // Shoreline foam only makes sense on flat/pooled liquid.
        liquidShade = mix(liquidShade, foamCol, min(0.5 * edgeProximity + edgeProximity *  smoothstep(0.25, 0.6, coastalNoise * edgeProximity),1.0));

        // "White water" riding the falling wave crests.
        float fallHighlight = smoothstep(0.05, 0.45, coastalNoise *totalWave * smoothstep(0.05, 0.1, liquidFactor)) * fallFactor;
        liquidShade = mix(liquidShade, foamCol, fallHighlight * (matID == MAT_LAVA ? 0.5 : 0.9));
    }

    //return liquidShade;

    if (matID == MAT_LAVA || matID == MAT_ACID || matID == MAT_SUNMAGMA) {
        // Extra glow riding along the falling wave crests (molten/corrosive runoff).
        float streakGlow = fallFactor * smoothstep(0.5, 0.85, totalWave);
        return liquidShade * mix(2.0, 3.2, streakGlow);
    }

    float diffSun = sunFacing * clamp(dot(liquidNormal, sunDir) * 3.0, 0.0, 1.0);
    float diffMoon = moonFacing * clamp(dot(liquidNormal, -sunDir) * 3.0, 0.0, 1.0);
    vec3 finalCol = liquidShade * (diffSun * 0.8 + diffMoon * 0.3 + skyColor);

    
    if (matID == MAT_MUD) return finalCol;

    vec3 sunRefDir = reflect(-sunDir, liquidNormal);
    float specSun = pow(clamp(dot(sunRefDir, -rd), 0.0, 1.0), 32.0);
    vec3 sunGlint = (sunFacing > -0.1) ? vec3(1.0, 0.95, 0.8) * specSun * 0.85 * smoothstep(-0.1, 0.2, diffSun) : vec3(0.0);

    vec3 moonRefDir = reflect(sunDir, liquidNormal);
    float specMoon = pow(clamp(dot(moonRefDir, -rd), 0.0, 1.0), 32.0);
    vec3 moonGlint = (moonFacing > -0.1) ? vec3(0.6, 0.7, 1.0) * specMoon * 0.5 * smoothstep(-0.1, 0.2, diffMoon): vec3(0.0);


    //return skyColor;

    //vec3 skyReflection = skyColor * fresnel;

    return min(finalCol + sunGlint + moonGlint, vec3(1.0));
}

vec3 moveNorthToSouth(vec3 pos, float time, float speed) {
    float r = length(pos); if(r<0.0001) return pos; vec3 dir = pos/r;
    float theta = acos(clamp(dir.y,-1.0,1.0)); float phi = atan(dir.z, dir.x);
    theta = mod(theta - 10*time*speed, 3.14159265);
    dir.y = cos(theta); dir.x = sin(theta)*cos(phi); dir.z = sin(theta)*sin(phi);
    return dir * r;
}

vec2 raySphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center; float b = dot(oc, rd); float c = dot(oc, oc) - radius * radius;
    float h = b * b - c; if (h < 0.0) return vec2(-1.0);
    h = sqrt(h); return vec2(-b - h, -b + h);
}

float raySphereHit(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    
    // Return -1.0 if the ray misses the sphere entirely, or points away
    if (h < 0.0) return -1.0;
    
    float tHit = -b - sqrt(h);
    return tHit > 0.0 ? tHit : -1.0;
}

vec3 sampleCloudAtDepth(vec3 ro, vec3 rd, float t_hit, vec3 windAxis, float windSpeed, float noiseScale, vec2 coverageThreshold, float shellThickness, float densityScale, vec3 sunDir) {
    vec3 cloudPos = ro + rd * t_hit; vec3 absPos = cloudPos - planetCenter; vec3 normPos = normalize(absPos);
    float viewAngle = abs(dot(normPos, rd)); float pathLength = shellThickness / (viewAngle + 0.08);
    vec3 posBase = absPos; vec3 posTop  = absPos + normPos * shellThickness;
    vec3 rotBase = moveNorthToSouth(posBase, time, windSpeed * 0.05); vec3 rotTop  = moveNorthToSouth(posTop,  time, windSpeed * 0.05);
    vec3 morphOffset = vec3(sin(time * 0.0004) * 12.0 + time * 0.00015, cos(time * 0.0003) * 12.0, sin(time * 0.0005) * 12.0 + time * 0.00015);
    float nBase = fbm(rotBase * noiseScale + morphOffset); float nTop  = fbm(rotTop  * noiseScale * 1.2 + morphOffset * 1.2);
    float baseAlpha = smoothstep(coverageThreshold.x, coverageThreshold.y, nBase); float topAlpha  = smoothstep(coverageThreshold.x - 0.1, coverageThreshold.y, nTop);
    float cloudDensity = baseAlpha * topAlpha;
    if (cloudDensity <= 0.001) return vec3(0.0);
    cloudDensity *= smoothstep(0.02, 0.15, viewAngle);
    float y = abs(cloudPos.y - planetCenter.y);
    float desertBand = smoothstep(0000.0, 2000.0, y) * (1.0 - smoothstep(2000.0, 3000.0, y));
    cloudDensity *= mix(1.0, 0.01, desertBand);
    float alpha = 1.0 - exp(-cloudDensity * pathLength * densityScale);
    float sunDot = dot(normPos, sunDir);
    vec3 dayCloud = mix(vec3(0.45, 0.50, 0.60), vec3(1.0), nTop);
    vec3 nightCloud = mix(vec3(0.05, 0.08, 0.12), vec3(0.15, 0.2, 0.3), nTop);
    vec3 sunsetCloud = mix(vec3(0.4, 0.1, 0.1), vec3(1.0, 0.6, 0.2), nTop);
    vec3 cloudBase = mix(nightCloud, dayCloud, smoothstep(-0.15, 0.15, sunDot));
    float sunsetBand = smoothstep(0.5, 0.0, abs(sunDot - 0.45)); vec3 cloudColor = mix(cloudBase, sunsetCloud, sunsetBand * 0.9);
    return cloudColor * alpha;
}

vec3 getCloudContribution(vec3 ro, vec3 rd, float rayT, float radius, vec3 windAxis, float windSpeed, float noiseScale, vec2 coverageThreshold, float shellThickness, float densityScale, vec3 sunDir) {
    vec2 intersect = raySphere(ro, rd, planetCenter, radius); if (intersect.y <= 0.0) return vec3(0.0);
    vec3 layerColor = vec3(0.0); float t_near = intersect.x; float t_far  = intersect.y;
    if (t_near > 0.0 && t_near < rayT) layerColor += sampleCloudAtDepth(ro, rd, t_near, windAxis, windSpeed, noiseScale, coverageThreshold, shellThickness, densityScale, sunDir);
    if (t_far > 0.0 && t_far < rayT) layerColor += sampleCloudAtDepth(ro, rd, t_far, windAxis, windSpeed, noiseScale, coverageThreshold, shellThickness, densityScale, sunDir);
    return layerColor;
}

void main() {
    vec2 p = (TexCoords * 2.0 - 1.0) * vec2(resolution.x / resolution.y, 1.0);

    vec3 ro = vec3(0.0);
    vec3 rd = normalize(p.x * camRight + p.y * camUp + 1.5 * camForward);
    
    // TRACK THE UNBENT RAY FOR OPTICAL DEPTH
    vec3 orig_ro = ro;
    vec3 orig_rd = rd;
    
    vec3 sunDir = getSunDir();

    bool skipAtmosphere = false;
    float t = 0.0; vec3 col = vec3(0.00);

    const float MIN_EPS = 0.20;
    const float PIXEL_SCALE = 0.003;
    const float STEP_SCALE = 0.97; 
    const float MIN_STEP = 0.015;

    
    // Recalculate radius using the ray's final origin after all portal bends
    float camRadius = length(ro - planetCenter);

    bool isUnderground = camRadius < 5800.0;
    float maxRayDist = 100000.0;

    int iterCap = min(maxIterations, MAX_RAYMARCH_ITERATIONS_CAP);
    int iterStop = 0;

    // 1. Calculate bubble-entry distances FIRST
    float tPortal[MAX_PORTALS];
    float closestPortalDist = maxRayDist;
    for (int k = 0; k < uPortalCount; k++) {
        PortalGPU pk = uPortals[k];
        float th = (pk.meta.y != 0) ? portalSphereHit(ro, rd, pk.posRadius.xyz, pk.posRadius.w) : -1.0;
        
        // t is still 0.0 here, perfectly protecting valid portals in front of the camera
        tPortal[k] = (th <= t) ? -10.0 : th; 
        
        if (th > 0.0 && th < closestPortalDist) {
            closestPortalDist = th;
        }
    }

    // 2. Safely apply the bounding sphere optimization
    if (!isUnderground) {
        vec2 bounds = raySphere(ro, rd, planetCenter, 6300.0);
        if (bounds.y > 0.0 && bounds.x > 0.0) {
            // Fast-forward the ray, but do not overshoot the portal boundary
            t = min(bounds.x, closestPortalDist);
        } else if (closestPortalDist < maxRayDist) {
            // Bonus optimization: If we miss the planet but hit a portal in space, 
            // jump straight to the portal!
            t = closestPortalDist;
        }
    }
    // Tracks the portal the ray most recently bent through, and how many
    // times it's bent so far — written to PortalMeta for the object pass.
    int lastPortalId = -1;
    int bounceCount = 0;
    float firstPortalT = -1.0; // TRACK PORTAL MOUTH DISTANCE

    for(int i = 0; i < iterCap; i++) {
        iterStop = i;
        vec3 pos = ro + rd * t;

        // ==========================================
        // 1. NON-EUCLIDEAN PORTAL WARP (generalized to N portals)
        // ==========================================
        int bentPortal = -1;
        float bentT = -1.0;
        for (int k = 0; k < uPortalCount; k++) {
            if (tPortal[k] > 0.0 && t >= tPortal[k]) {
                bentPortal = k;
                bentT = tPortal[k];
                break;
            }
        }

        if (bentPortal >= 0) {
            // --- UNIFIED PORTAL PAIR OUTLINE (Works for primary & nested portals) ---
            vec3 outlineColor = getPortalPairColor(bentPortal);
            vec3 hitPos = ro + rd * bentT;
            vec3 portalCenter = uPortals[bentPortal].posRadius.xyz;

            // Check if the ray hits near the silhouette/rim of the portal sphere
            if (dot(normalize(hitPos - portalCenter), -rd) < 0.3) {
                col = outlineColor;
                break;
            }

            if (bounceCount == 0) {
                firstPortalT = bentT; // SAVE ENTRANCE DISTANCE
            }

            portalBendRay(bentPortal, bentT, ro, rd, t);
            lastPortalId = bentPortal;
            bounceCount++;

            for (int k = 0; k < uPortalCount; k++) {
                PortalGPU pk = uPortals[k];
                float th = (pk.meta.y != 0) ? portalSphereHit(ro, rd, pk.posRadius.xyz, pk.posRadius.w) : -1.0;
                tPortal[k] = (th <= t) ? -10.0 : th;
            }
            continue;
        }

        // ==========================================
        // 2. SCENE & GRASS LOGIC
        // ==========================================
        float d = mapSceneDistFast(pos);
        float eps = max(MIN_EPS, t * PIXEL_SCALE);

        //bool insideAnyPortal = false;
        //for (int k = 0; k < uPortalCount; k++) {
        //    if (uPortals[k].meta.y != 0) {
        //        float distToCenter = length(pos - uPortals[k].posRadius.xyz);
        //        if (distToCenter < uPortals[k].posRadius.w) { // inside bubble radius
        //            insideAnyPortal = true;
        //            break;
        //        }
        //    }
        //}

        bool hitGrassBlade = false;
        vec3 targetPos = pos;
        float currentBladeVariation = 1.0;

        const float MAX_GRASS_HEIGHT = 2.0;
        const float GRASS_MIN_START = 0.05;
        const float GRASS_MAX_END = 0.5;

        if (i == iterCap - 1) {
            targetPos = pos;
        }

        float h = 0.0;
        
        if ((d < MAX_GRASS_HEIGHT && d >= eps && t < 300.0 && !isUnderground)) {
            vec3 normPos = normalize(pos - planetCenter);
            vec3 groundPos = pos - normPos * d; 
            
            SDFResult groundRes = mapScene(groundPos);
            float grassWeight = groundRes.mat[MAT_GRASS];
            
            if (grassWeight > GRASS_MIN_START) {
                vec3 pRoot = groundPos - planetCenter;
                float heightNoise = fbm(pRoot * 0.3);
                float randomizedMaxHeight = mix(MAX_GRASS_HEIGHT * 0.5, MAX_GRASS_HEIGHT, heightNoise);

                float localHeight = smoothstep(0.0, GRASS_MAX_END, grassWeight) * randomizedMaxHeight;
                h = d / localHeight;

                if (h <= 1.0) { 
                    vec3 wind = getWindOffset(groundPos - planetCenter, time);
                    vec3 rootPos = groundPos - wind * h * 0.25;
                    vec3 pRoot = rootPos - planetCenter;
                    
                    float densityNoise = fbm(pRoot * 0.7 + vec3(17.2, 5.4, 31.1));
                    float densityFactor = mix(0.2, 1.2, densityNoise);

                    float pattern = sin(pRoot.x * 35.0) * sin(pRoot.y * 35.0) * sin(pRoot.z * 35.0);
                    pattern = (pattern * 0.5 + 0.5) * densityFactor;

                    if (pattern > h) {
                        hitGrassBlade = true;
                        
                        vec3 bladeCell = floor(pRoot * 35.0);
                        float randomHash = fract(sin(dot(bladeCell, vec3(12.9898, 78.233, 45.164))) * 2.0);
                        
                        currentBladeVariation = mix(0.8, 1.2, randomHash); 
                        
                        targetPos = groundPos; 
                    }
                }
            }
        }

        if (d < eps || hitGrassBlade || i == iterCap - 1) {
            SDFResult res = mapScene(targetPos);
            if (res.dist < -eps && !hitGrassBlade) {
                t += res.dist; pos = ro + rd * t; targetPos = pos; res = mapScene(targetPos); 
            }

            for(int m = 0; m < 12; m++) {
                if (res.mat[m] > 0.001 && (m == MAT_MOONROCK || m == MAT_SUNMAGMA || m == MAT_GLOWING_CRYSTAL)) {
                    skipAtmosphere = true; break;
                }
            }

            eps = max(0.05, t * 0.05);
            vec3 n = calcNormal(targetPos, eps);
            vec3 blendedColor = vec3(0.0); vec3 blendedNormal = vec3(0.0); vec3 emissive = vec3(0.0);

            float grassDetailFactor = smoothstep(50.0, 0.0, t) * clamp(1.0- dot(n,-rd), 0.0, 1.0);

            float totalSolidWeight = 0.0;
            for(int m = 0; m < 12; m++) {
                if (res.mat[m] > 0.001) {
                    float texVal; vec3 bumpNormal;
                    if (res.mat[m] > 0.03) {
                        bumpNormal = applyTriplanarBump(targetPos - planetCenter, n, t, float(m), texVal);
                    } else {
                        texVal = getTriplanarTexture(targetPos - planetCenter, n, float(m)); bumpNormal = n;
                    }

                    blendedNormal += bumpNormal * res.mat[m];
                    vec3 baseCol = getMaterialBaseColor(m);

                    if (hitGrassBlade && m == MAT_GRASS) {
                        baseCol *= currentBladeVariation;
                        baseCol += vec3(1.0,1.0,1.0) * h *  grassDetailFactor;
                    }

                    blendedColor += (baseCol * texVal) * res.mat[m];
                    if (m == MAT_GLOWING_CRYSTAL || m == MAT_MAGMA || m == MAT_SUNMAGMA) { 
                        emissive += baseCol * res.mat[m] * 2.5; 
                    }
                    totalSolidWeight += res.mat[m];
                }
            }
            if (length(blendedNormal) > 0.1) n = normalize(blendedNormal);

            vec3 liquidColor = vec3(0.0); float totalLiquidWeight = 0.0;
            for(int m = 12; m < 16; m++) {
                if (res.mat[m] > 0.001) {
                    float weight = res.mat[m];
                    // Multiply by weight in the numerator so it scales down properly at edges
                    liquidColor += calculateLiquid(weight, m, targetPos, n, rd, sunDir, t) * weight;
                    totalLiquidWeight += weight;
                }
            }

            if (totalLiquidWeight > 0.001) {
                // Divide by total weight to normalize the blend cleanly without artificial spikes
                col = liquidColor / totalLiquidWeight;
            }

            float distToCenter = length(targetPos - planetCenter);
            float distFromPole = length(targetPos.xz - planetCenter.xz);
            float isCave = 0.0; 

            vec3 finalLitSolid = vec3(0.0);

            if (isCave > 0.5) {
                vec3 toPlayer = ro - targetPos; float distToPlayer = length(toPlayer);
                vec3 pointLightDir = toPlayer / distToPlayer;
                float diff = max(dot(n, pointLightDir), 0.0);
                float attenuation = 1.0 / (1.0 + 0.005 * distToPlayer * distToPlayer);
                finalLitSolid = blendedColor * (diff * attenuation * 2.0 + 0.05) + emissive;
            } else {
                vec3 normPos = normalize(targetPos - planetCenter);
                float sunFacing = clamp(dot(normPos, sunDir) * 5.0 + 0.5, 0.0, 1.0);
                float moonFacing = clamp(dot(normPos, -sunDir) * 5.0 + 0.5, 0.0, 1.0);
                float diffSun = sunFacing * clamp(dot(n, sunDir) * 3.0, 0.0, 1.0);
                float diffMoon = moonFacing * clamp(dot(n, -sunDir) * 3.0, 0.0, 1.0);
                vec3 skyColor = getSkyAmbientColor(normPos, sunDir);
                float hemisphere = clamp(dot(n, normPos) * 0.5 + 0.5, 0.1, 1.0);
                vec3 ambientSky = skyColor * hemisphere * 0.35;
                float fresnel = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
                vec3 skyReflection = skyColor * fresnel * 0.25;

                finalLitSolid = blendedColor * (diffSun * vec3(1.1, 1.05, 0.9) + diffMoon * vec3(0.30, 0.5, 0.8) + ambientSky) + skyReflection + emissive + vec3(0.1);
            }

            if (totalLiquidWeight > 0.1) {
                col = liquidColor / max(totalLiquidWeight, 0.1);

                float edge = smoothstep(0.5, 0.3, totalLiquidWeight) * smoothstep(0.7, 0.5, col.x * col.y * col.z);

                col = mix(col,finalLitSolid, edge);

                //float liquidFactor = smoothstep(0.0, 0.5, totalLiquidWeight);
                //vec3 normalizedLiquid = liquidColor / max(totalLiquidWeight, 0.1);
                //
                //vec3 wetSolid = finalLitSolid * mix(1.0, 0.0, liquidFactor);
                //
                //float foamNoise = fbm((targetPos - planetCenter) * 1.5 + vec3(time * 0.2));
                //edgeFoam *= smoothstep(0.3, 0.7, foamNoise);
                //vec3 foamCol = vec3(1.0);
                //vec3 mixedSurface = min(finalLitSolid + normalizedLiquid,vec3(1.0));
                //mixedSurface = min(mixedSurface + foamCol * edgeFoam, vec3(1.0));
//
                //col = mix(mixedSurface, liquidColor / max(totalLiquidWeight, 0.001), liquidFactor);
            } else {
                col = finalLitSolid;
            }
            break;
        }

        if (t > maxRayDist) { t = maxRayDist; break; }

        float stepDist = (d < 0.6) ? min(d * 0.5,   0.15) : max(d * STEP_SCALE, MIN_STEP);
        t += max(stepDist, 0.05);
    }

    vec3 atmosAdded = vec3(0.0);
    if(!skipAtmosphere) {
        
        // --- 1. DESTINATION ATMOSPHERE (After Portal) ---
        vec2 atmosIntersect = raySphere(ro, rd, planetCenter, 6500.0);
        float atmosphereAccum = 0.0; float t_enter = 0.0;
        
        if (atmosIntersect.y > 0.0) {
            t_enter = max(0.0, atmosIntersect.x);
            
            // FIX: Prevent destination fog from leaking into the pre-portal space
            if (bounceCount > 0) t_enter = max(t_enter, firstPortalT); 
            
            float t_exit = min(t, atmosIntersect.y);
            if (t_exit > t_enter) atmosphereAccum = t_exit - t_enter;
        }

        vec3 atmosPoint = ro + rd * (t_enter + atmosphereAccum * 0.5);
        vec3 atmosNormal = normalize(atmosPoint - planetCenter);
        float atmosSunDot = dot(atmosNormal, sunDir);

        vec3 dayColor = vec3(0.3, 0.65, 1.0); 
        vec3 nightColor = vec3(0.04, 0.1, 0.3); 
        vec3 sunsetColor = vec3(0.95, 0.45, 0.15);
        
        vec3 surfaceAtmosphere = mix(nightColor, dayColor, smoothstep(0.45, 0.50, atmosSunDot));
        float finalCamRadius = length(ro - planetCenter);
        vec3 caveAtmosphere =  mix(vec3(0.8, 0.2, 0.0), vec3(0.1, 0.1, 0.015), smoothstep(1500.0, 6000.0, finalCamRadius));
        vec3 baseAtmos =  mix(surfaceAtmosphere, caveAtmosphere, smoothstep(5990.0, 5800.0, finalCamRadius));
        
        float sunsetBand = smoothstep(0.45, 0.0, abs(atmosSunDot - 0.45));
        vec3 targetAtmosColor = mix(baseAtmos, sunsetColor, sunsetBand * 0.85);
        
        float sunViewGlow = pow(max(dot(rd, sunDir), 0.0), 12.0); 
        float moonViewGlow = pow(max(dot(rd, -sunDir), 0.0), 24.0);
        targetAtmosColor += vec3(1.0, 0.9, 0.5) * sunViewGlow * smoothstep(-0.2, 0.2, atmosSunDot) * 0.7;
        targetAtmosColor += vec3(0.4, 0.6, 1.0) * moonViewGlow * smoothstep(0.2, -0.2, atmosSunDot) * 0.3;

        float atmosFactor = 1.0 - exp(-atmosphereAccum * 0.0025);
        atmosAdded = targetAtmosColor * atmosFactor;
        col = col * (1.0 - atmosFactor) + atmosAdded;

        // --- 2. SOURCE ATMOSPHERE (Before Portal) ---
        if (bounceCount > 0) {
            vec2 srcIntersect = raySphere(orig_ro, orig_rd, planetCenter, 6500.0);
            float srcAccum = 0.0; float srcEnter = 0.0;
            
            if (srcIntersect.y > 0.0) {
                srcEnter = max(0.0, srcIntersect.x);
                float srcExit = min(firstPortalT, srcIntersect.y);
                if (srcExit > srcEnter) srcAccum = srcExit - srcEnter;
            }
            
            if (srcAccum > 0.0) {
                vec3 srcPoint = orig_ro + orig_rd * (srcEnter + srcAccum * 0.5);
                vec3 srcNormal = normalize(srcPoint - planetCenter);
                float srcSunDot = dot(srcNormal, sunDir);
                
                vec3 srcSurfaceAtmos = mix(nightColor, dayColor, smoothstep(-0.15, 0.15, srcSunDot));
                float origCamRadius = length(orig_ro - planetCenter);
                vec3 srcCaveAtmos = mix(vec3(0.8, 0.2, 0.0), vec3(0.1, 0.1, 0.015), smoothstep(1500.0, 6000.0, origCamRadius));
                vec3 srcBaseAtmos = mix(srcSurfaceAtmos, srcCaveAtmos, smoothstep(5990.0, 5800.0, origCamRadius));
                
                float srcSunsetBand = smoothstep(0.45, 0.0, abs(srcSunDot - 0.45));
                vec3 srcTargetColor = mix(srcBaseAtmos, sunsetColor, srcSunsetBand * 0.85);
                
                float srcSunGlow = pow(max(dot(orig_rd, sunDir), 0.0), 12.0);
                float srcMoonGlow = pow(max(dot(orig_rd, -sunDir), 0.0), 24.0);
                srcTargetColor += vec3(1.0, 0.9, 0.5) * srcSunGlow * smoothstep(-0.2, 0.2, srcSunDot) * 0.7;
                srcTargetColor += vec3(0.4, 0.6, 1.0) * srcMoonGlow * smoothstep(0.2, -0.2, srcSunDot) * 0.3;
                
                float srcFactor = 1.0 - exp(-srcAccum * 0.0025);
                
                // Blend the source fog over the destination view
                col = col * (1.0 - srcFactor) + srcTargetColor * srcFactor; 
            }
        }
    }

    vec3 clouds = vec3(0.0);
    vec2 hit1 = raySphere(ro, rd, planetCenter, 6100.0); vec2 hit2 = raySphere(ro, rd, planetCenter, 6250.0);
    float camDist = length(ro - planetCenter);
    float t1 = (camDist < 6100.0) ? hit1.y : hit1.x; float t2 = (camDist < 6250.0) ? hit2.y : hit2.x;
    vec3 axis1 = vec3(0.0, 1.0, 0.0); float speed1 = 0.001; float scale1 = 0.008; vec2 coverage1 = vec2(0.42, 0.70); float thickness1 = 90.0; float density1 = 0.012;
    vec3 axis2 = vec3(-0.6, 0.0, -0.3); float speed2 = 0.002; float scale2 = 0.002; vec2 coverage2 = vec2(0.45, 0.62); float thickness2 = 100.0; float density2 = 0.006;

    if (t1 < t2) {
        clouds += getCloudContribution(ro, rd, t, 6100.0, axis1, speed1, scale1, coverage1, thickness1, density1, sunDir);
        clouds += getCloudContribution(ro, rd, t, 6250.0, axis2, speed2, scale2, coverage2, thickness2, density2, sunDir);
    } else {
        clouds += getCloudContribution(ro, rd, t, 6250.0, axis2, speed2, scale2, coverage2, thickness2, density2, sunDir);
        clouds += getCloudContribution(ro, rd, t, 6100.0, axis1, speed1, scale1, coverage1, thickness1, density1, sunDir);
    }

    col = clamp(col + clouds, 0.0, 1.0);

    if (t >= maxRayDist) {
        gl_FragDepth = 1.0;
    } else {
        // USE THE UNBENT RAY TO CALCULATE CORRECT VIRTUAL/OPTICAL DEPTH
        vec4 clipSpace = proj * view * vec4(orig_ro + orig_rd * t, 1.0);
        gl_FragDepth = (clipSpace.z / clipSpace.w) * 0.5 + 0.5;
    }
    
    // Calculate the exact distance to the first cloud layer based on your existing hits
    float cloudStartDist = (t1 < t2) ? t1 : t2;
    if (cloudStartDist <= 0.0) cloudStartDist = maxRayDist;

    FragColor = vec4(col, 1.0);
    
    // Output ONLY the clouds, and the distance where they begin
    VolumetricTex = vec4(clouds, cloudStartDist);

    // +1 so 0 unambiguously means "no portal was crossed" for this pixel.
    PortalMeta = vec4(float(lastPortalId + 1), float(bounceCount), firstPortalT, 0.0);
}