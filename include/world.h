#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vector.h"
#include "queue.h"


#define CHUNK_DIM 32
#define CHUNK_SIZE CHUNK_DIM * CHUNK_DIM * CHUNK_DIM

#define CHUNK_COORD_LIMIT 16
#define CHUNK_STREAM_WINDOW (2 * CHUNK_COORD_LIMIT) // GPU addressing wraps every this-many chunks, per axis

#define CHUNK_QUEUE_SIZE 32


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
    iVector_t coord;
    uint64_t brickMask;
    uint64_t brickDirtyMask;
    bool loaded;

    uint8_t* voxels;
    uint32_t* voxelMask;
} Chunk;

typedef struct {
    iVector_t  key;
    Chunk value;
} ChunkMapEntry;

typedef struct {
    iVector_t key;
    bool value;
} PendingMapEntry; // set of coords already sitting in chunk_load_queue or chunk_rmf_queue


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
    PendingMapEntry* pending_map;

    Queue_t chunk_load_queue;
    Queue_t chunk_rmf_queue;

} World;

World world_create(uint32_t seed);

void world_load_spawn_chunk(World* wrld);


int world_chunk_load(World* wrld, iVector_t chunkCoords);

void world_chunk_memory_queue(World* wrld, Vector_t* cameraPos);

int world_chunk_test(World* wrld, Vector_t* cameraPos); // returns the chunk_map index or -1
int world_chunk_untest(World* wrld, Vector_t* cameraPos); // returns the chunk_map index of an out-of-range chunk, or -1
void world_chunk_unload_by_index(World* wrld, int idx);
int world_chunk_find_collision(World* wrld, iVector_t coord, int excludeIdx); // returns the chunk_map index of a same-slot chunk, or -1

//void world_generate_terrain(World* wrld);

//void world_generate_structures(World* wrld);

//void world_structure_place(World* wrld, StructureTypes type, uint32_t origin[3], uint32_t normal, uint32_t rotation, float scale, bool overwrite);

void world_destroy(World* wrld);

#endif //WORLD_H