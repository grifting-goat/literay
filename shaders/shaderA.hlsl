[[vk::binding(1, 0)]] Texture3D<uint> brickAtlasIn; //atlas-packed BRICK_SIZE^3 bricks
[[vk::binding(6, 0)]] Texture3D<uint> brickIndirectionIn; // brick coord
[[vk::binding(7, 0)]] Texture3D<uint> voxelOccupancyIn;
[[vk::binding(11, 0)]] Texture3D<uint> chunkSkipIn;

struct Material {
    float4 color;
    float4 emissionColor;
    float emissionStrength;
    float smoothness;
    float specularProbability;
    float noise;

    float opaque;
    float3 _pad0;
};
[[vk::binding(2, 0)]] StructuredBuffer<Material> materials;

[[vk::binding(3, 0)]]
cbuffer CameraData {
    float4 camera_position;
    float4 camera_forward;
    float4 camera_right;
    float4 camera_up;

    float tan_fov_v;
    float tan_fov_h;
    float2 _pad_cam;

    float4 stream_box_min;
    float4 stream_box_max;
};

struct Entity {
    float4 position; // xyz used, w padding
    float4 rotation; // xyz used, w padding

    float4 worldMin;
    float4 worldMax;

    float scale; // scale <= 0 marks an unused slot
    uint modelIdx;
};
[[vk::binding(8, 0)]] StructuredBuffer<Entity> entities;

struct Model {
    uint voxelOffset; // index into modelVoxelsIn where this model's voxels start
    uint axes; // byte0 = up_axis, byte1 = cardinal_axis
    uint dimX;
    uint dimY;
    uint dimZ;
    uint size;
};
[[vk::binding(9, 0)]] StructuredBuffer<Model> models;
[[vk::binding(10, 0)]] ByteAddressBuffer modelVoxelsIn; // every model's voxel material-ids, back to back

[[vk::binding(4, 0)]]
[[vk::image_format("rgba8")]]
RWTexture2D<float4> outputImage;

[[vk::binding(5, 0)]]
[[vk::image_format("rgba32f")]]
RWTexture2D<float4> accumImage;

struct PushConstants {
    int2 _screen_size;
    uint frame_idx;
    uint accumCount; 

    int3 _voxel_grid_size;
    float _pad; 

    float3 sun_direction; // pre-normalized on the CPU
};
[[vk::push_constant]] PushConstants pc;

static const float FLT_INF = asfloat(0x7F800000);
static const uint MAX_BOUNCES = 2;
static const uint RAYS_PER_PIXEL = 1;
static const uint MAX_BONUS_BOUNCES = 3;
static const float FIREFLY_CLAMP = 10.0f;

static const uint BRICK_SIZE = 8;
static const uint BRICK_SIZE_SHIFT = 3; // log2(BRICK_SIZE)
static const uint EMPTY_SLOT = 0xFFFFFFFFu;

static const uint CHUNK_DIM = 32;
static const uint CHUNK_DIM_SHIFT = 5; // log2(CHUNK_DIM)
static const uint BRICKS_PER_CHUNK_AXIS = 4; // CHUNK_DIM / BRICK_SIZE

static const int CHUNK_STREAM_WINDOW_VOXELS_XZ = 2048;
static const int CHUNK_STREAM_WINDOW_VOXELS_Y = 512;
static const int CHUNK_STREAM_WINDOW_BRICKS_XZ = CHUNK_STREAM_WINDOW_VOXELS_XZ / int(BRICK_SIZE);
static const int CHUNK_STREAM_WINDOW_BRICKS_Y = CHUNK_STREAM_WINDOW_VOXELS_Y / int(BRICK_SIZE);
static const int CHUNK_STREAM_WINDOW_CHUNKS_XZ = CHUNK_STREAM_WINDOW_BRICKS_XZ / BRICKS_PER_CHUNK_AXIS;
static const int CHUNK_STREAM_WINDOW_CHUNKS_Y = CHUNK_STREAM_WINDOW_BRICKS_Y / BRICKS_PER_CHUNK_AXIS;


static const float MAX_RAY_DISTANCE = float(CHUNK_STREAM_WINDOW_VOXELS_XZ) / 2.0f;
static const float MAX_TOTAL_DISTANCE = MAX_RAY_DISTANCE * 2.0f;

