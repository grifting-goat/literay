#ifndef COMPUTE_RES_H
#define COMPUTE_RES_H

#include "stdint.h"

typedef struct {
    float color[4];
    float emissionColor[4];
    float emissionStrength;
    float smoothness;
    float specularProbability;
    float noise;

    float opaque;
    float _pad0[3];
} Material;

typedef struct {
    float camera_position[4];
    float camera_forward[4];
    float camera_right[4];
    float camera_up[4];

    float tan_fov_v;
	float tan_fov_h;
} CameraData;

typedef struct {
    float position[4];
    float rotation[4];

    // conservative world-space AABB (valid under any yaw), precomputed on the CPU
    // so the shader doesn't redo this math on every CastDynamicRay call
    float worldMin[4];
    float worldMax[4];

    float scale;
    uint32_t modelIdx;
    float _pad0[2]; // rounds struct to 80 bytes, matching the HLSL cbuffer's 16-byte rounding
} EntityData;


typedef struct {
    uint32_t voxelOffset;
    uint32_t axes; // byte0 = up_axis, byte1 = cardinal_axis
    uint32_t dimensions[3];
    uint32_t size;
} GpuModel;

typedef struct {
    int _screen_size[2];
    unsigned int frame_idx;
    unsigned int accumCount; 

    int _voxel_grid_size[3];
    float _pad; 

    float sun_direction[3];
} PushConstants;


#endif //COMPUTE_RES_H