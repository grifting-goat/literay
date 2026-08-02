#include "world.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "material.h"
#include "vox_loader.h"

#include "stb_ds.h"

//move these elsewhere later
#define CHUNK_STREAM_RADIUS CHUNK_COORD_LIMIT
#define CHUNK_STREAM_DIAMETER (CHUNK_STREAM_RADIUS * 2)
#define CHUNK_STREAM_COUNT (CHUNK_STREAM_DIAMETER * CHUNK_STREAM_DIAMETER * CHUNK_STREAM_DIAMETER)

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
    for (int32_t dz = -CHUNK_STREAM_RADIUS; dz < CHUNK_STREAM_RADIUS; dz++) {
        for (int32_t dy = -CHUNK_STREAM_RADIUS; dy < CHUNK_STREAM_RADIUS; dy++) {
            for (int32_t dx = -CHUNK_STREAM_RADIUS; dx < CHUNK_STREAM_RADIUS; dx++) {
                chunk_stream_offsets[i++] = (ChunkOffset){dx, dy, dz, dx*dx + dy*dy + dz*dz};
            }
        }
    }

    qsort(chunk_stream_offsets, CHUNK_STREAM_COUNT, sizeof(ChunkOffset), chunk_offset_cmp);
    chunk_stream_offsets_ready = true;
}



//Claude made these for me
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
void chunk_free(Chunk* chunk);



World world_create(uint32_t seed) {

    chunk_stream_offsets_load(); //temp

    World wrld = {0};
    PerlinParams pp = {
        .amplitude = 100.0f,
        .scale = 0.001f,
        .octaves = 6,
        .sea_level = 50.0f
    };

    wrld.chunk_load_queue = q_create(MAX_QUEUE_SIZE);
    wrld.chunk_rmf_queue = q_create(MAX_QUEUE_SIZE);

    wrld.perlin_params = pp;

    wrld.seed = seed;

    wrld.chunk_map = NULL;
    wrld.pending_map = NULL;

    return wrld;
}


void world_load_spawn_chunk(World* wrld) {

    for (int x = -7; x < 8; x++) {
        for (int y = -7; y < 8; y++) {
            for (int z = -7; z < 8; z++) {
                iVector_t coord = iVector_create(x, y, z);
                Chunk chunk = chunk_create(wrld, coord);
                

                chunk_generate_brick_map(&chunk);
                chunk_generate_occupancy_mask(&chunk);

                hmput(wrld->chunk_map, chunk.coord, chunk);
            }
        }
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
    Chunk chunk = chunk_create(wrld, chunkCoords);
    chunk_generate_brick_map(&chunk);
    chunk_generate_occupancy_mask(&chunk);
    hmput(wrld->chunk_map, chunk.coord, chunk);
    return hmgeti(wrld->chunk_map, chunkCoords);
}

void world_chunk_memory_queue(World* wrld, Vector_t* cameraPos) {

    iVector_t chunk_coord = iVector_create((int32_t)floor(cameraPos->x / CHUNK_DIM), (int32_t)floor(cameraPos->y / CHUNK_DIM), (int32_t)floor(cameraPos->z / CHUNK_DIM));

    for (int i = 0; i < CHUNK_STREAM_COUNT; i++) {
        ChunkOffset off = chunk_stream_offsets[i];
        iVector_t test_coord = iVector_create(chunk_coord.x + off.dx, chunk_coord.y + off.dy, chunk_coord.z + off.dz);

        int idx = hmgeti(wrld->chunk_map, test_coord);
        if (idx == -1 && hmgeti(wrld->pending_map, test_coord) == -1) {
            q_enque(&wrld->chunk_load_queue, &test_coord);
            hmput(wrld->pending_map, test_coord, true);
        }

    }

    iVector_t cam_coord = chunk_coord;
    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        iVector_t test_coord = wrld->chunk_map[i].key;
        int32_t dx = test_coord.x - cam_coord.x;
        int32_t dy = test_coord.y - cam_coord.y;
        int32_t dz = test_coord.z - cam_coord.z;

        if ((dx < -CHUNK_STREAM_RADIUS || dx >= CHUNK_STREAM_RADIUS ||
            dy < -CHUNK_STREAM_RADIUS || dy >= CHUNK_STREAM_RADIUS ||
            dz < -CHUNK_STREAM_RADIUS || dz >= CHUNK_STREAM_RADIUS) &&
            hmgeti(wrld->pending_map, test_coord) == -1) {
            q_enque(&wrld->chunk_rmf_queue, &test_coord);
            hmput(wrld->pending_map, test_coord, true);
        }
    }

}


