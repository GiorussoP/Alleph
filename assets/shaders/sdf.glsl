// sdf.glsl [source: 4]
//
// Shared SDF definition for both GLSL rendering and CPU physics.
// Includes the volumetric 16-material composition array with animated ocean waves.

#ifndef SDF_GLSL
#define SDF_GLSL

#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include "glsl_compat.h"

inline float clamp(float x, float minVal, float maxVal) {
    return std::max(minVal, std::min(maxVal, x));
}

inline float smoothstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

extern vec3 planetCenter;
#else
uniform vec3 planetCenter;
uniform samplerCube macroElevationMap;
uniform float time;
#endif

// --- 16 MATERIAL VOLUMETRIC IDs ---
// Solids
#define MAT_STONE 0
#define MAT_SAND 1
#define MAT_GRASS 2
#define MAT_DIRT 3
#define MAT_SNOW 4
#define MAT_SANDSTONE 5
#define MAT_ICE 6
#define MAT_GLOWING_CRYSTAL 7
#define MAT_MAGMA 8
#define MAT_MOONROCK 9
#define MAT_SUNMAGMA 10
#define MAT_MINERAL 11
// Liquids
#define MAT_WATER 12
#define MAT_LAVA 13
#define MAT_MUD 14
#define MAT_ACID 15

// SDF Result struct carrying the 16-blend array
struct SDFResult {
    float dist;
    float mat[16];
};

SDFResult makeSDF(float d, int matID) {
    SDFResult res;
    res.dist = d;
    for(int i = 0; i < 16; i++) {
        res.mat[i] = 0.0;
    }
    if (matID >= 0 && matID < 16) {
        res.mat[matID] = 1.0;
    }
    return res;
}

// Smooth union that perfectly blends material weights where SDFs intersect
SDFResult sminSDF(SDFResult a, SDFResult b, float k) {
    float h = clamp(0.5 + 0.5 * (b.dist - a.dist) / k, 0.0, 1.0);
    SDFResult res;
    res.dist = mix(b.dist, a.dist, h) - k * h * (1.0 - h);
    for(int i = 0; i < 16; i++) {
        res.mat[i] = mix(b.mat[i], a.mat[i], h);
    }
    return res;
}

// Sharp union
SDFResult minSDF(SDFResult a, SDFResult b) {
    return (a.dist < b.dist) ? a : b;
}

vec3 rotateQuat(vec3 v, vec4 q) {
    vec3 qv = vec3(q.x, q.y, q.z);
    vec3 t = 2.0 * cross(qv, v);
    return v + q.w * t + cross(qv, t);
}

#ifndef __cplusplus
float sdSphere(vec3 p, float s) { return length(p) - s; }
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float sunSDF(vec3 p) { return length(p) - 600.0; }
float moonSDF(vec3 p) { return length(p) - 400.0; }
#endif

