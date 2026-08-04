#include "world.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "material.h"
#include "vox_loader.h"

#include "stb_ds.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

//move these elsewhere later
#define CHUNK_STREAM_COUNT (CHUNK_STREAM_WINDOW_XZ * CHUNK_STREAM_WINDOW_XZ * CHUNK_STREAM_WINDOW_Y)

void region_free(Region* r);
void chunk_region_structure_placements(WorldGenerator* gen, Chunk* chunk);

double smoothstep(double a, double b, double x) {
    x = (x - a) / (b - a);

    if (x < 0.0) x = 0.0;
    if (x > 1.0) x = 1.0;

    return x * x * (3.0 - 2.0 * x);
}

uint32_t xz_hash(uint32_t x, uint32_t z, uint32_t seed)  {
    uint32_t h = x * 374761393u + z * 668265263u + seed;
             h ^= h >> 13;
             h *= 1274126177u;
    return h;
}           

static int32_t wrap_axis(int32_t v, int32_t window) {
    int32_t m = v % window;
    return m < 0 ? m + window : m;
}

static iVector_t chunk_slot_key(iVector_t coord) { //combine this with slot_grid_index
    return iVector_create(
        wrap_axis(coord.x, CHUNK_STREAM_WINDOW_XZ),
        wrap_axis(coord.y, CHUNK_STREAM_WINDOW_Y),
        wrap_axis(coord.z, CHUNK_STREAM_WINDOW_XZ));
}

