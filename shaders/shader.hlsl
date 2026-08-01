[[vk::binding(1, 0)]] Texture3D<uint> brickAtlasIn; //atlas-packed BRICK_SIZE^3 bricks
[[vk::binding(6, 0)]] Texture3D<uint> brickIndirectionIn; // brick coord
[[vk::binding(7, 0)]] Texture3D<uint> voxelOccupancyIn; 

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
};

struct Entity {
    float4 position; // xyz used, w padding
    float4 rotation; // xyz used, w padding

    // conservative world-space AABB (valid under any yaw), precomputed on the CPU
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
static const float MAX_RAY_DISTANCE = 400.0f;
static const uint MAX_BOUNCES = 2;
static const uint RAYS_PER_PIXEL = 1;
static const uint MAX_BONUS_BOUNCES = 3;

static const uint BRICK_SIZE = 8; // must match VOXEL_MASK_BLOCK_SIZE in display.h
static const uint BRICK_SIZE_SHIFT = 3; // log2(BRICK_SIZE)
static const uint EMPTY_SLOT = 0xFFFFFFFFu; // must match EMPTY_SLOT in display.h

static const bool EnvironmentEnabled = true;
static const float3 SkyColourHorizon = float3(0.8f, 0.9f, 1.0f);
static const float3 SkyColourZenith = float3(0.2f, 0.4f, 0.9f);
static const float SunFocus = 500.0f;
static const float SunIntensity = 0.5f;

static const float3 AmbientLight = float3(0.05f, 0.05f, 0.05f);

struct Ray {
    float3 origin;
    float3 direction;
    float3 invDir;

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

Ray CreateRay(float3 origin, float3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = normalize(direction);
    ray.invDir = rcp(ray.direction);
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


uint ReadBrickSlot(int3 brickCell) {
    return brickIndirectionIn.Load(int4(brickCell, 0));
}

int3 DecodeAtlasSlot(uint slot) {
    uint sx = slot & 0xFFu;
    uint sy = (slot >> 8) & 0xFFu;
    uint sz = (slot >> 16) & 0xFFu;
    return int3(sx, sy, sz) * int(BRICK_SIZE);
}

// occupancy bit, addressed by world voxel coord
bool IsVoxelSolid(int3 worldCell) {
    uint word = voxelOccupancyIn.Load(int4(worldCell.x >> 5, worldCell.y, worldCell.z, 0));
    uint bit = uint(worldCell.x) & 31u;
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


//https://github.com/SebLague/Ray-Tracing
float3 GetEnvironmentLight(Ray ray) { //replce with skybox or upgrad with time of day

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



// dynamic/entity path temporarily disabled while the static path is rewritten for the brick atlas
/*
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
*/

//standard dda voxel, over the static world grid
Hit CastStaticRay(Ray ray) {

    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);
    hit.isEntity = false;

    int3 cell = int3(floor(ray.origin));
    if (any(cell < 0) || any(cell >= pc._voxel_grid_size)) {
        return hit;
    }

    int3 istep = int3(sign(ray.direction));
    float3 invDir = ray.invDir;
    float3 tDelta = abs(invDir);
    float3 stepSign = max(sign(ray.direction), 0.0f);

    float3 tFarPerAxis = max(-ray.origin * invDir, (float3(pc._voxel_grid_size) - ray.origin) * invDir);
    float tExit = min(min(tFarPerAxis.x, tFarPerAxis.y), min(tFarPerAxis.z, MAX_RAY_DISTANCE)) - 0.01f;

    int3 brickGridSize = pc._voxel_grid_size >> int(BRICK_SIZE_SHIFT);

    int3 brickCell = cell >> int(BRICK_SIZE_SHIFT);
    float3 brickNextBoundary = float3(brickCell) * BRICK_SIZE + stepSign * BRICK_SIZE;
    float3 brickTMax = select(ray.direction != 0.0f, (brickNextBoundary - ray.origin) * invDir, FLT_INF);
    float3 brickTDelta = tDelta * BRICK_SIZE;
    float brickTCurrent = 0.0f;
    float3 normal = float3(0.0f, 0.0f, 0.0f);

    while (brickTCurrent < tExit) {

        uint slot = ReadBrickSlot(brickCell);
        if (slot != EMPTY_SLOT) {
            int3 atlasOrigin = DecodeAtlasSlot(slot);

            float3 pos = ray.origin + ray.direction * (brickTCurrent + 0.01f);
            int3 brickMin = brickCell << int(BRICK_SIZE_SHIFT);
            int3 fineCell = clamp(int3(floor(pos)), brickMin, brickMin + int(BRICK_SIZE) - 1);

            float tCurrent = brickTCurrent;
            float3 tMax = select(ray.direction != 0.0f, (float3(fineCell) + stepSign - ray.origin) * invDir, FLT_INF);

            float brickBoundary = min(min(brickTMax.x, brickTMax.y), brickTMax.z) + 0.01f;
            float brickExit = min(brickBoundary, tExit);

            while (tCurrent < brickExit) {
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
        if (any(brickCell < 0) || any(brickCell >= brickGridSize)) {
            break;
        }
    }
    return hit;
}

/*
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
*/

// skipIndex < 0 tests every entity; otherwise that single entity slot is excluded
Hit CastRay(Ray ray, int skipIndex) {
    return CastStaticRay(ray);
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

    float3 totalLight = 0.0f;
    uint bonus = 0;

    for (uint sample = 0; sample < RAYS_PER_PIXEL; sample++) {
        float jitterX = RandomValue(rngState) - 0.5f;
        float jitterY = RandomValue(rngState) - 0.5f;
        float x = (float(pixel.x) + 0.5f + jitterX) / float(pc._screen_size.x);
        float y = (float(pixel.y) + 0.5f + jitterY) / float(pc._screen_size.y);

        Ray r = CreateCameraRay(x, y);
        for (uint bounces = 0; bounces < MAX_BOUNCES + bonus; bounces++) {
            Hit hit = CastRay(r, bounces == 0 ? 0 : -1); // skip entity 0 (the player) only on the ray straight from the camera, so the player doesn't see themselves
            if (hit.mat_type != 0) {
                Material mat = materials[hit.mat_type];

                //shadow ray
                if (!bounces || bonus == 1) { //keep shdows in mirrors
                    Ray shadow = CreateRay(hit.end + (hit.normal * 0.01f), pc.sun_direction);
                    Hit shadowHit = CastRay(shadow, -1);

                    if (shadowHit.mat_type == 0) { 
                        float sunAmount = saturate(dot(hit.normal, pc.sun_direction));
                        r.incomingLight += SunIntensity * sunAmount * mat.color.rgb * r.color;
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
                bonus = min(bonus + (uint)isSpecular, MAX_BONUS_BOUNCES);

                float3 diffuseDir = normalize(hit.normal + RandomDirection(rngState));
                float3 specularDir = Reflect(r.direction, hit.normal);

                r.direction = normalize(lerp(diffuseDir, specularDir, mat.smoothness * isSpecular));
                r.invDir = rcp(r.direction);

                r.incomingLight += AmbientLight * r.color * (!isSpecular); //so specular doesnt add any ambiant light
            }
            else {
                r.incomingLight += GetEnvironmentLight(r) * r.color;
                break;
            }
        }

        totalLight += r.incomingLight;
        
    }

    

    float3 newSample = totalLight / float(RAYS_PER_PIXEL);


    float3 prevAccum = accumImage[pixel].rgb;
    float3 accum = prevAccum + (newSample - prevAccum) / float(pc.accumCount);

    accumImage[pixel] = float4(accum, 1.0f);
    outputImage[pixel] = float4(accum, 1.0f);

}