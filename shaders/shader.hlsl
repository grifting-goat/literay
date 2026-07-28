[[vk::binding(1, 0)]] Texture3D<uint> voxelsIn; // material ids, R8_UINT
[[vk::binding(6, 0)]] ByteAddressBuffer voxelsMaskIn;
[[vk::binding(7, 0)]] ByteAddressBuffer voxelsOccIn; // 1 bit per voxel

struct Material {
    float4 color;
    float4 emissionColor;
    float emissionStrength;
    float smoothness;
    float specularProbability;
    float noise;
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
static const uint MAX_BONUS_BOUNCES = 4;

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

// occupancy bit 
bool IsVoxelSolid(uint index) {
    uint word = voxelsOccIn.Load((index >> 5) << 2);
    uint bit = index & 31u;
    return ((word >> bit) & 1u) != 0u;
}

uint ReadVoxel(int3 cell) {
    return voxelsIn.Load(int4(cell, 0));
}


float3 DDAStepMask(float3 tMax) {
    float3 mask;
    mask.x = (tMax.x <= tMax.y) && (tMax.x <= tMax.z);
    mask.y = (tMax.y <  tMax.x) && (tMax.y <= tMax.z);
    mask.z = 1.0f - mask.x - mask.y;
    return mask;
} //goofy optimization fable said to do


//https://github.com/SebLague/Ray-Tracing
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
/*
//standard dda voxel
Hit CastStaticRay(Ray ray) {
    
    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);

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


    int3 blockCell = cell / int(BLOCK_SIZE);
    float3 blockNextBoundary = float3(blockCell) * BLOCK_SIZE + stepSign * BLOCK_SIZE;
    float3 blockTMax = select(ray.direction != 0.0f, (blockNextBoundary - ray.origin) * invDir, FLT_INF);
    float3 blockTDelta = tDelta * BLOCK_SIZE;
    int blockIndex = blockCell.x + blockCell.y * blockGridSize.x + blockCell.z * blockGridSize.x * blockGridSize.y;
    float blockTCurrent = 0.0f;
    float3 normal = float3(0.0f, 0.0f, 0.0f);

    while (blockTCurrent < tExit) {

        if (IsBlockSolid(uint(blockIndex))) {
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

//intersect with non-grid aligned voxels, bounding boxes with own coord system

Hit CastDynamicRay(Ray ray) {

    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);

    

}

Hit NewCastRay(Ray ray) {
    Hit staticHit = CastStaticRay(ray);
    Hit dynamicHit = CastDynamicRay(ray);

    if (staticHit.dist > dynamicHit.dist) {
        return dynamicHit;
    }
    return staticHit;

}*/

Hit CastRay(Ray ray) {

    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.end = float3(0.0f, 0.0f, 0.0f);
    hit.normal = float3(0.0f, 0.0f, 0.0f);

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


    int3 blockCell = cell / int(BLOCK_SIZE);
    float3 blockNextBoundary = float3(blockCell) * BLOCK_SIZE + stepSign * BLOCK_SIZE;
    float3 blockTMax = select(ray.direction != 0.0f, (blockNextBoundary - ray.origin) * invDir, FLT_INF);
    float3 blockTDelta = tDelta * BLOCK_SIZE;
    int blockIndex = blockCell.x + blockCell.y * blockGridSize.x + blockCell.z * blockGridSize.x * blockGridSize.y;
    float blockTCurrent = 0.0f;
    float3 normal = float3(0.0f, 0.0f, 0.0f);

    while (blockTCurrent < tExit) {

        if (IsBlockSolid(uint(blockIndex))) {
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
            Hit hit = CastRay(r);
            if (hit.mat_type != 0) {
                Material mat = materials[hit.mat_type];

                //shadow ray
                if (!bounces || bonus == 1) { //keep shdows in mirrors
                    Ray shadow = CreateRay(hit.end + (hit.normal * 0.01f), pc.sun_direction);
                    Hit shadowHit = CastRay(shadow);

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

/*
    outputImage[pixel] = float4(
        r.direction.x > 0 ? r.direction.x : 0.0,
        r.direction.y > 0 ? r.direction.y : 0.0,
        r.direction.z > 0 ? r.direction.z : 0.0,
        1.0f
    );

*/