#ifndef COMPUTE_RES_H
#define COMPUTE_RES_H

typedef struct {
    float color[4];
    float emmisonColor[4];
} Material;

typedef struct {
    float camera_position[4];
	float camera_rotation[4];

    float tan_fov_v;
	float tan_fov_h;
} CameraData;

typedef struct {
    int _screen_size[2];
    int pixel_count;
    unsigned int voxelCount;

    int _voxel_grid_size[3];
} PushConstants;


#endif //COMPUTE_RES_H