static const float DENSITY = 0.0002f;//0.001f;

int3 WrapCoord3Aniso(int3 v, int nXZ, int nY) {
    int3 n = int3(nXZ, nY, nXZ);
    int3 m = v % n;
    return select(m < 0, m + n, m);
}

static const bool EnvironmentEnabled = true;


static const float3 NightSkyColourHorizon = float3(0.02f, 0.02f, 0.05f);
static const float3 NightSkyColourZenith  = float3(0.0f, 0.0f, 0.015f);
static const float3 DuskSkyColourHorizon  = float3(1.0f, 0.5f, 0.3f);
static const float3 DuskSkyColourZenith   = float3(0.3f, 0.25f, 0.45f);
static const float3 DaySkyColourHorizon   = float3(0.8f, 0.9f, 1.0f);
static const float3 DaySkyColourZenith    = float3(0.2f, 0.4f, 0.9f);

static const float3 SunColourDusk = float3(1.0f, 0.4f, 0.2f);
static const float3 SunColourDay  = float3(1.0f, 1.0f, 0.95f);

static const float SunFocus = 500.0f;
static const float SunIntensity = 0.5f;

static const float3 AmbientLight = float3(0.01f, 0.01f, 0.01f);

struct Ray {
    float3 origin;
    float3 direction;
    float3 invDir;

    float cumdist;

    float3 color;
    float3 incomingLight;
};

struct Hit {
    float dist;
    float3 end;
    float3 normal;
    uint mat_type;
    bool isEntity;
};

//RNG //https://github.com/SebLague/Ray-Tracing
uint NextRandom(inout uint state) {
    state = state * 747796405 + 2891336453;
    uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
    result = (result >> 22) ^ result;
    return result;
}

float RandomValue(inout uint state) {
    return NextRandom(state) * 2.3283064365386963e-10f; // 1 / 2^32
}

static const float2 R2_STEP = float2(0.7548776662f, 0.5698402909f);

float InterleavedGradientNoise(float2 px) {
    return frac(52.9829189f * frac(dot(px, float2(0.06711056f, 0.00583715f))));
}

float2 NextBlueNoise2(inout float2 state) {
    state = frac(state + R2_STEP);
    return state;
}

uint HashVoxelCell(int3 cell) {
    uint seed = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u;
    return NextRandom(seed);
}

// Uniform direction on the unit sphere
float3 RandomDirection(inout uint state) {
    float z = 1.0f - 2.0f * RandomValue(state);
    float phi = 6.2831853f * RandomValue(state);
    float r = sqrt(max(0.0f, 1.0f - z * z));
    float s, c;
    sincos(phi, s, c);
    return float3(r * c, r * s, z);
}


void BuildONB(float3 n, out float3 t, out float3 b) {
    float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0f + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = float3(c, sign + n.y * n.y * a, -n.y);
}


float3 CosineWeightedHemisphereSample(inout uint state) {
    float u1 = RandomValue(state);
    float u2 = RandomValue(state);
    float r = sqrt(u1);
    float phi = 6.2831853f * u2;
    float s, c;
    sincos(phi, s, c);
    return float3(r * c, r * s, sqrt(max(0.0f, 1.0f - u1)));
}

float3 CosineWeightedDirection(float3 normal, inout uint state) {
    float3 t, b;
    BuildONB(normal, t, b);
    float3 local = CosineWeightedHemisphereSample(state);
    return normalize(t * local.x + b * local.y + normal * local.z);
}

Ray CreateRay(float3 origin, float3 direction) {
    Ray ray;
    ray.origin = origin;
    direction = select(direction == 0.0f, 1e-6f, direction);
    ray.direction = normalize(direction);
    ray.invDir = rcp(ray.direction);
    ray.cumdist = 0.000f;
    ray.color = float3(1.0f, 1.0f, 1.0f);
    ray.incomingLight = 0.0f;
    return ray;
}

Ray CreateCameraRay(float x, float y) {
    x = (x - 0.5f) * 2.0f;
    y = (0.5f - y) * 2.0f;

    float3 direction = camera_forward.xyz + x * tan_fov_h * camera_right.xyz + y * tan_fov_v * camera_up.xyz;

    return CreateRay(camera_position.xyz, direction);
}


