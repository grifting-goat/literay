#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <stdlib.h>

#include "vector.h"


typedef enum {
    MIRROR_WAll = 0,
    VARNISH_WALL

} StructureTypes;

typedef struct {

    uint8_t* voxels;
    Vector_t dimensions;
    uint32_t size;
    
} Structure_t;


typedef struct {

    double scale;
	double amplitude;
	uint32_t octaves;

} PerlinParams;

typedef struct {

    uint32_t seed;
    PerlinParams perlin_params;

    uint8_t* voxels;
    uint32_t world_size;

    Vector_t world_dimensions;

} World;

World world_create(Vector_t dims);
void world_generate_terrain(World* wrld);

Structure_t create_structure();


#endif //WORLD_H