//returns the chunk_map index or -1
int world_chunk_test(World* wrld, Vector_t* cameraPos) {

    iVector_t chunk_coord = iVector_create((int32_t)floor(cameraPos->x / CHUNK_DIM), 0, (int32_t)floor(cameraPos->z / CHUNK_DIM));
    chunk_stream_offsets_load();

    for (int i = 0; i < CHUNK_STREAM_COUNT; i++) {
        ChunkOffset off = chunk_stream_offsets[i];
        iVector_t test_coord = iVector_create(chunk_coord.x + off.dx, chunk_coord.y + off.dy, chunk_coord.z + off.dz);

        int idx = hmgeti(wrld->chunk_map, test_coord);
        if (idx == -1) {
            Chunk chunk = chunk_create(wrld, test_coord);
            chunk_generate_brick_map(&chunk);
            chunk_generate_occupancy_mask(&chunk);
            hmput(wrld->chunk_map, chunk.coord, chunk);
            idx = hmgeti(wrld->chunk_map, test_coord);
            return idx;
        }
    }
    return -1;

}



//returns the chunk_map index of a loaded chunk now outside the streaming box, or -1
int world_chunk_untest(World* wrld, Vector_t* cameraPos) {

    iVector_t cam_coord = iVector_create((int32_t)floor(cameraPos->x / CHUNK_DIM), 0, (int32_t)floor(cameraPos->z / CHUNK_DIM));

    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        iVector_t chunk_coord = wrld->chunk_map[i].key;
        int32_t dx = chunk_coord.x - cam_coord.x;
        int32_t dz = chunk_coord.z - cam_coord.z;

        if (dx < -CHUNK_STREAM_RADIUS || dx >= CHUNK_STREAM_RADIUS ||
            dz < -CHUNK_STREAM_RADIUS || dz >= CHUNK_STREAM_RADIUS) {
            return i;
        }
    }
    return -1;
}

int world_chunk_find_collision(World* wrld, iVector_t coord, int excludeIdx) {
    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        if (i == excludeIdx) { continue; }
        iVector_t c = wrld->chunk_map[i].key;
        if (c.y != coord.y) { continue; }
        if ((c.x - coord.x) % CHUNK_STREAM_WINDOW == 0 && (c.z - coord.z) % CHUNK_STREAM_WINDOW == 0) {
            return i;
        }
    }
    return -1;
}


void world_chunk_unload_by_index(World* wrld, int idx) {
    if (idx < 0 || idx >= hmlen(wrld->chunk_map)) { return; }
    iVector_t coord = wrld->chunk_map[idx].key;
    chunk_free(&wrld->chunk_map[idx].value);
    hmdel(wrld->chunk_map, coord);
}


void chunk_free(Chunk* chunk) {
    free(chunk->voxelMask); 
    chunk->voxelMask = NULL;

    free(chunk->voxels);
    chunk->voxels = NULL;

    chunk->loaded = false;
}

