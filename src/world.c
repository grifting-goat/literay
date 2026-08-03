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

static iVector_t chunk_slot_key(iVector_t coord) {
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

    return gen;
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
            int stone = 240 + off;
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
                    chunk.voxels[topIdx] = GRASS;
                } else if (total_height < snow) {
                    chunk.voxels[topIdx] = STONE;
                } else {
                    chunk.voxels[topIdx] = SNOW;
                }
            }

		}
	}

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
}



/*

static Structure_t create_cube_structure(uint32_t dim[3], uint8_t material) {
    uint32_t size = dim[0] * dim[1] * dim[2];

    Structure_t structure = {
        .size = size,
        .voxels = calloc(size, sizeof(uint8_t))
    };
    memcpy(structure.dimensions, dim, sizeof(structure.dimensions));
    memset(structure.voxels, material, size);

    return structure;
}

static Structure_t create_plane_structure(uint32_t width, uint32_t depth, uint8_t material) {
    uint32_t dim[3] = {width, 1, depth};
    return create_cube_structure(dim, material);
}

static Structure_t create_sphere_structure(uint32_t diameter, uint8_t material) {
    uint32_t dim[3] = {diameter, diameter, diameter};
    uint32_t voxel_count = dim[0] * dim[1] * dim[2];

    Structure_t structure = {
        .size = voxel_count,
        .voxels = calloc(voxel_count, sizeof(uint8_t))
    };
    memcpy(structure.dimensions, dim, sizeof(structure.dimensions));

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
                    structure.voxels[idx] = material;
                }
            }
        }
    }

    return structure;
}


void structure_list_create() {
    uint32_t light_cube_dim[3] = {2, 2, 2};

    structure_list[MIRROR_PLANE] = create_plane_structure(50, 50, MIRROR);
    structure_list[VARNISH_PLANE] = create_plane_structure(40, 40, BLACK_VARNISH);
    structure_list[MIRROR_BALL] = create_sphere_structure(40, MIRROR);
    structure_list[RUBY_BALL] = create_sphere_structure(30, RUBY);
    structure_list[LIGHT_CUBE] = create_cube_structure(light_cube_dim, YELLOW_LIGHT);
    structure_list[CASTLE] = structure_load_vox("./res/models/castle.vox", COPPER, false);
    structure_list[STATUE] = structure_load_vox("./res/models/sculpt2.vox", MARBLE, false);
    structure_list[BOAT] = structure_load_vox("./res/models/boat.vox",  AIR, true);
    structure_list[TREE] = structure_load_vox("./res/models/tree.vox", AIR, true);
    structure_list[SHIMMER] = structure_load_vox("./res/models/bronze.vox", COPPER, false);
    structure_list[CUTE] = structure_load_vox("./res/models/cute.vox", COPPER, true);
    structure_list[OUT] = structure_load_vox("./res/models/out.vox", COPPER, true);
}


void world_structure_place(World* wrld, StructureTypes type, uint32_t origin[3], uint32_t normal, uint32_t rotation, float scale, bool overwrite) {

    uint32_t dim[3] = {structure_list[type].dimensions[0], structure_list[type].dimensions[1], structure_list[type].dimensions[2]};


    static const int normalAxis[6] = {1, 1, 0, 0, 2, 2}; // which world axis carries local Y
    static const int normalSign[6] = {1, -1, 1, -1, 1, -1};

    int axis[3];
    int sign[3];

    int nAxis = normalAxis[normal % 6];
    axis[nAxis] = 1; // local Y -> the normal's world axis
    sign[nAxis] = normalSign[normal % 6];


    int sideWorldAxis[2];
    int sideCount = 0;
    for (int w = 0; w < 3; w++) {
        if (w != nAxis) { sideWorldAxis[sideCount++] = w; }
    }


    static const int spinLocalAxis[4][2] = { {0, 2}, {2, 0}, {0, 2}, {2, 0} };
    static const int spinSign[4][2] = { {1, 1}, {-1, 1}, {-1, -1}, {1, -1} };
    const int* sla = spinLocalAxis[rotation % 4];
    const int* ss = spinSign[rotation % 4];

    axis[sideWorldAxis[0]] = sla[0];
    sign[sideWorldAxis[0]] = ss[0];
    axis[sideWorldAxis[1]] = sla[1];
    sign[sideWorldAxis[1]] = ss[1];

    uint32_t scaled_dim[3] = {
        (uint32_t)ceilf(dim[axis[0]] * scale),
        (uint32_t)ceilf(dim[axis[1]] * scale),
        (uint32_t)ceilf(dim[axis[2]] * scale)
    };

    uint32_t outer_dim[3] = {scaled_dim[0] + origin[0], scaled_dim[1] + origin[1], scaled_dim[2] + origin[2]};

    if (outer_dim[0] > wrld->dimensions[0] || outer_dim[1] > wrld->dimensions[1] || outer_dim[2] > wrld->dimensions[2]) {
        printf("Cant place Structure\n");
        return;
    }

    for (uint32_t x = origin[0]; x < outer_dim[0]; x++) {
        for (uint32_t y = origin[1]; y < outer_dim[1]; y++) {
            for (uint32_t z = origin[2]; z < outer_dim[2]; z++) {
                uint32_t local[3] = {
                    (uint32_t)((x - origin[0]) / scale),
                    (uint32_t)((y - origin[1]) / scale),
                    (uint32_t)((z - origin[2]) / scale)
                };

                uint32_t src[3];
                for (int w = 0; w < 3; w++) {
                    uint32_t axisDim = dim[axis[w]];
                    src[axis[w]] = (sign[w] > 0) ? local[w] : (axisDim - 1 - local[w]);
                }

                if (src[0] >= dim[0]) { src[0] = dim[0] - 1; }
                if (src[1] >= dim[1]) { src[1] = dim[1] - 1; }
                if (src[2] >= dim[2]) { src[2] = dim[2] - 1; }

                uint32_t world_idx = x + y * wrld->dimensions[0] + z * wrld->dimensions[0] * wrld->dimensions[1];
                uint32_t struct_idx = src[0] + src[1] * dim[0] + src[2] * dim[0] * dim[1];

                if (wrld->voxels[world_idx] == 0 || overwrite) {
                    wrld->voxels[world_idx] = structure_list[type].voxels[struct_idx];
                }   
            }
        }
    }

}

void world_generate_structures(World* wrld) {
    uint32_t seed = wrld->seed;
    srand(seed);

    uint32_t* ymap = wrld->hieght_map;

    for (uint32_t x = 0; x < wrld->dimensions[0]; x++) {
        for (uint32_t z = 0; z < wrld->dimensions[2]; z++) {
            rand();
            rand();
            rand();
            if (!rand() % 25000) {
                uint32_t y = ymap[x + z * wrld->dimensions[1]] + rand() % 50 + 10;
                uint32_t origin[3] = {x, y, z};
                world_structure_place(wrld, (StructureTypes)MIRROR_BALL, origin, 0, 0, (float)(rand() % 6) * 0.1f + 0.5f, true);
            }

            if (!rand() % 20000) {
                uint32_t y = ymap[x + z * wrld->dimensions[1]] + rand() % 50 + 4;
                uint32_t origin[3] = {x, y, z};
                world_structure_place(wrld, (StructureTypes)RUBY_BALL, origin, 0, 0, (float)(rand() % 8) * 0.1f + 0.3f, false);
            }

            if (!rand() % 2000) {

                uint32_t y = ymap[x + z * wrld->dimensions[1]]+1;
                uint32_t origin[3] = {x, y, z};
                //world_structure_place(wrld, (StructureTypes)LIGHT_CUBE, origin, 0, 0, 1.0f, true);

            }

            if (!rand() % 800) {
                uint32_t y = ymap[x + z * wrld->dimensions[1]];
                uint32_t origin[3] = {x, y-2, z};
                if (y > wrld->perlin_params.sea_level) {
                    world_structure_place(wrld, (StructureTypes)TREE, origin, 0, rand() % 4, 2.0f, false);
                }

            }
        }
    }

    uint32_t mp_origin[3] = {wrld->dimensions[0] / 2, wrld->dimensions[1] * 0.55f, wrld->dimensions[2] / 2};
    //world_structure_place(wrld, (StructureTypes)MIRROR_PLANE, mp_origin, 2, 0, 1.0f, true); // facing +X, stands it up as a wall

    //uint32_t vp_origin[3] = {wrld->dimensions[0] / 2, wrld->dimensions[1] * 0.55f, wrld->dimensions[2] / 3};
    //world_structure_place(wrld, (StructureTypes)VARNISH_PLANE, vp_origin, 0, 0, 1.0f, true);

    uint32_t castle_origin[3] = {wrld->dimensions[0] * 0.7f, wrld->dimensions[1] * 0.5f, wrld->dimensions[2] * 0.7f};
    world_structure_place(wrld, (StructureTypes)CASTLE, castle_origin, 0, 0, 4.0f, false);

    uint32_t statue_origin[3] = {wrld->dimensions[0] * 0.2f, wrld->dimensions[1] * 0.52f, wrld->dimensions[2] * 0.2f};
    world_structure_place(wrld, (StructureTypes)STATUE, statue_origin, 0, 2, 1.0f, false);

    uint32_t boat_origin[3] = {wrld->dimensions[0] * 0.6f, wrld->dimensions[1] * 0.48f, wrld->dimensions[2] * 0.22f};
    world_structure_place(wrld, (StructureTypes)BOAT, boat_origin, 0, 0, 1.0f, false);

    uint32_t shimmer_origin[3] = {wrld->dimensions[0] * 0.3f, wrld->dimensions[1] * 0.45f, wrld->dimensions[2] * 0.7f};
    shimmer_origin[1] = ymap[shimmer_origin[0] + shimmer_origin[2] * wrld->dimensions[1]];
    //world_structure_place(wrld, (StructureTypes)SHIMMER, shimmer_origin, 0, 1, 1.0f, false);

    uint32_t out_origin[3] = {wrld->dimensions[0] * 0.17f, wrld->dimensions[1] * 0.5f, wrld->dimensions[2] * 0.82f};
    out_origin[1] = ymap[out_origin[0] + out_origin[2] * wrld->dimensions[1]];
    world_structure_place(wrld, (StructureTypes)OUT, out_origin, 0, 0, 1.0f, false);

    uint32_t cute_origin[3] = {wrld->dimensions[0] * 0.45f, wrld->dimensions[1] * 0.5f, wrld->dimensions[2] * 0.6f};
    cute_origin[1] = ymap[cute_origin[0] + cute_origin[2] * wrld->dimensions[1]];
    //world_structure_place(wrld, (StructureTypes)CUTE, cute_origin, 0, 1, 1.0f, false);

}

*/


