#ifndef MODEL_H
#define MODEL_H


#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "material.h"
#include "vector.h"

typedef struct {

    uint8_t* voxels;

    uint8_t up_axis; //which axis is up
    uint8_t cardinal_axis; //facing NORTH (+Z) SOUTH, EAST (+X) WEST

    uVector_t dimensions;
    uint32_t size;

} Model_t;

typedef enum {
    SPHERE = 0,
    PLANE,
    CUBE
} ModelPrefabs;

extern Model_t model_instance_list[16];

Model_t model_create_prefab(ModelPrefabs prefab_type, uint8_t up_axis, uint8_t cardinal_axis, void* param1, void* param2, void* param3);

Model_t model_create_from_vox(const char* path, uint8_t up_axis, uint8_t cardinal_axis, MaterialTypes material, bool colorMatch); //color mathc ignores material (can be NULL)

Model_t model_create_from_custom(const char* path); //not implemented yet // custom file type

void model_free(Model_t* model);

void model_scale_baked(Model_t* model, float scale); //scale the model (this alters the voxel data so no takies backsies)
void model_rotate_baked(Model_t* model, uint8_t up_axis, uint8_t cardinal_axis);

#endif //MODEL_H