Chunk chunk_create(World* wrld, iVector_t coord) {

    Chunk chunk = {0};
    chunk.coord = coord;
    chunk.voxels = calloc(CHUNK_SIZE, sizeof(uint8_t));

	uint8_t dimX = CHUNK_DIM, dimY = CHUNK_DIM, dimZ = CHUNK_DIM;

    uint32_t hieght_limit = 0xFF;

	int perm[256];
	for (int i = 0; i < 256; i++) {perm[i] = i;}

	unsigned int seed = wrld->seed;
	for (int i = 255; i > 0; i--) {
		seed = seed * 1664525u + 1013904223u;
		int j = (int)(seed % (unsigned int)(i + 1));
		int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
	}

	double baseScale = wrld->perlin_params.scale;
	double amplitude = wrld->perlin_params.amplitude;
	int octaves = wrld->perlin_params.octaves;

	int chunkBaseY = coord.y * (int32_t)dimY; // world Y of this chunk's local y=0

	for (uint32_t z = 0; z < dimZ; z++) {
		for (uint32_t x = 0; x < dimX; x++) {

			double nx = ((double)coord.x * CHUNK_DIM + (double)x) * baseScale;
			double nz = ((double)coord.z * CHUNK_DIM + (double)z) * baseScale;

			double total = 0.0;
			double freq = 1.0;
			double amp = 1.0;
			double maxAmp = 0.0;

			for (int octave = 0; octave < octaves; octave++) {
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
				freq *= 2.0;
				amp *= 0.5;
			}

			double noiseValue = total / maxAmp;
			int total_height = (int)((double)wrld->perlin_params.sea_level + noiseValue * amplitude);
			if (total_height < 1) { total_height = 1; }
			if (total_height >= (int)hieght_limit) { total_height = (int)hieght_limit - 1; }

            int localHeightRaw = total_height - chunkBaseY; 

            int height = localHeightRaw;
            if (height < 0) { height = 0; }
            if (height > dimY) { height = dimY; }

			for (int y = 0; y < height; y++) {
				uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
				chunk.voxels[idx] = DIRT; // brown, fills down to the bottom of the grid
			}

            int localSeaLevel = (int)wrld->perlin_params.sea_level - chunkBaseY;
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
			    chunk.voxels[topIdx] = (uint32_t)total_height > wrld->perlin_params.sea_level ? GRASS : SAND; // green, top block of the column

            }

		}
	}

	return chunk;
}


void world_destroy(World* wrld) {
    for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
        chunk_free(&wrld->chunk_map[i].value);
    }
    hmfree(wrld->chunk_map);
    wrld->chunk_map = NULL;

    hmfree(wrld->pending_map);
    wrld->pending_map = NULL;

    q_unalloc(&wrld->chunk_load_queue);
    q_unalloc(&wrld->chunk_rmf_queue);
}



/*
void world_generate_terrain(World* wrld) {

    generate_terrain_heightmap(wrld);
}



void generate_terrain_heightmap(World* wrld) {
	uint8_t* voxels = wrld->voxels;
    uint32_t* y_map = wrld->hieght_map;

	uint32_t dimX = wrld->dimensions[0];
	uint32_t dimY = wrld->dimensions[1];
	uint32_t dimZ = wrld->dimensions[2];

	int perm[256];
	for (int i = 0; i < 256; i++) { perm[i] = i; }
	unsigned int seed = wrld->seed;
	for (int i = 255; i > 0; i--) {
		seed = seed * 1664525u + 1013904223u;
		int j = (int)(seed % (unsigned int)(i + 1));
		int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
	}

	double baseScale = wrld->perlin_params.scale;
	double amplitude = wrld->perlin_params.amplitude;
	int octaves = wrld->perlin_params.octaves;

	for (uint32_t z = 0; z < dimZ; z++) {
		for (uint32_t x = 0; x < dimX; x++) {

			double nx = (double)x * baseScale;
			double nz = (double)z * baseScale;

			double total = 0.0;
			double freq = 1.0;
			double amp = 1.0;
			double maxAmp = 0.0;

			for (int octave = 0; octave < octaves; octave++) {
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
				freq *= 2.0;
				amp *= 0.5;
			}

			double noiseValue = total / maxAmp;
			int height = (int)((double)(dimY / 2) + noiseValue * amplitude);
			if (height < 1) { height = 1; }
			if (height >= (int)dimY) { height = (int)dimY - 1; }
            y_map[x + dimX * z] = height;

			for (int y = 0; y < height; y++) {
				uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
				voxels[idx] = DIRT; // brown, fills down to the bottom of the grid
			}

            if (height < wrld->perlin_params.sea_level) {
                for (int y = height; y < wrld->perlin_params.sea_level; y++) {
                    uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
                    voxels[idx] = WATER;
                }
            }

			uint32_t topIdx = x + (uint32_t)height * dimX + z * dimX * dimY;
			voxels[topIdx] = height > wrld->perlin_params.sea_level ? GRASS : SAND; // green, top block of the column
		}
	}
}



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


