#include "world.h"

#include "material.h"


//Claude made these for me
static double perlin_fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
static double perlin_lerp(double t, double a, double b) { return a + t * (b - a); }
static double perlin_grad(int hash, double x, double y) {
	int h = hash & 7;
	double u = h < 4 ? x : y;
	double v = h < 4 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
}

void generate_terrain_heightmap(World* wrld);


World world_create(Vector_t dims) {

    World wrld;


    uint32_t x_size = (uint32_t)dims.x;
    uint32_t y_size = (uint32_t)dims.y;
    uint32_t z_size = (uint32_t)dims.z;


    wrld.world_dimensions = vector_create(x_size, y_size, z_size);
    wrld.world_size = x_size * y_size * z_size;

    PerlinParams pp = {
        .amplitude = 512.0f / 10,
        .scale = 0.005f,
        .octaves = 6
    };

    wrld.perlin_params = pp;

    wrld.voxels = calloc(wrld.world_size, sizeof(uint8_t));

    wrld.seed = 67;

    return wrld;
}


static void place_test_slab(World* wrld, float heightFraction, float sizeFraction, uint8_t material) {
    uint32_t dimX = (uint32_t)wrld->world_dimensions.x;
    uint32_t dimY = (uint32_t)wrld->world_dimensions.y;
    uint32_t dimZ = (uint32_t)wrld->world_dimensions.z;

    uint32_t slabY = (uint32_t)(dimY * heightFraction);
    uint32_t slabSize = (uint32_t)(dimX * sizeFraction);
    uint32_t slabStart = (dimX - slabSize) / 2;

    for (uint32_t sz = 0; sz < slabSize; sz++) {
        for (uint32_t sx = 0; sx < slabSize; sx++) {
            uint32_t x = slabStart + sx;
            uint32_t z = slabStart + sz;
            uint32_t idx = x + slabY * dimX + z * dimX * dimY;
            wrld->voxels[idx] = material;
        }
    }
}

void world_generate_terrain(World* wrld) {
    generate_terrain_heightmap(wrld);

    // horizontal test slabs, centered in x/z, floating above the terrain
    place_test_slab(wrld, 0.65f, 1.0f / 8.0f, MIRROR);
    place_test_slab(wrld, 0.55f, 1.0f / 10.0f, BLACK_VARNISH);
}



void generate_terrain_heightmap(World* wrld) {
	uint8_t* voxels = wrld->voxels;

	uint32_t dimX = (uint32_t)wrld->world_dimensions.x;
	uint32_t dimY = (uint32_t)wrld->world_dimensions.y;
	uint32_t dimZ = (uint32_t)wrld->world_dimensions.z;

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

			for (int y = 0; y < height; y++) {
				uint32_t idx = x + (uint32_t)y * dimX + z * dimX * dimY;
				voxels[idx] = DIRT; // brown, fills down to the bottom of the grid
			}

			uint32_t topIdx = x + (uint32_t)height * dimX + z * dimX * dimY;
			voxels[topIdx] = GRASS; // green, top block of the column
		}
	}
}


