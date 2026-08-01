#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vector.h"


#define CHUNK_DIM 32
#define CHUNK_SIZE CHUNK_DIM * CHUNK_DIM * CHUNK_DIM


typedef enum {

    MIRROR_PLANE = 0,
    VARNISH_PLANE,
    LIGHT_CUBE,
    MIRROR_BALL,
    RUBY_BALL,
    CASTLE,
    STATUE,
    BOAT,
    TREE,
    SHIMMER,
    CUTE,
    OUT,
    STRUCTURE_COUNT

} StructureTypes;

typedef struct {

    uint8_t* voxels;
    uint32_t dimensions[3];
    uint32_t size;
    
} Structure_t;

static Structure_t structure_list[STRUCTURE_COUNT];

void structure_list_create(); //programaticly define structures


//32^3 chunk size
//8 ^3 brick size
typedef struct {
    uVector_t coord;
    uint64_t brickMask;
    bool dirty;
    bool loaded;

    uint8_t* voxels;
    uint32_t* voxelMask;
} Chunk;

typedef struct {
    uVector_t  key;
    Chunk value;
} ChunkMapEntry;


typedef struct {

    double scale;
	double amplitude;
	uint32_t octaves;
    uint32_t sea_level;

} PerlinParams;

typedef struct {

    uint32_t seed;
    PerlinParams perlin_params;
    ChunkMapEntry* chunk_map;

} World;

World world_create(uint32_t seed);

void world_load_spawn_chunk(World* wrld);

//void world_generate_terrain(World* wrld);


//void world_generate_structures(World* wrld);


//void world_structure_place(World* wrld, StructureTypes type, uint32_t origin[3], uint32_t normal, uint32_t rotation, float scale, bool overwrite);

void world_destroy(World* wrld);

#endif //WORLD_H