[[vk::binding(1, 0)]] ByteAddressBuffer voxelsIn;

struct Material {
    float4 color;
    float4 emissionColor;
    float emissionStrength;
    float smoothness;
    float specularProbability;
};
[[vk::binding(2, 0)]] StructuredBuffer<Material> materials;

[[vk::binding(3, 0)]]
cbuffer CameraData {
    float4 camera_position;
    float4 camera_rotation;

    float tan_fov_v;
    float tan_fov_h;
};

[[vk::binding(4, 0)]]
[[vk::image_format("rgba8")]]
RWTexture2D<float4> outputImage;

struct PushConstants {
    int2 _screen_size;
    int pixel_count;
    uint voxelCount;

    int3 _voxel_grid_size;
    uint frame_idx;
};
[[vk::push_constant]] PushConstants pc;

static const float FLT_INF = asfloat(0x7F800000);
static const float MAX_RAY_DISTANCE = 100.0f;
static const uint MAX_BOUNCES = 2;
static const uint RAYS_PER_PIXEL = 2;

static const bool EnvironmentEnabled = true;
static const float3 SkyColourHorizon = float3(0.8f, 0.9f, 1.0f);
static const float3 SkyColourZenith = float3(0.2f, 0.4f, 0.9f);
static const float3 SunDirection = float3(0.318f, 0.848f, 0.424f);
static const float SunFocus = 500.0f;
static const float SunIntensity = 5.0f;

static const float3 AmbientLight = float3(0.005f, 0.005f, 0.005f);

struct Ray {
    float3 origin;
    float3 direction;

    float4 color;
    float4 incomingLight;

    float dist0;
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
    return NextRandom(state) / 4294967295.0; // 2^32 - 1
}

// Random value in normal distribution (with mean=0 and sd=1)
float RandomValueNormalDistribution(inout uint state) {
    float theta = 2 * 3.1415926 * RandomValue(state);
    float rho = sqrt(-2 * log(RandomValue(state)));
    return rho * cos(theta);
}

// Calculate a random direction
float3 RandomDirection(inout uint state) {

    float x = RandomValueNormalDistribution(state);
    float y = RandomValueNormalDistribution(state);
    float z = RandomValueNormalDistribution(state);
    return normalize(float3(x, y, z));
}

Ray CreateRay(float3 origin, float3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = normalize(direction);
    ray.color = float4(1.0f,1.0f,1.0f,1.0f);
    ray.incomingLight = 0.0f;
    ray.dist0 = 0.0f;
    return ray;
}

Ray CreateCameraRay(float x, float y) {
    x = (x - 0.5f) * 2.0f;
    y = (0.5f - y) * 2.0f;

    float pitch = camera_rotation.x;
    float yaw = camera_rotation.y;

    // Minecraft-style yaw/pitch: +Y up, yaw 0 looks toward +Z, pitch>0 looks down.
    float3 forward = float3(-sin(yaw) * cos(pitch), -sin(pitch), cos(yaw) * cos(pitch));
    float3 right = float3(-cos(yaw), 0.0f, -sin(yaw));
    float3 up = cross(right, forward);

    float3 direction = forward + x * tan_fov_h * right + y * tan_fov_v * up;

    return CreateRay(camera_position.xyz, direction);
}

uint ReadVoxel(uint index) {
    uint word = voxelsIn.Load(index & ~3u);
    uint shift = (index & 3u) * 8u;
    return (word >> shift) & 0xFFu;
}

//https://github.com/SebLague/Ray-Tracing
float4 GetEnvironmentLight(Ray ray) {

    if (!EnvironmentEnabled) {
        return 0;
    }

    if (ray.direction.y <= 0.0f) {
        float voidT = smoothstep(-0.4f, 0.0f, ray.direction.y);
        return float4(SkyColourHorizon * voidT, 1.0f);
    }

    float skyGradientT = pow(smoothstep(0, 0.4, ray.direction.y), 0.35);
    float3 skyGradient = lerp(SkyColourHorizon, SkyColourZenith, skyGradientT);
    float sun = pow(max(0, dot(ray.direction, SunDirection)), SunFocus) * SunIntensity;

    float3 composite = skyGradient + sun;
    return float4(composite, 1.0f);
}