static bool ivec_eq(iVector_t a, iVector_t b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static uint32_t slot_grid_index(iVector_t wrapped) {
    return (uint32_t)wrapped.x + (uint32_t)wrapped.y * CHUNK_STREAM_WINDOW_XZ + (uint32_t)wrapped.z * CHUNK_STREAM_WINDOW_XZ * CHUNK_STREAM_WINDOW_Y;
}

typedef struct {
    int32_t dx, dy, dz;
    int32_t dist_sq;
} ChunkOffset;

static int chunk_offset_cmp(const void* a, const void* b) {
    int32_t da = ((const ChunkOffset*)a)->dist_sq;
    int32_t db = ((const ChunkOffset*)b)->dist_sq;
    return (da > db) - (da < db);
}

static ChunkOffset chunk_stream_offsets[CHUNK_STREAM_COUNT];
static bool chunk_stream_offsets_ready = false;

static void chunk_stream_offsets_load(void) {
    if (chunk_stream_offsets_ready) {return;}

    int i = 0;
    for (int32_t dz = -CHUNK_STREAM_RADIUS_XZ; dz < CHUNK_STREAM_RADIUS_XZ; dz++) {
        for (int32_t dy = -CHUNK_STREAM_RADIUS_Y; dy < CHUNK_STREAM_RADIUS_Y; dy++) {
            for (int32_t dx = -CHUNK_STREAM_RADIUS_XZ; dx < CHUNK_STREAM_RADIUS_XZ; dx++) {
                chunk_stream_offsets[i++] = (ChunkOffset){dx, dy, dz, dx*dx + dy*dy + dz*dz};
            }
        }
    }

    qsort(chunk_stream_offsets, CHUNK_STREAM_COUNT, sizeof(ChunkOffset), chunk_offset_cmp);
    chunk_stream_offsets_ready = true;
}


static double perlin_fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
static double perlin_lerp(double t, double a, double b) { return a + t * (b - a); }
static double perlin_grad(int hash, double x, double y) {
	int h = hash & 7;
	double u = h < 4 ? x : y;
	double v = h < 4 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
}

//void generate_terrain_heightmap(World* wrld);

Chunk chunk_create(World* wrld, iVector_t coord);
void chunk_generate_brick_map(Chunk* chunk);
void chunk_generate_occupancy_mask(Chunk* chunk);

static Chunk chunk_generate(World* wrld, iVector_t coord) {
    Chunk chunk = chunk_create(wrld, coord);
    chunk_generate_brick_map(&chunk);
    chunk_generate_occupancy_mask(&chunk);
    return chunk;
}

static Chunk world_chunk_generate_callback(void* userData, iVector_t coord) {
    return chunk_generate((World*)userData, coord);
}

void world_chunk_gen_workers_start(World* wrld) {
    chunk_thread_pool_start(&wrld->genPool, world_chunk_generate_callback, wrld);
}

void world_chunk_gen_workers_stop(World* wrld) {
    chunk_thread_pool_stop(&wrld->genPool);
}

void world_chunk_gen_submit(World* wrld, iVector_t coord) {
    chunk_thread_pool_submit(&wrld->genPool, coord);
}

bool world_chunk_gen_poll(World* wrld, Chunk* outChunk) {
    return chunk_thread_pool_poll(&wrld->genPool, outChunk);
}

int world_chunk_insert(World* wrld, Chunk* chunk) {
    hmput(wrld->chunk_map, chunk->coord, *chunk);

    uint32_t si = slot_grid_index(chunk_slot_key(chunk->coord));
    wrld->slot_grid[si].occupied = true;
    wrld->slot_grid[si].coord = chunk->coord;

    return hmgeti(wrld->chunk_map, chunk->coord);
}


void perm_map_gen(int* perm, uint32_t perm_size, uint32_t seed) {

    for (int i = 0; i < 256; i++) { perm[i] = i; }
    unsigned int permSeed = seed;
    for (int i = 255; i > 0; i--) {
        permSeed = permSeed * 1664525u + 1013904223u;
        int j = (int)(permSeed % (unsigned int)(i + 1));
        int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    }
}


WorldGenerator world_generator_create(uint32_t seed) {
    WorldGenerator gen = {0};

    PerlinParams ymap = {
        .amplitude = 0.0,
        .scale = 0.001,
        .lacunarity = 2.0,
        .persistence = 0.45,
        .octaves = 5,
        .sea_level = 128.0
    };
    perm_map_gen(ymap.perm, 256, seed);

    PerlinParams amp_pass = {
        .amplitude = 512,
        .scale = 0.00015,
        .lacunarity = 2.0,
        .persistence = 0.5,
        .octaves = 2,
        .sea_level = 100
    };
    perm_map_gen(amp_pass.perm, 256, ~seed);

    gen.ymap = ymap;
    gen.amplify = amp_pass;

    gen.seed = seed;

    gen.regions = NULL;

    structure_list_create(gen.structure_list); //do this here could be to expensive but not for now


    return gen;
}

void world_generator_free(WorldGenerator* gen) {

    for (uint32_t i= 0; i < hmlen(gen->regions); i++) {
        region_free(&gen->regions[i].value);
    }

    for (uint32_t i = 0; i < STRUCTURE_COUNT; i++) { //might make dynamic later
        model_free(&gen->structure_list[i].model);
    }

    hmfree(gen->regions);
    gen->regions = NULL;
}

void region_spawn_generator(WorldGenerator* gen) {
    const int radius = 2;
    for (int x = -radius; x <= radius; x++) {
        for (int z = -radius; z <= radius; z++) {
            iVector_t region_coord = iVector_create(x, 0, z);
            Region region = region_create(gen);
            hmput(gen->regions, region_coord, region);
        }
    }
}

Region region_create(WorldGenerator* gen) {
    Region r = {0};

    r.structure_count = 1;
    r.structures = calloc(r.structure_count, sizeof(Structure_t));

    //for now copy structure_list[0] into region structure
    memcpy(&r.structures[0], &gen->structure_list[3], sizeof(Structure_t));

    r.structures[0].region_coords = uVector_create(200, 128, 200);
    return r;
}



void region_free(Region* r) {
    free(r->structures);
    r->structures = NULL;
    r->structure_count = 0;
}


World world_create(uint32_t seed) {

    chunk_stream_offsets_load(); //temp

    World wrld = {0};

    wrld.chunk_load_queue = q_create(MAX_QUEUE_SIZE, sizeof(iVector_t));
    wrld.chunk_rmf_queue = q_create(MAX_QUEUE_SIZE, sizeof(iVector_t));


    wrld.seed = seed;
    wrld.generator = world_generator_create(seed);


    wrld.chunk_map = NULL;
    wrld.slot_grid = calloc(CHUNK_SLOT_GRID_COUNT, sizeof(SlotGridEntry));

    return wrld;
}


void world_load_spawn_chunk(World* wrld) {
    uint32_t total = 0;

    region_spawn_generator(&wrld->generator);

    chunk_thread_pool_lock_jobs(&wrld->genPool);
    for (int x = -(CHUNK_STREAM_RADIUS_XZ >> 1); x < CHUNK_STREAM_RADIUS_XZ >> 1; x++) {
        for (int y = WORLD_CHUNK_Y_MIN; y < WORLD_CHUNK_Y_MAX; y++) {
            for (int z = -(CHUNK_STREAM_RADIUS_XZ >> 1); z < CHUNK_STREAM_RADIUS_XZ >> 1 ; z++) {
                chunk_thread_pool_enqueue_locked(&wrld->genPool, iVector_create(x, y, z));
                total++;
            }
        }
    }
    chunk_thread_pool_unlock_jobs_and_wake(&wrld->genPool);

    uint32_t received = 0;
    while (received < total) {
        Chunk chunk;
        chunk_thread_pool_poll_blocking(&wrld->genPool, &chunk);
        world_chunk_insert(wrld, &chunk);
        received++;
    }
}

void chunk_generate_brick_map(Chunk* chunk) {
    if (chunk == NULL || chunk->voxels == NULL) {return;}

    uint8_t* voxels = chunk->voxels;
    uint64_t mask = 0;
    uint8_t brick_size = 8;
    uint8_t bricks_per_axis = CHUNK_DIM >> 3;

    //level 1
    for (uint32_t bz = 0; bz < bricks_per_axis; bz++) {
		for (uint32_t by = 0; by < bricks_per_axis; by++) {
			for (uint32_t bx = 0; bx < bricks_per_axis; bx++) {

                // level 2
                bool solid = false;
                for (uint32_t vz = 0; vz < brick_size && !solid; vz++) {
					for (uint32_t vy = 0; vy < brick_size && !solid; vy++) {
						for (uint32_t vx = 0; vx < brick_size; vx++) {
							uint32_t x = bx * brick_size + vx;
							uint32_t y = by * brick_size + vy;
							uint32_t z = bz * brick_size + vz;
							uint32_t idx = x + y * CHUNK_DIM + z * CHUNK_DIM * CHUNK_DIM;
							if (voxels[idx] != 0) { solid = true; break; }
						}
					}
				}
                if (solid) {
					uint32_t brick_index = bx + by * bricks_per_axis + bz * bricks_per_axis * bricks_per_axis;
					mask |= (1ull << brick_index);
				}

            }
        }
    }

    chunk->brickMask = mask;
}


void chunk_generate_occupancy_mask(Chunk* chunk) {
    if (chunk == NULL || chunk->voxels == NULL) {return;}

    uint8_t* voxels = chunk->voxels;
    uint32_t* voxelMask = calloc(CHUNK_SIZE >> 5, sizeof(uint32_t));

    for (uint32_t z = 0; z < CHUNK_DIM; z++) {
		for (uint32_t y = 0; y < CHUNK_DIM; y++) {
			for (uint32_t x = 0; x < CHUNK_DIM; x++) {
                uint32_t idx = x + y * CHUNK_DIM + z * CHUNK_DIM * CHUNK_DIM;
                if (voxels[idx] != 0) {
                    voxelMask[idx >> 5] |= (1u << (idx & 31u));
                }
            }
        }
    }

    chunk->voxelMask = voxelMask;

}

int world_chunk_load(World* wrld, iVector_t chunkCoords) {
    Chunk chunk = chunk_generate(wrld, chunkCoords);
    return world_chunk_insert(wrld, &chunk);
}

void world_chunk_memory_queue(World* wrld, Vector_t* cameraPos) {
    q_clear(&wrld->chunk_load_queue);
    q_clear(&wrld->chunk_rmf_queue);

    iVector_t chunk_coord = iVector_create((int32_t)floor(cameraPos->x / CHUNK_DIM), CHUNK_STREAM_CENTER_Y, (int32_t)floor(cameraPos->z / CHUNK_DIM));
    wrld->stream_center = chunk_coord;

    for (int i = 0; i < CHUNK_STREAM_COUNT; i++) {
        ChunkOffset off = chunk_stream_offsets[i];
        iVector_t test_coord = iVector_create(chunk_coord.x + off.dx, chunk_coord.y + off.dy, chunk_coord.z + off.dz);

        uint32_t si = slot_grid_index(chunk_slot_key(test_coord));
        bool alreadyLoaded = wrld->slot_grid[si].occupied && ivec_eq(wrld->slot_grid[si].coord, test_coord);
        if (!alreadyLoaded) {
            q_enque(&wrld->chunk_load_queue, &test_coord);
        }

    }

    iVector_t cam_coord = chunk_coord;
    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        iVector_t test_coord = wrld->chunk_map[i].key;
        int32_t dx = test_coord.x - cam_coord.x;
        int32_t dy = test_coord.y - cam_coord.y;
        int32_t dz = test_coord.z - cam_coord.z;

        if (dx < -CHUNK_STREAM_RADIUS_XZ || dx >= CHUNK_STREAM_RADIUS_XZ ||
            dy < -CHUNK_STREAM_RADIUS_Y || dy >= CHUNK_STREAM_RADIUS_Y ||
            dz < -CHUNK_STREAM_RADIUS_XZ || dz >= CHUNK_STREAM_RADIUS_XZ) {
            q_enque(&wrld->chunk_rmf_queue, &test_coord);
        }
    }

    int32_t minX = INT32_MAX, maxX = INT32_MIN;
    int32_t minY = INT32_MAX, maxY = INT32_MIN;
    int32_t minZ = INT32_MAX, maxZ = INT32_MIN;
    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        iVector_t k = wrld->chunk_map[i].key;
        if (k.x < minX) { minX = k.x; } if (k.x > maxX) { maxX = k.x; }
        if (k.y < minY) { minY = k.y; } if (k.y > maxY) { maxY = k.y; }
        if (k.z < minZ) { minZ = k.z; } if (k.z > maxZ) { maxZ = k.z; }
    }
    //printf("[stream] cam_chunk=(%d,%d,%d) loaded=%d bounds x[%d,%d] y[%d,%d] z[%d,%d]\n",
        //chunk_coord.x, chunk_coord.y, chunk_coord.z, (int)hmlen(wrld->chunk_map),
        //minX, maxX, minY, maxY, minZ, maxZ);
}


