#include "model.h"
#include "vox_loader.h"

#include <math.h>
#include <string.h>


static Model_t create_cube_model(uint32_t dim[3], uint8_t material);
static Model_t create_plane_model(uint32_t width, uint32_t depth, uint8_t material);
static Model_t create_sphere_model(uint32_t diameter, uint8_t material);



static Model_t create_cube_model(uint32_t dim[3], uint8_t material) {
    uint32_t size = dim[0] * dim[1] * dim[2];

    Model_t model = {
        .size = size,
        .voxels = calloc(size, sizeof(uint8_t)),
        .up_axis = 0,
        .cardinal_axis = 0
    };
    model.dimensions = uVector_create(dim[0], dim[1], dim[2]);
    memset(model.voxels, material, size);

    return model;
}

static Model_t create_plane_model(uint32_t width, uint32_t depth, uint8_t material) {
    uint32_t dim[3] = {width, 1, depth};
    return create_cube_model(dim, material);
}

static Model_t create_sphere_model(uint32_t diameter, uint8_t material) {
    uint32_t dim[3] = {diameter, diameter, diameter};
    uint32_t voxel_count = dim[0] * dim[1] * dim[2];

    Model_t model = {
        .size = voxel_count,
        .voxels = calloc(voxel_count, sizeof(uint8_t)),
        .up_axis = 0,
        .cardinal_axis = 0
    };
    model.dimensions = uVector_create(dim[0], dim[1], dim[2]);

    float radius = diameter / 2.0f;
    float center = radius - 0.5f;

    for (uint32_t z = 0; z < diameter; z++) {
        for (uint32_t y = 0; y < diameter; y++) {
            for (uint32_t x = 0; x < diameter; x++) {
                float dx = (float)x - center;
                float dy = (float)y - center;
                float dz = (float)z - center;
                if (dx * dx + dy * dy + dz * dz <= radius * radius) {
                    uint32_t idx = x + y * dim[0] + z * dim[0] * dim[1];
                    model.voxels[idx] = material;
                }
            }
        }
    }

    return model;
}

Model_t model_create_prefab(ModelPrefabs prefab_type, uint8_t up_axis, uint8_t cardinal_axis, void* param1, void* param2, void* param3) {
    Model_t model = {0};

    switch (prefab_type) {
        case SPHERE: {
            uint32_t diameter = *(uint32_t*)param1;
            uint8_t material = *(uint8_t*)param2;
            model = create_sphere_model(diameter, material);
            break;
        }
        case PLANE: {
            uint32_t width = *(uint32_t*)param1;
            uint32_t depth = *(uint32_t*)param2;
            uint8_t material = *(uint8_t*)param3;
            model = create_plane_model(width, depth, material);
            break;
        }
        case CUBE: {
            uint32_t* dim = (uint32_t*)param1;
            uint8_t material = *(uint8_t*)param2;
            model = create_cube_model(dim, material);
            break;
        }
    }

    model.up_axis = up_axis;
    model.cardinal_axis = cardinal_axis;
    return model;
}


Model_t model_create_from_vox(const char* path, uint8_t up_axis, uint8_t cardinal_axis, MaterialTypes material, bool colorMatch) {
    return vox_load_model(path, up_axis, cardinal_axis, material, colorMatch);
}

void model_free(Model_t* model) {
    free(model->voxels);
    model->voxels = NULL;
    model->size = 0;
}


/*
baked models ops

*/