bool ChunkHasGeometry(int3 chunkCell) {
    int3 texCell = WrapCoord3Aniso(chunkCell, CHUNK_STREAM_WINDOW_CHUNKS_XZ, CHUNK_STREAM_WINDOW_CHUNKS_Y);
    uint owner = chunkSkipIn.Load(int4(texCell, 0));
    uint expected = (asuint(chunkCell.x) & 0x3FFu)
        | ((asuint(chunkCell.y) & 0x3FFu) << 10)
        | ((asuint(chunkCell.z) & 0x3FFu) << 20)
        | (1u << 30) | (1u << 31);
    return owner == expected;
}

uint ReadBrickSlot(int3 brickCell) {
    int3 texCell = WrapCoord3Aniso(brickCell, CHUNK_STREAM_WINDOW_BRICKS_XZ, CHUNK_STREAM_WINDOW_BRICKS_Y);
    return brickIndirectionIn.Load(int4(texCell, 0));
}

int3 DecodeAtlasSlot(uint slot) {
    uint sx = slot & 0xFFu;
    uint sy = (slot >> 8) & 0xFFu;
    uint sz = (slot >> 16) & 0xFFu;
    return int3(sx, sy, sz) * int(BRICK_SIZE);
}

// occupancy bit, addressed by world voxel coord
bool IsVoxelSolid(int3 worldCell) {
    int3 texCell = WrapCoord3Aniso(worldCell, CHUNK_STREAM_WINDOW_VOXELS_XZ, CHUNK_STREAM_WINDOW_VOXELS_Y);
    uint word = voxelOccupancyIn.Load(int4(texCell.x >> 5, texCell.y, texCell.z, 0));
    uint bit = uint(texCell.x) & 31u;
    return ((word >> bit) & 1u) != 0u;
}

uint ReadAtlasVoxel(int3 atlasCell) {
    return brickAtlasIn.Load(int4(atlasCell, 0));
}


float3 DDAStepMask(float3 tMax) {
    float3 mask;
    mask.x = (tMax.x <= tMax.y) && (tMax.x <= tMax.z);
    mask.y = (tMax.y <  tMax.x) && (tMax.y <= tMax.z);
    mask.z = 1.0f - mask.x - mask.y;
    return mask;
} //goofy optimization fable said to do


int3 SnapEntryCell(int3 floorCell, int3 cellMin, int3 cellMax, float3 normal) {
    int3 snapped = select(normal < 0.0f, cellMin, cellMax);
    return select(normal != 0.0f, snapped, floorCell);
}


//https://github.com/SebLague/Ray-Tracing
/*
float3 GetEnvironmentLight(Ray ray) {

    if (!EnvironmentEnabled) {return 0;}

    if (ray.direction.y <= 0.0f) {
        float voidT = smoothstep(-0.4f, 0.0f, ray.direction.y);
        return SkyColourHorizon * voidT;
    }

    float skyGradientT = pow(smoothstep(0, 0.4, ray.direction.y), 0.35);
    float3 skyGradient = lerp(SkyColourHorizon, SkyColourZenith, skyGradientT);
    float sun = pow(max(0, dot(ray.direction, pc.sun_direction)), SunFocus) * SunIntensity;

    return skyGradient + sun;
}
*/