int world_chunk_find_collision(World* wrld, iVector_t coord, int excludeIdx) {
    uint32_t si = slot_grid_index(chunk_slot_key(coord));
    if (!wrld->slot_grid[si].occupied) { return -1; }

    iVector_t owner = wrld->slot_grid[si].coord;
    if (ivec_eq(owner, coord)) { return -1; }

    int idx = hmgeti(wrld->chunk_map, owner);
    if (idx == excludeIdx) { return -1; }
    return idx;
}


void world_chunk_unload_by_index(World* wrld, int idx) {
    if (idx < 0 || idx >= hmlen(wrld->chunk_map)) { return; }
    iVector_t coord = wrld->chunk_map[idx].key;

    uint32_t si = slot_grid_index(chunk_slot_key(coord));
    if (wrld->slot_grid[si].occupied && ivec_eq(wrld->slot_grid[si].coord, coord)) {
        wrld->slot_grid[si].occupied = false;
    }

    chunk_free(&wrld->chunk_map[idx].value);
    hmdel(wrld->chunk_map, coord);
}


int perlin_iter(uint32_t x, uint32_t z, iVector_t coord, PerlinParams* pp, double* amp_override) {

    double amplitude = amp_override == NULL ? pp->amplitude : *amp_override;
    double baseScale = pp->scale;

    double nx = ((double)coord.x * CHUNK_DIM + (double)x) * baseScale;
	double nz = ((double)coord.z * CHUNK_DIM + (double)z) * baseScale;

    double total = 0.0;
    double freq = 1.0;
    double amp = 1.0;
    double maxAmp = 0.0;
    uint32_t hieght_limit = WORLD_HEIGHT_LIMIT;

    int* perm = pp->perm;

    for (int octave = 0; octave < pp->octaves; octave++) {
        double px = nx * freq;
        double pz = nz * freq;

        int xi = (int)floor(px);
        int zi = (int)floor(pz);
        int X = xi & 255;
        int Z = zi & 255;
        int X1 = (X + 1) & 255;
        int Z1 = (Z + 1) & 255;
        double xf = px - (double)xi;
        double zf = pz - (double)zi;
        double u = perlin_fade(xf);
        double v = perlin_fade(zf);

        int aa = perm[(perm[X] + Z) & 255];
        int ba = perm[(perm[X1] + Z) & 255];
        int ab = perm[(perm[X] + Z1) & 255];
        int bb = perm[(perm[X1] + Z1) & 255];

        double x1 = perlin_lerp(u, perlin_grad(aa, xf, zf), perlin_grad(ba, xf - 1.0, zf));
        double x2 = perlin_lerp(u, perlin_grad(ab, xf, zf - 1.0), perlin_grad(bb, xf - 1.0, zf - 1.0));
        double n = perlin_lerp(v, x1, x2);

        total += n * amp;
        maxAmp += amp;
        freq *= pp->lacunarity;
        amp *= pp->persistence;
    }

    double noiseValue = total / maxAmp;

    if (amp_override != NULL) {*amp_override = noiseValue;}
    int total_height = (int)((double)pp->sea_level + noiseValue * amplitude);
    if (total_height < 1) { total_height = 1; }
    if (total_height >= (int)hieght_limit) { total_height = (int)hieght_limit - 1; }

    return total_height;
}

