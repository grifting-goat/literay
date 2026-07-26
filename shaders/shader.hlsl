[[vk::binding(1, 0)]] ByteAddressBuffer voxelsIn;

struct Material {
    float4 color;
    float4 emmisonColor;
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
};
[[vk::push_constant]] PushConstants pc;

static const float FLT_INF = asfloat(0x7F800000);
static const float MAX_RAY_DISTANCE = 200.0f;
static const uint MAX_BOUNCES = 1;

struct Ray {
    float3 origin;
    float3 direction;
    float4 color;

    float dist0;
    uint bounces;
};

struct Hit {
    float dist;
    float3 normal;
    uint mat_type;
};


float3 RotateVector(float3 vect, float3 rotation) {
    float xr = rotation.x;
    float yr = rotation.y;
    float zr = rotation.z;

    float3x3 Rx = float3x3(
        1,  0,  0,
        0,  cos(xr),    -sin(xr),
        0,  sin(xr),    cos(xr)
    );

    float3x3 Ry = float3x3(
        cos(yr),    0,    sin(yr),
        0,  1,  0,
        -sin(yr),   0,   cos(yr)
    );

    float3x3 Rz = float3x3(
        cos(zr), -sin(zr),  0,
        sin(zr),  cos(zr),  0,
        0,  0,  1
    );

    return mul(mul(mul(Ry, Rx), Rz), vect);
}

Ray CreateRay(float3 origin, float3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = normalize(direction);
    ray.color = float4(1.0f,1.0f,1.0f,1.0f);
    ray.dist0 = 0.0f;
    ray.bounces = 0;
    return ray;
}

Ray CreateCameraRay(float x, float y) {
    x = (x - 0.5f) * 2.0f;
    y = (y - 0.5f) * 2.0f;

    float3 plane = float3(x * tan_fov_h, y * tan_fov_v, 1.0f);
    float3 direction = RotateVector(normalize(plane), camera_rotation.xyz);

    return CreateRay(camera_position.xyz, direction);
}

uint ReadVoxel(uint index) {
    uint word = voxelsIn.Load(index & ~3u);
    uint shift = (index & 3u) * 8u;
    return (word >> shift) & 0xFFu;
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




[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= pc._screen_size.x || pixel.y >= pc._screen_size.y) {
        return;
    }

    float x = (float(pixel.x) + 0.5f) / float(pc._screen_size.x);
    float y = (float(pixel.y) + 0.5f) / float(pc._screen_size.y);

    Ray r = CreateCameraRay(x, y);

    while(r.bounces < MAX_BOUNCES) {
        Hit hit = CastRay(r);
        if (hit.mat_type != 0) {
            r.color = r.color * materials[hit.mat_type].color;
            r.dist0 = hit.dist;
            r.bounces = r.bounces + 1;
        }
        else {
            break;
        }
    }

    float shade = r.dist0 / 50.0f;
    outputImage[pixel] = r.color * float4(shade, shade, shade, 1.0f);

}

/*
    outputImage[pixel] = float4(
        r.direction.x > 0 ? r.direction.x : 0.0,
        r.direction.y > 0 ? r.direction.y : 0.0,
        r.direction.z > 0 ? r.direction.z : 0.0,
        1.0f
    );

*/