Hit CastRay(Ray ray) {

    int x = int(ray.origin.x);
    int y = int(ray.origin.y);
    int z = int(ray.origin.z);

    int stepX = (ray.direction.x > 0) ? 1 : ((ray.direction.x < 0) ? -1 : 0);
    int stepY = (ray.direction.y > 0) ? 1 : ((ray.direction.y < 0) ? -1 : 0);
    int stepZ = (ray.direction.z > 0) ? 1 : ((ray.direction.z < 0) ? -1 : 0);

    float tDeltaX = (ray.direction.x != 0) ? abs(1.0f / ray.direction.x) : FLT_INF;
    float tDeltaY = (ray.direction.y != 0) ? abs(1.0f / ray.direction.y) : FLT_INF;
    float tDeltaZ = (ray.direction.z != 0) ? abs(1.0f / ray.direction.z) : FLT_INF;

    float nextBoundaryX = (stepX > 0) ? floor(ray.origin.x) + 1.0f : floor(ray.origin.x);
    float nextBoundaryY = (stepY > 0) ? floor(ray.origin.y) + 1.0f : floor(ray.origin.y);
    float nextBoundaryZ = (stepZ > 0) ? floor(ray.origin.z) + 1.0f : floor(ray.origin.z);

    float tMaxX = (ray.direction.x != 0) ? (nextBoundaryX - ray.origin.x) / ray.direction.x : FLT_INF;
    float tMaxY = (ray.direction.y != 0) ? (nextBoundaryY - ray.origin.y) / ray.direction.y : FLT_INF;
    float tMaxZ = (ray.direction.z != 0) ? (nextBoundaryZ - ray.origin.z) / ray.direction.z : FLT_INF;

    Hit hit;
    hit.mat_type = 0;
    hit.dist = FLT_INF;
    hit.normal = float3(0.0f, 0.0f, 0.0f);

    if (x < 0 || x >= pc._voxel_grid_size.x ||
        y < 0 || y >= pc._voxel_grid_size.y ||
        z < 0 || z >= pc._voxel_grid_size.z) {
        return hit;
    }

    float3 normal = float3(0.0f, 0.0f, 0.0f);
    float tCurrent = 0.0f;

    while (hit.mat_type == 0) {
        if (tCurrent > MAX_RAY_DISTANCE) {
            return hit;
        }

        uint voxelIndex = uint(x) + uint(y) * uint(pc._voxel_grid_size.x) + uint(z) * uint(pc._voxel_grid_size.x) * uint(pc._voxel_grid_size.y);

        hit.mat_type = int(ReadVoxel(voxelIndex));
        if (hit.mat_type != 0) {
            hit.dist = tCurrent;
            hit.normal = normal;
            hit.end = ray.origin + ray.direction * tCurrent;
            break;
        }

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                tCurrent = tMaxX;
                x = x + stepX;
                tMaxX = tMaxX + tDeltaX;
                if (x < 0 || x >= pc._voxel_grid_size.x) { return hit; }
                normal = float3(-stepX, 0.0f, 0.0f);
            }
            else {
                tCurrent = tMaxZ;
                z = z + stepZ;
                tMaxZ = tMaxZ + tDeltaZ;
                if (z < 0 || z >= pc._voxel_grid_size.z) { return hit; }
                normal = float3(0.0f, 0.0f, -stepZ);
            }
        }
        else {
            if (tMaxY < tMaxZ) {
                tCurrent = tMaxY;
                y = y + stepY;
                tMaxY = tMaxY + tDeltaY;
                if (y < 0 || y >= pc._voxel_grid_size.y) { return hit; }
                normal = float3(0.0f, -stepY, 0.0f);
            }
            else {
                tCurrent = tMaxZ;
                z = z + stepZ;
                tMaxZ = tMaxZ + tDeltaZ;
                if (z < 0 || z >= pc._voxel_grid_size.z) { return hit; }
                normal = float3(0.0f, 0.0f, -stepZ);
            }
        }
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
    uint rngState = pixelIndex + pc.frame_idx * 719393;

    float4 totalLight = 0.0f;

    for (uint sample = 0; sample < RAYS_PER_PIXEL; sample++) {
        float jitterX = RandomValue(rngState) - 0.5f;
        float jitterY = RandomValue(rngState) - 0.5f;
        float x = (float(pixel.x) + 0.5f + jitterX) / float(pc._screen_size.x);
        float y = (float(pixel.y) + 0.5f + jitterY) / float(pc._screen_size.y);

        Ray r = CreateCameraRay(x, y);
        uint bounces;
        for (bounces = 0; bounces < MAX_BOUNCES; bounces++) {
            Hit hit = CastRay(r);
            if (hit.mat_type != 0) {

                r.color *= materials[hit.mat_type].color;
                float4 emittedLight = materials[hit.mat_type].emissionColor * materials[hit.mat_type].emissionStrength;
                r.incomingLight += emittedLight * r.color;

                r.origin = hit.end + hit.normal * 0.001f;

                bool isSpecular = materials[hit.mat_type].specularProbability >= RandomValue(rngState);

                float3 diffuseDir = normalize(hit.normal + RandomDirection(rngState));
                float3 specularDir = Reflect(r.direction, hit.normal);

                r.direction = normalize(lerp(diffuseDir, specularDir, materials[hit.mat_type].smoothness * isSpecular));


                if (!bounces) {
                    r.dist0 = hit.dist;
                    r.incomingLight += float4(AmbientLight, 1.0f) * r.color;
                }
            }
            else {
                r.incomingLight += GetEnvironmentLight(r) * r.color;
                break;
            }
        }

        totalLight += r.incomingLight;
    }

    //float shade = (bounces > 0) ? saturate(1.0f - (r.dist0 / MAX_RAY_DISTANCE)) : 1.0f;
    outputImage[pixel] = totalLight / float(RAYS_PER_PIXEL);

}

/*
    outputImage[pixel] = float4(
        r.direction.x > 0 ? r.direction.x : 0.0,
        r.direction.y > 0 ? r.direction.y : 0.0,
        r.direction.z > 0 ? r.direction.z : 0.0,
        1.0f
    );

*/