Chunk chunk_create(World* wrld, iVector_t coord) {

    Chunk chunk = {0};
    chunk.coord = coord;
    chunk.voxels = calloc(CHUNK_SIZE, sizeof(uint8_t));

	uint8_t dimX = CHUNK_DIM, dimY = CHUNK_DIM, dimZ = CHUNK_DIM;

    uint32_t hieght_limit = WORLD_HEIGHT_LIMIT;

	int chunkBaseY = coord.y * (int32_t)dimY; // world Y of this chunk's local y=0

	for (uint32_t z = 0; z < dimZ; z++) {
		for (uint32_t x = 0; x < dimX; x++) {

            double amps = wrld->generator.amplify.amplitude;
            perlin_iter(x, z, coord, &wrld->generator.amplify, &amps);

            double t = (amps + 1.0) * 0.5;
            t = smoothstep(0.2, 0.9, t);

            amps = 16 + t * 450;

            int total_height = perlin_iter(x, z, coord, &wrld->generator.ymap, &amps);

            int localHeightRaw = total_height - chunkBaseY; 

            int height = localHeightRaw;
            if (height < 0) { height = 0; }
            if (height > dimY) { height = dimY; }

            //funny hash function
            uint32_t h = xz_hash(x, z, wrld->seed);

            int off = (h % 21) - 10;
            int stone = 220 + off;
            off = (~h % 21) - 10;
            int snow = 260 + off;
            int stone_start = stone - chunkBaseY;

            if (stone_start < 0) { stone_start = 0; }
            if (stone_start > dimY) { stone_start = dimY; }

            int dirt_height = min(stone_start, height);
            

			for (int y = 0; y < dirt_height; y++) {
				uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
				chunk.voxels[idx] = DIRT; // brown, fills down to the bottom of the grid
			}

            for (int y = stone_start; y < height; y++) {
				uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
				chunk.voxels[idx] = STONE; 
			}

            int localSeaLevel = (int)wrld->generator.ymap.sea_level - chunkBaseY;
            if (localSeaLevel < 0) { localSeaLevel = 0; }
            if (localSeaLevel > dimY) { localSeaLevel = dimY; }

            if (height < localSeaLevel) {
                for (int y = height; y < localSeaLevel; y++) {
                    uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
                    chunk.voxels[idx] = WATER;
                }
            }
            if (localHeightRaw >= 0 && localHeightRaw < dimY) {
                uint32_t topIdx = x + (uint32_t)height * dimX + z * dimX * dimY;
                if ((uint32_t)total_height <= wrld->generator.ymap.sea_level) {
                    chunk.voxels[topIdx] = SAND;
                } else if (total_height < stone) {
                    chunk.voxels[topIdx] = total_height < stone - 1 ? GRASS : DIRT;
                } else if (total_height < snow) {
                    chunk.voxels[topIdx] = STONE;
                } else {
                    chunk.voxels[topIdx] = SNOW;
                }
            }

		}
	}

    chunk_region_structure_placements(&wrld->generator, &chunk);

	return chunk;
}