float3 GetEnvironmentLight(Ray ray) { 

    if (!EnvironmentEnabled) {return AmbientLight;}

    float elevation = pc.sun_direction.y;
    float tNightToDusk = smoothstep(-0.2f, 0.0f, elevation);
    float3 horizonNightDusk = lerp(NightSkyColourHorizon, DuskSkyColourHorizon, tNightToDusk);
    float3 zenithNightDusk = lerp(NightSkyColourZenith, DuskSkyColourZenith, tNightToDusk);

    float tDuskToDay = smoothstep(0.0f, 0.25f, elevation);
    float3 horizonColor = lerp(horizonNightDusk, DaySkyColourHorizon, tDuskToDay);
    float3 zenithColor = lerp(zenithNightDusk, DaySkyColourZenith, tDuskToDay);

    float sunVisibility = smoothstep(-0.05f, 0.05f, elevation);
    float3 sunColor = lerp(SunColourDusk, SunColourDay, smoothstep(0.0f, 0.3f, elevation));

    float cameraHeight = max(0.0f, camera_position.y - 130.0f); // ~130 approximates typical ground/sea level
    float horizonOffset = saturate(cameraHeight / MAX_RAY_DISTANCE);

    if (ray.direction.y <= -horizonOffset) {

        float3 deepHaze = zenithColor * 0.3f;
        float voidT = smoothstep(-0.35f - horizonOffset, -horizonOffset, ray.direction.y);
        return lerp(deepHaze, horizonColor, voidT);
    }

    float skyGradientT = pow(smoothstep(-horizonOffset, 0.4f - horizonOffset, ray.direction.y), 0.35);
    float3 skyGradient = lerp(horizonColor, zenithColor, skyGradientT);

    float3 sunDiscDir = normalize(float3(pc.sun_direction.x, pc.sun_direction.y - horizonOffset, pc.sun_direction.z));
    float sun = pow(max(0, dot(ray.direction, sunDiscDir)), SunFocus) * SunIntensity * sunVisibility;

    return skyGradient + sun * sunColor;
}



uint ReadModelVoxel(uint index) {
    uint word = modelVoxelsIn.Load(index & ~3u);
    return (word >> ((index & 3u) * 8u)) & 0xFFu;
}

bool TestAABB(Ray ray, float3 boundsMin, float3 boundsMax) {
    float3 t0 = (boundsMin - ray.origin) * ray.invDir;
    float3 t1 = (boundsMax - ray.origin) * ray.invDir;
    float3 tsmall = min(t0, t1);
    float3 tbig = max(t0, t1);
    float tmin = max(max(tsmall.x, tsmall.y), max(tsmall.z, 0.0f));
    float tmax = min(min(tbig.x, tbig.y), tbig.z);
    return tmax >= tmin;
}


