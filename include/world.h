#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vector.h"
#include "queue.h"
#include "chunk_threads.h"

#include "model.h"


#define CHUNK_DIM 32
#define CHUNK_SIZE CHUNK_DIM * CHUNK_DIM * CHUNK_DIM

#define WORLD_HEIGHT_LIMIT 512
#define WORLD_CHUNK_Y_MIN 0
#define WORLD_CHUNK_Y_MAX (WORLD_HEIGHT_LIMIT / CHUNK_DIM)

#define CHUNK_STREAM_RADIUS_XZ 32
#define CHUNK_STREAM_WINDOW_XZ (2 * CHUNK_STREAM_RADIUS_XZ)


#define CHUNK_STREAM_WINDOW_Y  (WORLD_CHUNK_Y_MAX - WORLD_CHUNK_Y_MIN)
#define CHUNK_STREAM_RADIUS_Y  (CHUNK_STREAM_WINDOW_Y / 2)
#define CHUNK_STREAM_CENTER_Y  (WORLD_CHUNK_Y_MIN + CHUNK_STREAM_RADIUS_Y)

#define CHUNK_QUEUE_SIZE 64

#define CHUNK_SLOT_GRID_COUNT (CHUNK_STREAM_WINDOW_XZ * CHUNK_STREAM_WINDOW_Y * CHUNK_STREAM_WINDOW_XZ)


#define REGION_DIM_XZ 1024
#define REGION_DIM_SHIFT 10 // log2(1024) voxel -> region shift
#define REGION_CHUNK_SHIFT 5 

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
    Model_t model;
    uVector_t region_coords;
} Structure_t;

void structure_list_create(Structure_t* structure_list); //programaticly define structures



typedef struct {
    iVector_t  key;
    Chunk value;
} ChunkMapEntry;

typedef struct {
    bool occupied;
    iVector_t coord;
} SlotGridEntry;


//regions are structure placement areas
//each chunk does an AABB test with every stucture in a region
//then it checks if overlaps with a structures voxels and updates the chunks
typedef struct {
    Structure_t* structures;
    uint32_t structure_count;
} Region;

typedef struct {
    iVector_t key; //region -> y is ignored
    Region value;
} RegionMapEntry;


typedef struct {

    double scale;
	double amplitude;
    double persistence;
    double lacunarity;
	uint32_t octaves;
    uint32_t sea_level;

    int perm[256];

} PerlinParams;

typedef struct {
    PerlinParams ymap;
    PerlinParams amplify;

    RegionMapEntry* regions;

    Structure_t structure_list[STRUCTURE_COUNT];

    uint32_t seed;

} WorldGenerator;

WorldGenerator world_generator_create(uint32_t seed);
void world_generator_free(WorldGenerator* gen);

Region region_create(WorldGenerator* gen, iVector_t region_coord);
void region_spawn_generator(WorldGenerator* wrldgen); //hardcode structure spawning around spawn

typedef struct {

    uint32_t seed;
    ChunkMapEntry* chunk_map;
    SlotGridEntry* slot_grid;

    iVector_t stream_center; // camera chunk (y fixed) the load/rmf queues were last built around

    Queue_t chunk_load_queue;
    Queue_t chunk_rmf_queue;

    WorldGenerator generator;

    ChunkThreadPool_t genPool;

} World;

World world_create(uint32_t seed);

void world_chunk_gen_workers_start(World* wrld);
void world_chunk_gen_workers_stop(World* wrld);
void world_chunk_gen_submit(World* wrld, iVector_t coord);
bool world_chunk_gen_poll(World* wrld, Chunk* outChunk);

int world_chunk_insert(World* wrld, Chunk* chunk);

void world_load_spawn_chunk(World* wrld);



int world_chunk_load(World* wrld, iVector_t chunkCoords);

void world_chunk_memory_queue(World* wrld, Vector_t* cameraPos);

void world_chunk_unload_by_index(World* wrld, int idx);
int world_chunk_find_collision(World* wrld, iVector_t coord, int excludeIdx); // returns the chunk_map index of a same-slot chunk, or -1

//void world_generate_terrain(World* wrld);

//void world_generate_structures(World* wrld);

//void world_structure_place(World* wrld, StructureTypes type, uint32_t origin[3], uint32_t normal, uint32_t rotation, float scale, bool overwrite);

void world_destroy(World* wrld);

void structure_manual_place(World* wrld, StructureTypes type, iVector_t origin, uint8_t rotation);

#endif //WORLD_H