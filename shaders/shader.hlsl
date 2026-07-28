[[vk::binding(1, 0)]] Texture3D<uint> voxelsIn; // material ids, R8_UINT
[[vk::binding(6, 0)]] ByteAddressBuffer voxelsMaskIn;
[[vk::binding(7, 0)]] ByteAddressBuffer voxelsOccIn; // 1 bit per voxel
[[vk::binding(8, 0)]] ByteAddressBuffer entityVoxelsIn; // 1 material byte per voxel, entity-local grid

struct Material {
    float4 color;
    float4 emissionColor;
    float emissionStrength;
    float smoothness;
    float specularProbability;
    float noise;
};
[[vk::binding(2, 0)]] StructuredBuffer<Material> materials;
[[vk::binding(9, 0)]] StructuredBuffer<Material> entityMaterials; // separate palette entities read from

[[vk::binding(3, 0)]]
cbuffer CameraData {
    float4 camera_position;
    float4 camera_forward;
    float4 camera_right;
    float4 camera_up;

    float tan_fov_v;
    float tan_fov_h;
};

[[vk::binding(4, 0)]]
[[vk::image_format("rgba8")]]
RWTexture2D<float4> outputImage;

[[vk::binding(5, 0)]]
[[vk::image_format("rgba32f")]]
RWTexture2D<float4> accumImage;

struct PushConstants {
    int2 _screen_size;
    uint frame_idx;
    uint accumCount; // fills the slot int3 below needs padding for anyway

    int3 _voxel_grid_size;
    float _sun_dir_pad; // fills the slot sun_direction below needs padding for anyway, same trick as accumCount above

    float3 sun_direction; // pre-normalized on the CPU side before upload
    float entity_yaw;

    float3 entity_pos;
    float _entity_pad;

    int3 entity_dim; // all zero = no entity
    float entity_scale;
};
[[vk::push_constant]] PushConstants pc;

static const float FLT_INF = asfloat(0x7F800000);
static const float MAX_RAY_DISTANCE = 400.0f;
static const uint MAX_BOUNCES = 2;
static const uint RAYS_PER_PIXEL = 1;

static const uint BLOCK_SIZE = 8; // must match VOXEL_MASK_BLOCK_SIZE in display.h

static const bool EnvironmentEnabled = true;
static const float3 SkyColourHorizon = float3(0.8f, 0.9f, 1.0f);
static const float3 SkyColourZenith = float3(0.2f, 0.4f, 0.9f);
static const float SunFocus = 500.0f;
static const float SunIntensity = 0.5f;

static const float3 AmbientLight = float3(0.05f, 0.05f, 0.05f);