//standard dda voxel, over the static world grid
Hit CastStaticRay(Ray ray) {

    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);
    hit.isEntity = false;

    int3 cell = int3(floor(ray.origin));

    int3 istep = int3(sign(ray.direction));
    float3 invDir = ray.invDir;
    float3 tDelta = abs(invDir);
    float3 stepSign = max(sign(ray.direction), 0.0f);

    float3 boxT0 = (stream_box_min.xyz + 1.0f - ray.origin) * invDir;
    float3 boxT1 = (stream_box_max.xyz - 1.0f - ray.origin) * invDir;
    float3 boxTBig = select(ray.direction != 0.0f, max(boxT0, boxT1), FLT_INF);
    float boxExit = min(min(boxTBig.x, boxTBig.y), boxTBig.z);
    float clipped = min(MAX_RAY_DISTANCE, MAX_TOTAL_DISTANCE - ray.cumdist);
    float tExit = min(clipped, boxExit) - 0.01f;

    // level 1: chunk skip
    int3 chunkCell = cell >> int(CHUNK_DIM_SHIFT);
    float3 chunkNextBoundary = float3(chunkCell) * CHUNK_DIM + stepSign * CHUNK_DIM;
    float3 chunkTMax = select(ray.direction != 0.0f, (chunkNextBoundary - ray.origin) * invDir, FLT_INF);
    float3 chunkTDelta = tDelta * CHUNK_DIM;
    float chunkTCurrent = 0.0f;
    float3 normal = float3(0.0f, 0.0f, 0.0f);

    while (chunkTCurrent < tExit) {

        if (ChunkHasGeometry(chunkCell)) {

            // level 2: brick skip
            int3 chunkMin = chunkCell << int(CHUNK_DIM_SHIFT);
            int3 chunkMax = chunkMin + int(CHUNK_DIM) - 1;
            float3 chunkPos = ray.origin + ray.direction * chunkTCurrent;
            int3 chunkEntryCell = SnapEntryCell(clamp(int3(floor(chunkPos)), chunkMin, chunkMax), chunkMin, chunkMax, normal);
            int3 brickCell = chunkEntryCell >> int(BRICK_SIZE_SHIFT);

            float brickTCurrent = chunkTCurrent;
            float3 brickNextBoundary = float3(brickCell) * BRICK_SIZE + stepSign * BRICK_SIZE;
            float3 brickTMax = select(ray.direction != 0.0f, (brickNextBoundary - ray.origin) * invDir, FLT_INF);
            float3 brickTDelta = tDelta * BRICK_SIZE;

            float chunkBoundary = min(min(chunkTMax.x, chunkTMax.y), chunkTMax.z) + 0.01f;
            float chunkExit = min(chunkBoundary, tExit);

            int3 chunkBrickMin = chunkMin >> int(BRICK_SIZE_SHIFT);
            int3 chunkBrickMax = chunkBrickMin + int(BRICKS_PER_CHUNK_AXIS) - 1;

            while (brickTCurrent < chunkExit) {

                if (any(brickCell < chunkBrickMin) || any(brickCell > chunkBrickMax)) { break; }

                uint slot = ReadBrickSlot(brickCell);
                if (slot != EMPTY_SLOT) {
                    int3 atlasOrigin = DecodeAtlasSlot(slot);

                    int3 brickMin = brickCell << int(BRICK_SIZE_SHIFT);
                    int3 brickMax = brickMin + int(BRICK_SIZE) - 1;
                    float3 pos = ray.origin + ray.direction * brickTCurrent;
                    int3 fineCell = SnapEntryCell(clamp(int3(floor(pos)), brickMin, brickMax), brickMin, brickMax, normal);

                    float tCurrent = brickTCurrent;
                    float3 tMax = select(ray.direction != 0.0f, (float3(fineCell) + stepSign - ray.origin) * invDir, FLT_INF);

                    float brickBoundary = min(min(brickTMax.x, brickTMax.y), brickTMax.z) + 0.01f;
                    float brickExit = min(brickBoundary, chunkExit);

                    while (tCurrent < brickExit) {
                        if (any(fineCell < brickMin) || any(fineCell > brickMax)) { break; }
                        // level 3: voxel skip
                        if (IsVoxelSolid(fineCell)) {
                            hit.mat_type = ReadAtlasVoxel(atlasOrigin + (fineCell - brickMin));
                            hit.dist = tCurrent;
                            hit.normal = normal;
                            hit.end = ray.origin + ray.direction * tCurrent;
                            return hit;
                        }

                        float3 mask = DDAStepMask(tMax);
                        tCurrent = dot(mask, tMax);
                        tMax += mask * tDelta;
                        normal = -mask * float3(istep);
                        fineCell += int3(mask) * istep;
                    }
                }

                float3 brickMask = DDAStepMask(brickTMax);
                brickTCurrent = dot(brickMask, brickTMax);
                brickTMax += brickMask * brickTDelta;
                normal = -brickMask * float3(istep);
                brickCell += int3(brickMask) * istep;
            }
        }

        float3 chunkMask = DDAStepMask(chunkTMax);
        chunkTCurrent = dot(chunkMask, chunkTMax);
        chunkTMax += chunkMask * chunkTDelta;
        normal = -chunkMask * float3(istep);

        chunkCell += int3(chunkMask) * istep;
    }
    return hit;
}