void world_destroy(World* wrld) {
    world_chunk_gen_workers_stop(wrld);

    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        chunk_free(&wrld->chunk_map[i].value);
    }
    hmfree(wrld->chunk_map);
    wrld->chunk_map = NULL;

    free(wrld->slot_grid);
    wrld->slot_grid = NULL;

    q_unalloc(&wrld->chunk_load_queue);
    q_unalloc(&wrld->chunk_rmf_queue);

    world_generator_free(&wrld->generator);
}


void structure_manual_place(World* wrld, StructureTypes type, iVector_t origin) {
    iVector_t region_coord = iVector_create(origin.x >> REGION_DIM_SHIFT, 0 , origin.z >> REGION_DIM_SHIFT);
    uVector_t region_rel_coord = uVector_create(
        (uint32_t)wrap_axis(origin.x, 1 << REGION_DIM_SHIFT),
        (uint32_t)origin.y,
        (uint32_t)wrap_axis(origin.z, 1 << REGION_DIM_SHIFT)
    );
    WorldGenerator* gen = &wrld->generator;

    if (hmgeti(gen->regions, region_coord) == -1) {
        Region r = region_create(gen);
        hmput(gen->regions, region_coord, r);
    }

    int idx = hmgeti(gen->regions, region_coord);
    Region* r = &gen->regions[idx].value;

    Structure_t* update = malloc((r->structure_count + 1) * sizeof(Structure_t));
    memcpy(update, r->structures, r->structure_count * sizeof(Structure_t));
    memcpy(&update[r->structure_count], &gen->structure_list[type], sizeof(Structure_t));
    free(r->structures);
    r->structures = update;

    r->structures[r->structure_count].region_coords = region_rel_coord;
    r->structure_count++;
}

