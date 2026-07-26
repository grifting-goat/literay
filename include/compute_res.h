#ifndef COMPUTE_RES_H
#define COMPUTE_RES_H

typedef struct {
    float color[4];
    float emissionColor[4];
    float emissionStrength;
    float smoothness;
    float specularProbability;
    float _pad[1]; // HLSL rounds StructuredBuffer element stride up to a multiple of 16 because of the float4 members; this keeps the CPU struct's size (and therefore materials[i] offsets) matching the GPU's.
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
    int _screen_size[2];
    unsigned int frame_idx;
    unsigned int accumCount; // fills the slot int3 below needs padding for anyway; keeps CPU/GPU layouts matched

    int _voxel_grid_size[3];
} PushConstants;


#endif //COMPUTE_RES_H