Hit CastEntityRay(Ray ray, Entity ent) {

    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);
    hit.isEntity = false;

    if (ent.scale <= 0.0f) { // unused slot
        return hit;
    }

    uint modelCount, modelStride;
    models.GetDimensions(modelCount, modelStride);
    if (ent.modelIdx >= modelCount) {
        return hit;
    }

    Model model = models[ent.modelIdx];
    if (model.size == 0) {
        return hit;
    }

    if (!TestAABB(ray, ent.worldMin.xyz, ent.worldMax.xyz)) {
        return hit;
    }

    float3 dim = float3(model.dimX, model.dimY, model.dimZ);

    float s, c;
    sincos(-ent.rotation.y, s, c); 

    // transform the ray into the models local voxel space
    float3 rel = ray.origin - ent.position.xyz;
    float3 rotatedRel = float3(c * rel.x - s * rel.z, rel.y, s * rel.x + c * rel.z);
    float3 rotatedDir = float3(c * ray.direction.x - s * ray.direction.z, ray.direction.y, s * ray.direction.x + c * ray.direction.z);

    float3 lo = rotatedRel / ent.scale + float3(dim.x * 0.5f, 0.0f, dim.z * 0.5f);
    float3 ld = rotatedDir / ent.scale;

    float3 invD = rcp(ld);
    float3 t0 = (0.0f - lo) * invD;
    float3 t1 = (dim - lo) * invD;
    float3 tmin3 = min(t0, t1);
    float3 tmax3 = max(t0, t1);
    float tEnter = max(max(tmin3.x, tmin3.y), max(tmin3.z, 0.0f));
    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z) - 0.001f;
    if (tExit <= tEnter) {
        return hit;
    }

    int3 idim = int3(model.dimX, model.dimY, model.dimZ);
    int3 istep = int3(sign(ld));
    float3 tDelta = abs(invD);
    float3 stepSign = max(sign(ld), 0.0f);

    float3 p = lo + ld * (tEnter + 0.001f);
    int3 cell = clamp(int3(floor(p)), int3(0, 0, 0), idim - 1);
    float3 tMax = select(ld != 0.0f, (float3(cell) + stepSign - lo) * invD, FLT_INF);
    float tCurrent = tEnter;
    float3 entryMask = select(tmin3 == tEnter, 1.0f, 0.0f);
    float3 lnormal = -entryMask * float3(istep);

    while (tCurrent < tExit) {
        uint localIdx = uint(cell.x + cell.y * idim.x + cell.z * idim.x * idim.y);
        uint mat = ReadModelVoxel(model.voxelOffset + localIdx);
        if (mat != 0) {
            hit.mat_type = mat;
            hit.dist = tCurrent;
            hit.end = ray.origin + ray.direction * tCurrent;
            hit.normal = float3(c * lnormal.x + s * lnormal.z, lnormal.y, -s * lnormal.x + c * lnormal.z);
            hit.isEntity = true;
            return hit;
        }

        float3 mask = DDAStepMask(tMax);
        tCurrent = dot(mask, tMax);
        tMax += mask * tDelta;
        lnormal = -mask * float3(istep);
        cell += int3(mask) * istep;
        if (any(cell < 0) || any(cell >= idim)) {
            break;
        }
    }
    return hit;
}

//tests every entity slot except skipIndex and keeps the closest hit
Hit CastDynamicRay(Ray ray, int skipIndex) {
    Hit best;
    best.mat_type = 0;
    best.dist = FLT_INF;
    best.end = float3(0.0f, 0.0f, 0.0f);
    best.normal = float3(0.0f, 0.0f, 0.0f);
    best.isEntity = false;

    uint entityCount, entityStride;
    entities.GetDimensions(entityCount, entityStride);

    for (uint i = 0; i < entityCount; i++) {
        if (int(i) == skipIndex) {
            continue;
        }
        Hit hit = CastEntityRay(ray, entities[i]);
        if (hit.dist < best.dist) {
            best = hit;
        }
    }
    return best;
}


// skipIndex < 0 tests every entity; otherwise that single entity slot is excluded
Hit CastRay(Ray ray, int skipIndex) {
    Hit staticHit = CastStaticRay(ray);

    Hit dynamicHit = CastDynamicRay(ray, skipIndex);
    if (dynamicHit.dist < staticHit.dist) {
        return dynamicHit;
    }
    return staticHit;
}
float3 Reflect(float3 dir, float3 normal) {
    return dir - 2 * dot(dir, normal) * normal;
}


