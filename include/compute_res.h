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
    float scale;

    uint32_t modelIdx;
} EntityData;

typedef struct {
    int _screen_size[2];
    unsigned int frame_idx;
    unsigned int accumCount; 

    int _voxel_grid_size[3];
    float _pad; 

    float sun_direction[3];
} PushConstants;


#endif //COMPUTE_RES_H