void chunk_region_structure_placements(WorldGenerator* gen, Chunk* chunk) {
    iVector_t region_coord = iVector_create(chunk->coord.x >> REGION_CHUNK_SHIFT, 0 , chunk->coord.z >> REGION_CHUNK_SHIFT);
    int idx = hmgeti(gen->regions, region_coord);
    if (idx != -1) {
        Region* r = &gen->regions[idx].value;

        for (uint32_t i = 0; i < r->structure_count; i++) {
            Structure_t* s = &r->structures[i];
            uVector_t model_dim = s->model.dimensions;

            uVector_t smin = s->region_coords;
            uVector_t smax = uVector_add(&smin, &model_dim);

            uint32_t local_chunk_x = (uint32_t)wrap_axis(chunk->coord.x, 1 << REGION_CHUNK_SHIFT);
            uint32_t local_chunk_z = (uint32_t)wrap_axis(chunk->coord.z, 1 << REGION_CHUNK_SHIFT);

            uVector_t cmin = uVector_create(local_chunk_x * CHUNK_DIM, (uint32_t)chunk->coord.y * CHUNK_DIM, local_chunk_z * CHUNK_DIM);
            uVector_t cmax = uVector_create(cmin.x + CHUNK_DIM, cmin.y + CHUNK_DIM, cmin.z + CHUNK_DIM);

            if (smax.x < cmin.x || smin.x > cmax.x) continue;
            if (smax.y < cmin.y || smin.y > cmax.y) continue;
            if (smax.z < cmin.z || smin.z > cmax.z) continue;

            //clamp aabb to same region
            uVector_t ovlp_min = uVector_create(
                smin.x > cmin.x ? smin.x : cmin.x,
                smin.y > cmin.y ? smin.y : cmin.y,
                smin.z > cmin.z ? smin.z : cmin.z
            );
            uVector_t ovlp_max = uVector_create(
                smax.x < cmax.x ? smax.x : cmax.x,
                smax.y < cmax.y ? smax.y : cmax.y,
                smax.z < cmax.z ? smax.z : cmax.z
            );

            for (uint32_t wz = ovlp_min.z; wz < ovlp_max.z; wz++) {
                for (uint32_t wy = ovlp_min.y; wy < ovlp_max.y; wy++) {
                    for (uint32_t wx = ovlp_min.x; wx < ovlp_max.x; wx++) {
                        uint32_t struct_idx = (wx - smin.x) + (wy - smin.y) * model_dim.x + (wz - smin.z) * model_dim.x * model_dim.y;
                        uint8_t voxel = s->model.voxels[struct_idx];
                        if (voxel == AIR) { continue; }

                        uint32_t chunk_idx = (wx - cmin.x) + (wy - cmin.y) * CHUNK_DIM + (wz - cmin.z) * CHUNK_DIM * CHUNK_DIM;
                        chunk->voxels[chunk_idx] = voxel;
                    }
                }
            }
        }


    } 
    else {
        //handle generating new region? //could cause race conditions with multiple threads?
    }

}