void model_scale_baked(Model_t* model, float scale) {
    uint32_t dim[3] = { model->dimensions.x, model->dimensions.y, model->dimensions.z };
    uint32_t new_dim[3] = {
        (uint32_t)ceilf(dim[0] * scale),
        (uint32_t)ceilf(dim[1] * scale),
        (uint32_t)ceilf(dim[2] * scale)
    };
    uint32_t new_size = new_dim[0] * new_dim[1] * new_dim[2];
    uint8_t* new_voxels = calloc(new_size, sizeof(uint8_t));

    for (uint32_t z = 0; z < new_dim[2]; z++) {
        for (uint32_t y = 0; y < new_dim[1]; y++) {
            for (uint32_t x = 0; x < new_dim[0]; x++) {
                uint32_t src[3] = {
                    (uint32_t)(x / scale),
                    (uint32_t)(y / scale),
                    (uint32_t)(z / scale)
                };
                if (src[0] >= dim[0]) { src[0] = dim[0] - 1; }
                if (src[1] >= dim[1]) { src[1] = dim[1] - 1; }
                if (src[2] >= dim[2]) { src[2] = dim[2] - 1; }

                uint32_t src_idx = src[0] + src[1] * dim[0] + src[2] * dim[0] * dim[1];
                uint32_t dst_idx = x + y * new_dim[0] + z * new_dim[0] * new_dim[1];
                new_voxels[dst_idx] = model->voxels[src_idx];
            }
        }
    }

    free(model->voxels);
    model->voxels = new_voxels;
    model->size = new_size;
    model->dimensions = uVector_create(new_dim[0], new_dim[1], new_dim[2]);
}

void model_rotate_baked(Model_t* model, uint8_t up_axis, uint8_t cardinal_axis) {
    static const int normalAxis[6] = {1, 1, 0, 0, 2, 2};
    static const int normalSign[6] = {1, -1, 1, -1, 1, -1};

    int axis[3];
    int sign[3];

    int nAxis = normalAxis[up_axis % 6];
    axis[nAxis] = 1;
    sign[nAxis] = normalSign[up_axis % 6];

    int sideModelAxis[2];
    int sideCount = 0;
    for (int w = 0; w < 3; w++) {
        if (w != nAxis) { sideModelAxis[sideCount++] = w; }
    }

    static const int spinLocalAxis[4][2] = { {0, 2}, {2, 0}, {0, 2}, {2, 0} };
    static const int spinSign[4][2] = { {1, 1}, {-1, 1}, {-1, -1}, {1, -1} };
    const int* sla = spinLocalAxis[cardinal_axis % 4];
    const int* ss = spinSign[cardinal_axis % 4];

    axis[sideModelAxis[0]] = sla[0];
    sign[sideModelAxis[0]] = ss[0];
    axis[sideModelAxis[1]] = sla[1];
    sign[sideModelAxis[1]] = ss[1];

    uint32_t dim[3] = { model->dimensions.x, model->dimensions.y, model->dimensions.z };
    uint32_t new_dim[3] = { dim[axis[0]], dim[axis[1]], dim[axis[2]] };
    uint32_t new_size = new_dim[0] * new_dim[1] * new_dim[2];
    uint8_t* new_voxels = calloc(new_size, sizeof(uint8_t));

    for (uint32_t x = 0; x < new_dim[0]; x++) {
        for (uint32_t y = 0; y < new_dim[1]; y++) {
            for (uint32_t z = 0; z < new_dim[2]; z++) {
                uint32_t local[3] = {x, y, z};

                uint32_t src[3];
                for (int w = 0; w < 3; w++) {
                    uint32_t axisDim = dim[axis[w]];
                    src[axis[w]] = (sign[w] > 0) ? local[w] : (axisDim - 1 - local[w]);
                }

                uint32_t new_idx = x + y * new_dim[0] + z * new_dim[0] * new_dim[1];
                uint32_t src_idx = src[0] + src[1] * dim[0] + src[2] * dim[0] * dim[1];
                new_voxels[new_idx] = model->voxels[src_idx];
            }
        }
    }

    free(model->voxels);
    model->voxels = new_voxels;
    model->size = new_size;
    model->dimensions = uVector_create(new_dim[0], new_dim[1], new_dim[2]);
    model->up_axis = up_axis;
    model->cardinal_axis = cardinal_axis;
}


Model_t model_instance_list[16] = {0};