[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= pc._screen_size.x || pixel.y >= pc._screen_size.y) {
        return;
    }

    uint pixelIndex = uint(pixel.y) * uint(pc._screen_size.x) + uint(pixel.x);
    uint rngState = pixelIndex + pc.frame_idx * 719393; //borrowed might want to update

    float2 blueState = frac(float2(InterleavedGradientNoise(float2(pixel)), InterleavedGradientNoise(float2(pixel) + 71.0f))
        + R2_STEP * float(pc.accumCount));

    float3 totalLight = 0.0f;
    uint bonus = 0;

    for (uint sample = 0; sample < RAYS_PER_PIXEL; sample++) {
        float2 jitter = pc.accumCount > 1 ? NextBlueNoise2(blueState) - 0.5f : 0.0f;
        float x = (float(pixel.x) + 0.5f + jitter.x) / float(pc._screen_size.x);
        float y = (float(pixel.y) + 0.5f + jitter.y) / float(pc._screen_size.y);

        Ray r = CreateCameraRay(x, y);
        float3 voidColor = 0.0f;
        float viewDist = 0.0f;
        bool trackViewDist = true;
        bool escaped = false;
        for (uint bounces = 0; bounces < MAX_BOUNCES + bonus; bounces++) {
            Hit hit = CastRay(r, bounces == 0 ? 0 : -1);
            r.cumdist += hit.dist;
            if (trackViewDist && hit.mat_type != 0) {
                viewDist += hit.dist;
                voidColor = GetEnvironmentLight(r) * r.color;
            }
            if (hit.mat_type != 0) {
                Material mat = materials[hit.mat_type];

                float distScatter = log(RandomValue(rngState)) * -1 / DENSITY;
                if (distScatter < hit.dist) {
                    r.origin = r.origin + r.direction * distScatter;
                    r.direction = RandomDirection(rngState);
                    continue;
                }

                //shadow ray
                if (!bounces || bonus == 1) { //keep shdows in mirrors

                    float sunElevationVisibility = smoothstep(-0.05f, 0.05f, pc.sun_direction.y);
                    if (sunElevationVisibility > 0.0f) {

                        Ray shadow = CreateRay(hit.end + hit.normal * 0.01f + pc.sun_direction * 0.02f, pc.sun_direction);
                        Hit shadowHit = CastRay(shadow, -1);

                        if (shadowHit.mat_type == 0) {
                            float sunAmount = saturate(dot(hit.normal, pc.sun_direction));
                            r.incomingLight += SunIntensity * sunAmount * sunElevationVisibility * mat.color.rgb * r.color;
                        }
                    }
                }

                int3 hitCell = int3(floor(hit.end - hit.normal * 0.5f));
                uint noiseState = HashVoxelCell(hitCell);
                float3 colorNoise = float3(RandomValue(noiseState), RandomValue(noiseState), RandomValue(noiseState)) - 0.5f;
                float3 noisyColor = saturate(mat.color.rgb + colorNoise * mat.noise);

                r.color *= noisyColor;
                r.incomingLight += mat.emissionColor.rgb * r.color; // emissionColor arrives premultiplied by emissionStrength

                r.origin = hit.end + hit.normal * 0.01f;

                bool isSpecular = mat.specularProbability >= RandomValue(rngState);
                r.cumdist -= hit.dist * isSpecular;
                trackViewDist = isSpecular;
                bonus = min(bonus + (uint)isSpecular, MAX_BONUS_BOUNCES);

                float3 diffuseDir = CosineWeightedDirection(hit.normal, rngState);
                float3 specularDir = Reflect(r.direction, hit.normal);

                r.direction = normalize(lerp(diffuseDir, specularDir, mat.smoothness * isSpecular));
                r.invDir = rcp(r.direction);

                r.incomingLight += AmbientLight * r.color * (!isSpecular); //so specular doesnt add any ambiant light

                float x = saturate(viewDist / MAX_RAY_DISTANCE);
                if (x > 0.85f) {
                    float voidT = 44.444f * ((x-0.85) * (x-0.85));
                    r.incomingLight = lerp(r.incomingLight, voidColor, voidT);
                }


            }
            else {
                r.incomingLight += GetEnvironmentLight(r) * r.color;
                escaped = true;
                break;
            }
        }

        if (!escaped) {
            r.incomingLight += GetEnvironmentLight(r) * r.color;
        }


        float sampleLuma = dot(r.incomingLight, float3(0.2126f, 0.7152f, 0.0722f));
        if (sampleLuma > FIREFLY_CLAMP) {
            r.incomingLight *= FIREFLY_CLAMP / sampleLuma;
        }

        totalLight += r.incomingLight;


        
    }



    float3 newSample = totalLight / float(RAYS_PER_PIXEL);

    float3 prevAccum = accumImage[pixel].rgb;
    float3 accum = prevAccum + (newSample - prevAccum) / float(pc.accumCount);

    accumImage[pixel] = float4(accum, 1.0f);
    outputImage[pixel] = float4(accum, 1.0f);

}