static Structure_t create_cube_structure(uint32_t dim[3], uint8_t material) {
    Structure_t structure = {0};
    structure.model = model_create_prefab(CUBE, 0, 0, dim, &material, NULL);
    return structure;
}

static Structure_t create_plane_structure(uint32_t width, uint32_t depth, uint8_t material) {
    Structure_t structure = {0};
    structure.model = model_create_prefab(PLANE, 0, 0, &width, &depth, &material);
    return structure;
}

static Structure_t create_sphere_structure(uint32_t diameter, uint8_t material) {
    Structure_t structure = {0};
    structure.model = model_create_prefab(SPHERE, 0, 0, &diameter, &material, NULL);
    return structure;
}


void structure_list_create(Structure_t* structure_list) {
    uint32_t light_cube_dim[3] = {2, 2, 2};

    structure_list[MIRROR_PLANE] = create_plane_structure(50, 50, MIRROR);
    structure_list[VARNISH_PLANE] = create_plane_structure(40, 40, BLACK_VARNISH);
    structure_list[MIRROR_BALL] = create_sphere_structure(40, MIRROR);
    structure_list[RUBY_BALL] = create_sphere_structure(30, RUBY);
    structure_list[LIGHT_CUBE] = create_cube_structure(light_cube_dim, YELLOW_LIGHT);
    structure_list[CASTLE].model = model_create_from_vox("./res/models/castle.vox", 0, 0, COPPER, false);
    structure_list[STATUE].model = model_create_from_vox("./res/models/sculpt2.vox", 0, 0, MARBLE, false);
    structure_list[BOAT].model = model_create_from_vox("./res/models/boat.vox", 0, 0, AIR, true);
    structure_list[TREE].model = model_create_from_vox("./res/models/tree.vox", 0, 0, AIR, true);
    structure_list[SHIMMER].model = model_create_from_vox("./res/models/bronze.vox", 0, 0, COPPER, false);
    structure_list[CUTE].model = model_create_from_vox("./res/models/cute.vox", 0, 0, COPPER, true);
    structure_list[OUT].model = model_create_from_vox("./res/models/out.vox", 0, 0, COPPER, true);
}