struct Ray {
    float3 origin;
    float3 direction;

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

// Uniform direction on the unit sphere (already normalized)
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


bool IsBlockSolid(uint blockIndex) {
    uint word = voxelsMaskIn.Load((blockIndex >> 5) << 2);
    uint bit = blockIndex & 31u;
    return ((word >> bit) & 1u) != 0u;
}

// occupancy bit only; the material byte is fetched just once, on an actual hit
bool IsVoxelSolid(uint index) {
    uint word = voxelsOccIn.Load((index >> 5) << 2);
    uint bit = index & 31u;
    return ((word >> bit) & 1u) != 0u;
}

uint ReadVoxel(int3 cell) {
    return voxelsIn.Load(int4(cell, 0));
}

uint ReadEntityVoxel(uint index) {
    uint word = entityVoxelsIn.Load(index & ~3u);
    return (word >> ((index & 3u) * 8u)) & 0xFFu;
}


float3 DDAStepMask(float3 tMax) {
    float3 mask;
    mask.x = (tMax.x <= tMax.y) && (tMax.x <= tMax.z);
    mask.y = (tMax.y <  tMax.x) && (tMax.y <= tMax.z);
    mask.z = 1.0f - mask.x - mask.y;
    return mask;
}

//https://github.com/SebLague/Ray-Tracing
float3 GetEnvironmentLight(Ray ray) {

    if (!EnvironmentEnabled) {
        return 0;
    }

    if (ray.direction.y <= 0.0f) {
        float voidT = smoothstep(-0.4f, 0.0f, ray.direction.y);
        return SkyColourHorizon * voidT;
    }

    float skyGradientT = pow(smoothstep(0, 0.4, ray.direction.y), 0.35);
    float3 skyGradient = lerp(SkyColourHorizon, SkyColourZenith, skyGradientT);
    float sun = pow(max(0, dot(ray.direction, pc.sun_direction)), SunFocus) * SunIntensity;

    return skyGradient + sun;
}

Hit CastVoxelRay(Ray ray) {

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
    float3 invDir = rcp(ray.direction);
    float3 tDelta = abs(invDir);
    float3 stepSign = max(sign(ray.direction), 0.0f);

    float3 tFarPerAxis = max(-ray.origin * invDir, (float3(pc._voxel_grid_size) - ray.origin) * invDir);
    float tExit = min(min(tFarPerAxis.x, tFarPerAxis.y), min(tFarPerAxis.z, MAX_RAY_DISTANCE)) - 0.01f;

    int3 idxStep = istep * int3(1, pc._voxel_grid_size.x, pc._voxel_grid_size.x * pc._voxel_grid_size.y);
    int3 blockGridSize = pc._voxel_grid_size / int(BLOCK_SIZE);
    int3 blockIdxStep = istep * int3(1, blockGridSize.x, blockGridSize.x * blockGridSize.y);

    // coarse state only; fine state is rebuilt on entry into a solid block
    int3 blockCell = cell / int(BLOCK_SIZE);
    float3 blockNextBoundary = float3(blockCell) * BLOCK_SIZE + stepSign * BLOCK_SIZE;
    float3 blockTMax = select(ray.direction != 0.0f, (blockNextBoundary - ray.origin) * invDir, FLT_INF);
    float3 blockTDelta = tDelta * BLOCK_SIZE;
    int blockIndex = blockCell.x + blockCell.y * blockGridSize.x + blockCell.z * blockGridSize.x * blockGridSize.y;
    float blockTCurrent = 0.0f;
    float3 normal = float3(0.0f, 0.0f, 0.0f);

    while (blockTCurrent < tExit) {

        if (IsBlockSolid(uint(blockIndex))) {
            // entry point nudged forward, then clamped into the block (guards the epsilon drift)
            float3 pos = ray.origin + ray.direction * (blockTCurrent + 0.01f);
            int3 blockMin = blockCell * int(BLOCK_SIZE);
            int3 fineCell = clamp(int3(floor(pos)), blockMin, blockMin + int(BLOCK_SIZE) - 1);

            float tCurrent = blockTCurrent;
            float3 tMax = select(ray.direction != 0.0f, (float3(fineCell) + stepSign - ray.origin) * invDir, FLT_INF);
            int voxelIndex = fineCell.x + fineCell.y * pc._voxel_grid_size.x + fineCell.z * pc._voxel_grid_size.x * pc._voxel_grid_size.y;

            float blockBoundary = min(min(blockTMax.x, blockTMax.y), blockTMax.z) + 0.01f;
            float blockExit = min(blockBoundary, tExit);

            while (tCurrent < blockExit) {
                if (IsVoxelSolid(uint(voxelIndex))) {
                    hit.mat_type = ReadVoxel(fineCell);
                    hit.dist = tCurrent;
                    hit.normal = normal;
                    hit.end = ray.origin + ray.direction * tCurrent;
                    return hit;
                }

                float3 mask = DDAStepMask(tMax);
                tCurrent = dot(mask, tMax);
                tMax += mask * tDelta;
                normal = -mask * float3(istep);
                voxelIndex += int(dot(mask, float3(idxStep)));
                fineCell += int3(mask) * istep;
            }
        }

        float3 blockMask = DDAStepMask(blockTMax);
        blockTCurrent = dot(blockMask, blockTMax);
        blockTMax += blockMask * blockTDelta;
        normal = -blockMask * float3(istep);

        blockCell += int3(blockMask) * istep;
        if (any(blockCell < 0) || any(blockCell >= blockGridSize)) {
            break;
        }
        blockIndex += int(dot(blockMask, float3(blockIdxStep)));
    }
    return hit;
}

// ray is transformed into the entity's local axis-aligned frame (translate to pivot at the
// grid's base center, then rotate by -yaw about Y), AABB-rejected, then standard fine DDA
Hit CastEntityRay(Ray ray) {
    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);
    hit.isEntity = false;

