#ifndef COMPUTE_RES_H
#define COMPUTE_RES_H

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
    int _screen_size[2];
    unsigned int frame_idx;
    unsigned int accumCount; // fills the slot int3 below needs padding for anyway; keeps CPU/GPU layouts matched

    int _voxel_grid_size[3];
    float _sun_dir_pad; // fills the slot sun_direction below needs padding for anyway, same trick as accumCount above

    float sun_direction[3];
    float entity_yaw;

    float entity_pos[3];
    float _entity_pad;

    int entity_dim[3]; // all zero = no entity
    float entity_scale;
} PushConstants;


#endif //COMPUTE_RES_H