float hash(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    vec3 pyxz = vec3(p.y, p.x, p.z);
    p += dot(p, pyxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix( hash(i+vec3(0,0,0)), hash(i+vec3(1,0,0)),f.x),
                   mix( hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix( hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)),f.x),
                   mix( hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}

float ridgedNoise(vec3 p) { return 1.0 - abs(noise(p) * 2.0f - 1.0f); }

float gyroidVal(vec3 p) {
    #ifdef __cplusplus
    vec3 s = vec3(sin(p.x), sin(p.y), sin(p.z));
    vec3 c = vec3(cos(p.x), cos(p.y), cos(p.z));
    #else
    vec3 s = sin(p);
    vec3 c = cos(p);
    #endif
    return s.x * c.y + s.y * c.z + s.z * c.x;
}


float fbm(vec3 p) {
    float f = 0.0; float amp = 0.5;
    for(int i = 0; i < 4; i++) { f += amp * noise(p); p *= 2.0; amp *= 0.5; }
    return f;
}

#ifndef __cplusplus
vec4 permute(vec4 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
#endif

// --- OCEAN WAVE DISPLACEMENT HELPER ---
float getWaterDisplacement(vec3 surfacePos,float time) {

    float t = time * 0.3;


    vec3 pNorm = normalize(surfacePos);
    float w1 = sin(dot(pNorm, vec3(1.0, 0.0, 1.0)) * 600.0 + t * 3.0);
    float w2 = cos(dot(pNorm, vec3(0.0, 1.0, -1.0)) * 400.0 - t * 2.0);
    float n = noise(surfacePos * 0.03 + vec3(t * 0.02, t * 0.01, t * 0.02));
    return (w1 * 1.5 + w2 * 1.0 + (n - 0.5) * 5.0);
}

// Helper: Raw distance to the water surface only
float mapWaterDistance(vec3 p, float time) {
    vec3 surfacePos = p - planetCenter;
    float distToCenter = length(surfacePos);
    float waterRadius = 5995.0 + getWaterDisplacement(surfacePos, time);
    return distToCenter - waterRadius;
}

// Dedicated normal function purely for water waves, free of terrain blending artifacts
vec3 calcWaterNormal(vec3 p, float time) {
    const float h = 0.01;
    const vec3 kxyy = vec3( 1.0, -1.0, -1.0);
    const vec3 kyyx = vec3(-1.0, -1.0,  1.0);
    const vec3 kyxy = vec3(-1.0,  1.0, -1.0);
    const vec3 kxxx = vec3( 1.0,  1.0,  1.0);
    return normalize(
        kxyy * mapWaterDistance(p + kxyy * h, time) +
        kyyx * mapWaterDistance(p + kyyx * h, time) +
        kyxy * mapWaterDistance(p + kyxy * h, time) +
        kxxx * mapWaterDistance(p + kxxx * h, time)
    );
}

// (Retained for the GPU baking compute shader)
vec3 getMacroData(vec3 surfacePos) {
    vec3 spherePos = normalize(surfacePos) * 6000.0;
    float absY = abs(spherePos.y);
    float biomeY = absY + (noise(spherePos * 0.001) * 600.0 - 300.0);
    float continentNoise = noise(spherePos * 0.0012);
    float continentElevation = (continentNoise * 400.0) - 270.0;
    float taigaBoost = smoothstep(2500.0, 3500.0, biomeY) * (1.0 - smoothstep(4500.0, 5200.0, biomeY));
    continentElevation += taigaBoost * 60.0;
    float nRound = noise(spherePos * 0.025) * 80.0;
    float nSharp = ridgedNoise(spherePos * 0.0025) * 130.0;
    float terrainMacro = mix(nRound, nSharp, smoothstep(800.0, 1400.0, biomeY));
    float polarFunnel = smoothstep(4800.0, 5800.0, biomeY) * 100.0;
    float iceRidges = ridgedNoise(spherePos * 0.004) * 150.0;
    float polarMacro = polarFunnel + (iceRidges * smoothstep(4500.0, 5000.0, biomeY) * (1.0 - smoothstep(5200.0, 5800.0, biomeY)));
    terrainMacro = mix(terrainMacro, polarMacro, smoothstep(4500.0, 5200.0, biomeY));
    float nDetail = fbm(spherePos * 0.0120) * 15.0;
    float baseElevation = continentElevation + terrainMacro + nDetail;
    float archWeight = smoothstep(0.2, 0.8, nSharp / 130.0) * (1.0 - smoothstep(4500.0, 5000.0, biomeY));
    return vec3(baseElevation, biomeY, archWeight);
}

// --- UNIVERSAL MACRO DATA SAMPLER ---
#ifdef __cplusplus
inline vec3 sampleCubemapBilinear(vec3 dir) {
    vec3 a = abs(dir);
    int face = 0;
    float sc = 0, tc = 0, ma = 0;
    if (a.x >= a.y && a.x >= a.z) {
        face = dir.x > 0.0f ? 0 : 1;
        sc = dir.x > 0.0f ? -dir.z : dir.z;
        tc = -dir.y;
        ma = a.x;
    } else if (a.y >= a.x && a.y >= a.z) {
        face = dir.y > 0.0f ? 2 : 3;
        sc = dir.x;
        tc = dir.y > 0.0f ? dir.z : -dir.z;
        ma = a.y;
    } else {
        face = dir.z > 0.0f ? 4 : 5;
        sc = dir.z > 0.0f ? dir.x : -dir.x;
        tc = -dir.y;
        ma = a.z;
    }
    float u = (sc / ma) * 0.5f + 0.5f;
    float v = (tc / ma) * 0.5f + 0.5f;

    float x = u * (float)cpuCubeRes - 0.5f;
    float y = v * (float)cpuCubeRes - 0.5f;
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;

    auto getPix = [&](int px, int py) {
        if (cpuMacroData.empty()) return vec3(0.0f, 0.0f, 0.0f);
        px = std::max(0, std::min(px, cpuCubeRes - 1));
        py = std::max(0, std::min(py, cpuCubeRes - 1));
        int idx = (face * cpuCubeRes * cpuCubeRes + py * cpuCubeRes + px) * 4;
        return vec3(cpuMacroData[idx], cpuMacroData[idx+1], cpuMacroData[idx+2]);
    };

    vec3 p00 = getPix(ix, iy);
    vec3 p10 = getPix(ix+1, iy);
    vec3 p01 = getPix(ix, iy+1);
    vec3 p11 = getPix(ix+1, iy+1);

    return mix(mix(p00, p10, fx), mix(p01, p11, fx), fy);
}

inline vec3 sampleMacroDataSmooth(vec3 surfacePos) {
    vec3 dir = normalize(surfacePos);
    vec3 absDir = abs(dir);
    float maxAxis = max(absDir.x, max(absDir.y, absDir.z));
    vec3 faceUV = (dir / maxAxis) * 0.5f + vec3(0.5f, 0.5f, 0.5f);
    vec3 texelCoords = (faceUV * (float)cpuCubeRes) - vec3(0.5f, 0.5f, 0.5f);

    vec3 i = floor(texelCoords);
    vec3 f = fract(texelCoords);

    f = f * f * f * (f * (f * 6.0f - vec3(15.0f, 15.0f, 15.0f)) + vec3(10.0f, 10.0f, 10.0f));

    vec3 smoothUV = (i + f + 0.5f) / (float)cpuCubeRes;
    vec3 smoothDir = (smoothUV * 2.0f - 1.0f) * maxAxis;

    float strength = 0.1f; 
    vec3 finalDir = mix(dir, normalize(smoothDir), strength);

    return sampleCubemapBilinear(finalDir);
}
#else
vec3 sampleMacroDataSmooth(vec3 surfacePos) {
    vec3 dir = normalize(surfacePos);
    
    const float CUBEMAP_RES = 2048.0; 
    
    vec3 absDir = abs(dir);
    float maxAxis = max(absDir.x, max(absDir.y, absDir.z));
    
    vec3 faceUV = (dir / maxAxis) * vec3(0.5) + vec3(0.5);
    vec3 texelCoords = (faceUV * CUBEMAP_RES) - vec3(0.5);
    
    vec3 i = floor(texelCoords);
    vec3 f = fract(texelCoords);
    
    f = f * f * f * (f * (f * vec3(6.0) - vec3(15.0)) + vec3(10.0));
    
    vec3 smoothUV = (i + f + vec3(0.5)) / CUBEMAP_RES;
    vec3 smoothDir = (smoothUV * vec3(2.0) - vec3(1.0)) * maxAxis;
    
    float strength = 0.1; 
    vec3 finalDir = mix(dir, normalize(smoothDir), strength);
    
    return textureLod(macroElevationMap, finalDir, 0.0).rgb;
}
#endif

// Helper: Evaluates raw land distance geometry
float mapPlanetDistance(vec3 p) {
    vec3 surfacePos = p - planetCenter;
    float distToCenter = length(surfacePos);

    vec3 macroData = sampleMacroDataSmooth(surfacePos);

    float baseElevation = macroData.x;
    float archWeight    = macroData.z;

    vec3 archPos = surfacePos * 0.018;
    float archVal = gyroidVal(archPos);
    float archEffect = (archVal * 40.0) * archWeight;

    float terrainRadius = 5980.0 + baseElevation + archEffect;
    float dTerrainRaw = distToCenter - terrainRadius;
    float dTerrain = dTerrainRaw * 0.50;

    float depthFactor = clamp((6000.0 - distToCenter) / 6000.0, 0.0, 1.0);
    float caveScale = mix(0.006, 0.003, depthFactor);
    float caveWall  = mix(0.18,  0.42,  depthFactor);

    vec3 cavePos = surfacePos * caveScale;
    float gVal = gyroidVal(cavePos);
    float dCave = (abs(gVal) - caveWall) / (caveScale * 1.7320508);
    

    float dLand = max(dTerrain, -dCave);
    float dCoreHollow = 900.0 - distToCenter;
    return max(dLand, dCoreHollow);
}

// Helper: Calculates land surface normal to determine upward pointing slope
vec3 calcLandNormal(vec3 p) {
    const float h = 12.0; 
    const vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * mapPlanetDistance(p + k.xyy * h) +
        k.yyx * mapPlanetDistance(p + k.yyx * h) +
        k.yxy * mapPlanetDistance(p + k.yxy * h) +
        k.xxx * mapPlanetDistance(p + k.xxx * h)
    );
}

// ---------------------------------------------------------------------------
// PROCEDURAL PONDS + WATERFALLS
//
// Scatters small biome-appropriate wet patches across the terrain using a
// coarse 3D cell hash (deterministic, no per-frame state). Ponds carry NO
// geometry of their own - they're a pure material paint applied on top of
// whatever the real terrain surface already is, exactly like water.gdshader
// never displaces the mesh, only recolors/normal-maps it. That keeps them
// perfectly flush with the ground (and matches physics, which only ever
// collides with the real terrain). render.frag's calculateLiquid() renders
// any non-flat liquid surface with the falling/waterfall look automatically,
// so this only has to say WHERE liquid exists; how it looks is entirely up
// to the local terrain slope.
//
// This is a cheap approximation, not a flow simulation: each grid cell
// independently decides (via hash) whether it holds a pond, and reaches
// out in one random direction as its "downhill corridor." Tune the
// #defines below to taste.
// ---------------------------------------------------------------------------

vec3 hash3(vec3 p) {
    return vec3(
        hash(p),
        hash(p + vec3(41.7, 12.9, 71.3)),
        hash(p + vec3(93.1, 57.2, 29.8))
    );
}

#define POND_CELL_SIZE     900.0  // average spacing between pond cells (world units)
#define POND_DENSITY       0.85   // fraction of cells that actually contain a pond
#define POND_MIN_RADIUS    40.0
#define POND_MAX_RADIUS    220.0
#define POND_ICE_LATITUDE  5700.0 // above this biomeY, ponds don't spawn (it's already ice up there)
#define SPILL_LENGTH       260.0  // how far the downhill corridor reaches past the rim
#define SPILL_WIDTH_TOP    55.0
#define SPILL_WIDTH_BOTTOM 22.0
#define POND_EDGE_SOFTNESS 5.0    // how wide the material blend is at the pond/corridor edge

struct PondData {
    float maskDist; // signed distance to the pond+corridor footprint (tangential, NOT geometry). <0 = inside.
    int   matID;    
};

// Ponds carry NO geometry of their own - they never touch the distance field,
// only which material shows on the terrain that's already there. That's what
// keeps them perfectly flush: physics and the raymarcher only ever see the
// real dLand surface, exactly like a texture/normal-map never displaces a mesh.
PondData getPondData(vec3 p) {
    PondData pd;
    pd.maskDist = 1e5;
    pd.matID = MAT_WATER;

    vec3 surfacePos = p - planetCenter;
    float distToCenter = length(surfacePos);
    vec3 shellPos = normalize(surfacePos) * 6000.0;

    vec3 cellCoord = floor(shellPos / POND_CELL_SIZE);

    // Cheap early-out: most cells contain nothing, so most calls
    // pay for one hash() and nothing else.
    if (hash(cellCoord) > POND_DENSITY) return pd;

    vec3 h3 = hash3(cellCoord);
    vec3 pondAnchor = (cellCoord + 0.5) * POND_CELL_SIZE + (h3 - 0.5) * POND_CELL_SIZE * 0.6;
    vec3 pondDir = normalize(pondAnchor);

    vec3 pondSurfacePos = pondDir * 6000.0;
    vec3 macroPond = sampleMacroDataSmooth(pondSurfacePos);
    float biomeYPond = macroPond.y;
    if (biomeYPond > POND_ICE_LATITUDE) return pd; // stay off the ice caps
    if (macroPond.x < 20.0) return pd;             // stay above global sea level (~15 units of elevation)

    float pondRadius = mix(POND_MIN_RADIUS, POND_MAX_RADIUS, h3.x);

    // --- Biome-driven liquid choice, mirrors mapPlanet()'s land biome logic ---
    float temp = 1.0 - clamp(biomeYPond / 5500.0, 0.0, 1.0);
    float hum  = fbm(pondSurfacePos * 0.005) * 0.5 + 0.5;
    float mineralChance = smoothstep(0.65, 0.9, noise(pondSurfacePos * 0.05));

    int matID = MAT_WATER;
    if (temp > 0.75 && hum < 0.35 && macroPond.x < 250.0)      matID = MAT_LAVA;  // hot & dry: volcanic vent
    else if (hum > 0.65 && temp > 0.25 && temp < 0.75)         matID = MAT_MUD;   // humid & temperate: swamp
    else if (mineralChance > 0.6)                              matID = MAT_ACID;  // mineral-rich ground
    pd.matID = matID;

    // Position of p relative to the pond, projected onto p's own radius so
    // the footprint stays correct regardless of local terrain height.
    vec3 pondCenterAtR = pondDir * distToCenter;
    vec3 toP = surfacePos - pondCenterAtR;
    float footprintDist = length(toP);

    // One random tangential direction per pond - the "downhill corridor."
    // Whatever the real terrain does along this direction (flat, gentle
    // slope, sheer cliff) is exactly what will show through, since we never
    // touch the geometry - only which material paints onto it.
    vec3 tangentA = normalize(cross(pondDir, abs(pondDir.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 tangentB = cross(pondDir, tangentA);
    float spillAngle = h3.y * 6.2831853;
    vec3 spillDir = normalize(tangentA * cos(spillAngle) + tangentB * sin(spillAngle));

    float alongSpill = dot(toP, spillDir);
    float sCoord = clamp(alongSpill - pondRadius, 0.0, SPILL_LENGTH);
    float spillWidth = mix(SPILL_WIDTH_TOP, SPILL_WIDTH_BOTTOM, sCoord / SPILL_LENGTH);
    float distToSpillLine = length(toP - spillDir * (pondRadius + sCoord));

    // Footprint = pond disc unioned with a tapering corridor leading away from it.
    pd.maskDist = min(footprintDist - pondRadius, distToSpillLine - spillWidth);
    return pd;
}

float mapPlanetDistanceFast(vec3 p,float time) {
    vec3 surfacePos = p - planetCenter;
    float distToCenter = length(surfacePos);

    vec3 macroData = sampleMacroDataSmooth(surfacePos);

    float baseElevation = macroData.x;
    float biomeY        = macroData.y;

    float coarseTerrainRadius = 5980.0 + baseElevation;
    float dTerrainCoarseRaw = distToCenter - coarseTerrainRadius;

    float waterRadius = 5995.0 + getWaterDisplacement(surfacePos, time);


    // 2. Define the ocean as a massive, 495-unit deep shell
    // max(outside the outer radius, inside the inner radius)
    float dOceanShell = max(distToCenter - waterRadius, 5800.0 - distToCenter);

    // 3. Cut a massive 1500-unit wide vertical cylinder out of the poles 
    // This gives your water actual vertical "walls" that stop right at the edge of the sinkhole
    float distFromPole = length(vec2(surfacePos.x, surfacePos.z));
    float dWater = max(dOceanShell, 1200.0 - distFromPole);

    const float DETAIL_SLACK = 160.0;
    const float GATE_MARGIN  = 240.0;

    if (dTerrainCoarseRaw > GATE_MARGIN) {
        float dTerrainSafe = (dTerrainCoarseRaw - DETAIL_SLACK) * 0.50;
        return min(dTerrainSafe, dWater);
    }

    float dLand = mapPlanetDistance(p);

    const float k = 2.0;
    float h = clamp(0.5 + 0.5 * (dWater - dLand) / k, 0.0, 1.0);
    return mix(dWater, dLand, h) - k * h * (1.0 - h);
}

SDFResult mapPlanet(vec3 p,float time) {
    vec3 surfacePos = p - planetCenter;
    float distToCenter = length(surfacePos);

    vec3 macroData = sampleMacroDataSmooth(surfacePos);

    float baseElevation = macroData.x;
    float biomeY        = macroData.y;

    float coarseTerrainRadius = 5980.0 + baseElevation;
    float dTerrainCoarseRaw = distToCenter - coarseTerrainRadius;

    float waterRadius = 5995.0 + getWaterDisplacement(surfacePos, time);

    // 2. Define the ocean as a massive, 495-unit deep shell
    // max(outside the outer radius, inside the inner radius)
    float dOceanShell = max(distToCenter - waterRadius, 5800.0 - distToCenter);

    // 3. Cut a massive 1500-unit wide vertical cylinder out of the poles 
    // This gives your water actual vertical "walls" that stop right at the edge of the sinkhole
    float distFromPole = length(vec2(surfacePos.x, surfacePos.z));
    float dWater = max(dOceanShell, 1200.0 - distFromPole);

    const float DETAIL_SLACK = 160.0;
    const float GATE_MARGIN  = 240.0;

    if (dTerrainCoarseRaw > GATE_MARGIN) {
        float dTerrainSafe = (dTerrainCoarseRaw - DETAIL_SLACK) * 0.50;
        float dMinSafe = min(dTerrainSafe, dWater);

        SDFResult res = makeSDF(dMinSafe, MAT_STONE);
        if (dWater < dTerrainSafe) {
            res = makeSDF(dWater, (biomeY > 5800.0) ? MAT_ICE : MAT_WATER);
        }
        return res; // far from any surface - ponds only resolve up close anyway
    }

    float dLand = mapPlanetDistance(p);

    SDFResult landRes = makeSDF(dLand, MAT_STONE);
    float perturbation = 10.0 * (noise(surfacePos * 0.1) * 0.5 - 1.0);
    float altitude = distToCenter - 6000.0;

    if (distToCenter < 900.0 + perturbation) {
        landRes = makeSDF(dLand, MAT_MAGMA);
    } else if (6000.0 - distToCenter > 60.0 + perturbation) {
        float mineralChance = smoothstep(0.65, 0.9, noise(surfacePos * 0.05));
        landRes.mat[MAT_STONE] = 1.0 - mineralChance;
        landRes.mat[MAT_MINERAL] = mineralChance;
    } else {
        vec3 up = normalize(surfacePos);
        vec3 landNorm = calcLandNormal(p);
        float flatness = clamp(dot(landNorm, up), 0.0, 1.0);
        float slopeFactor = smoothstep(0.75, 0.85, flatness);

        float temp = 1.0 - clamp((biomeY + perturbation) / 5500.0, 0.0, 1.0);
        float hum = fbm(surfacePos * 0.005) * 0.5 + 0.5;

        float w_sand = smoothstep(0.4, 0.7, temp) * smoothstep(0.0, 0.4, hum);
        float w_grass = smoothstep(0.2, 0.8, hum) * smoothstep(0.3, 0.7, temp) * smoothstep(0.0,2.0,altitude);
        float w_snow = smoothstep(0.3, 0.0, temp);
        float w_ice = smoothstep(0.1, -0.1, temp);
        float w_dirt = smoothstep(0.3, 0.7, hum) * smoothstep(0.4, 0.8, temp) * 0.5;
        float w_stone = 0.1;

        if (altitude < 15.0 + perturbation) {
            w_sand += smoothstep(0.0,5.0 + perturbation,altitude); 
        }
        if (altitude > 40.0 + perturbation) {
            w_stone += smoothstep(40.0, 70.0, altitude);
            w_snow += smoothstep(50.0, 90.0, altitude);
        }

        w_sand *= slopeFactor;
        w_grass *= slopeFactor;
        w_snow *= slopeFactor;
        w_dirt *= mix(1.0, slopeFactor, 0.6);
        w_stone += (1.0 - slopeFactor) * 0.8;

        float sum = w_sand + w_grass + w_snow + w_ice + w_dirt + w_stone;
        if(sum > 0.0) {
            landRes.mat[MAT_SAND] = w_sand / sum;
            landRes.mat[MAT_GRASS] = w_grass / sum;
            landRes.mat[MAT_SNOW] = w_snow / sum;
            landRes.mat[MAT_ICE] = w_ice / sum;
            landRes.mat[MAT_DIRT] = w_dirt / sum;
            landRes.mat[MAT_STONE] = w_stone / sum;
        }
    }

    // --- Ponds/waterfalls: pure material paint on top of whatever land
    // material was just computed. No geometry involved at all, so this is
    // applied last, uniformly, regardless of which branch above ran - the
    // ground stays exactly where physics/raymarching already found it, and
    // this just decides what color/material shows there.
    PondData pond = getPondData(p);
    if (pond.maskDist < POND_EDGE_SOFTNESS) {
        float pondWeight = 1.0 - smoothstep(-POND_EDGE_SOFTNESS, POND_EDGE_SOFTNESS, pond.maskDist);
        for (int i = 0; i < 16; i++) landRes.mat[i] *= (1.0 - pondWeight);
        landRes.mat[pond.matID] += pondWeight;
    }

    SDFResult waterRes;
    if (biomeY > 5800.0) {
        waterRes = makeSDF(dWater, MAT_ICE);
    } else {
        waterRes = makeSDF(dWater, MAT_WATER);
        waterRes.mat[MAT_WATER] = 0.95;
        waterRes.mat[MAT_MUD] = 0.05;
    }

    return sminSDF(landRes, waterRes, 2.0);
}

vec3 calcPlanetNormal(vec3 p,float time) {
    const float h = 0.01;
    const vec3 kxyy = vec3( 1.0, -1.0, -1.0);
    const vec3 kyyx = vec3(-1.0, -1.0,  1.0);
    const vec3 kyxy = vec3(-1.0,  1.0, -1.0);
    const vec3 kxxx = vec3( 1.0,  1.0,  1.0);
    return normalize(
        kxyy * mapPlanetDistanceFast(p + kxyy * h,time) +
        kyyx * mapPlanetDistanceFast(p + kyyx * h,time) +
        kyxy * mapPlanetDistanceFast(p + kyxy * h,time) +
        kxxx * mapPlanetDistanceFast(p + kxxx * h,time)
    );
}

#endif // SDF_GLSL