    int3 dim = pc.entity_dim;
    if (dim.x == 0) {
        return hit;
    }

    float s, c;
    sincos(-pc.entity_yaw, s, c);

    float3 rel = ray.origin - pc.entity_pos;
    float3 rotatedRel = float3(c * rel.x - s * rel.z, rel.y, s * rel.x + c * rel.z);
    float3 rotatedDir = float3(c * ray.direction.x - s * ray.direction.z, ray.direction.y, s * ray.direction.x + c * ray.direction.z);
    float3 lo = rotatedRel / pc.entity_scale + float3(dim.x * 0.5f, 0.0f, dim.z * 0.5f);
    float3 ld = rotatedDir / pc.entity_scale;

    float3 invD = rcp(ld);
    float3 t0 = (0.0f - lo) * invD;
    float3 t1 = (float3(dim) - lo) * invD;
    float3 tmin3 = min(t0, t1);
    float3 tmax3 = max(t0, t1);
    float tEnter = max(max(tmin3.x, tmin3.y), max(tmin3.z, 0.0f));
    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z) - 0.001f;
    if (tExit <= tEnter) {
        return hit;
    }

    int3 istep = int3(sign(ld));
    float3 tDelta = abs(invD);
    float3 stepSign = max(sign(ld), 0.0f);

    float3 p = lo + ld * (tEnter + 0.001f);
    int3 cell = clamp(int3(floor(p)), int3(0, 0, 0), dim - 1);
    float3 tMax = select(ld != 0.0f, (float3(cell) + stepSign - lo) * invD, FLT_INF);
    float tCurrent = tEnter;
    float3 entryMask = select(tmin3 == tEnter, 1.0f, 0.0f);
    float3 lnormal = -entryMask * float3(istep);

    while (tCurrent < tExit) {
        uint idx = uint(cell.x + cell.y * dim.x + cell.z * dim.x * dim.y);
        uint mat = ReadEntityVoxel(idx);
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
        if (any(cell < 0) || any(cell >= dim)) {
            break;
        }
    }
    return hit;
}

Hit CastRay(Ray ray, bool testEntity) {
    Hit worldHit = CastVoxelRay(ray);
    if (!testEntity) {
        return worldHit;
    }
    Hit entityHit = CastEntityRay(ray);
    if (entityHit.dist < worldHit.dist) {
        return entityHit;
    }
    return worldHit;
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
    uint rngState = pixelIndex + pc.frame_idx * 719393;

    float3 totalLight = 0.0f;
    uint bonus = 0;

    for (uint sample = 0; sample < RAYS_PER_PIXEL; sample++) {
        float jitterX = RandomValue(rngState) - 0.5f;
        float jitterY = RandomValue(rngState) - 0.5f;
        float x = (float(pixel.x) + 0.5f + jitterX) / float(pc._screen_size.x);
        float y = (float(pixel.y) + 0.5f + jitterY) / float(pc._screen_size.y);

        Ray r = CreateCameraRay(x, y);
        for (uint bounces = 0; bounces < MAX_BOUNCES + bonus; bounces++) {
            Hit hit = CastRay(r, bounces != 0);
            if (hit.mat_type != 0) {
                Material mat = materials[hit.mat_type];
                if (hit.isEntity) { mat = entityMaterials[hit.mat_type]; }

                //shadow ray
                if (bounces == 0 || (bonus && bounces == 1)) { //only do on first ray or first spec ray
                    Ray shadow = CreateRay(hit.end + (hit.normal * 0.01f), pc.sun_direction);
                    Hit shadowHit = CastRay(shadow, true);

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
                bonus = min(bonus + (uint)isSpecular, 3u);

                float3 diffuseDir = normalize(hit.normal + RandomDirection(rngState));
                float3 specularDir = Reflect(r.direction, hit.normal);

                r.direction = normalize(lerp(diffuseDir, specularDir, mat.smoothness * isSpecular));

                r.incomingLight += AmbientLight * r.color * (!isSpecular); //so specular doesnt add to ambient light
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

/*
    outputImage[pixel] = float4(
        r.direction.x > 0 ? r.direction.x : 0.0,
        r.direction.y > 0 ? r.direction.y : 0.0,
        r.direction.z > 0 ? r.direction.z : 0.0,
        1.0f
    );

*/