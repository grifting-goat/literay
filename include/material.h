#ifndef MATERIAL_H
#define MATERIAL_H

#include "compute_res.h"

#define PALETTE_MATERIAL_COUNT 128

typedef enum {
    AIR = 0,
    RED_LIGHT,
    GREEN_LIGHT,
    BLUE_LIGHT,
    MIRROR,
    WHITE_LIGHT,
    GRASS,
    DIRT,
    STONE,
    YELLOW_LIGHT,
    BLACK,
    BLACK_VARNISH,
    PURPLE,
    WATER,
    SAND,
    MARBLE,
    COPPER,
    PALETTE_MATERIAL_BASE,
    MATERIAL_COUNT = PALETTE_MATERIAL_BASE + PALETTE_MATERIAL_COUNT

} MaterialTypes;

extern Material material_list[0xFF];

void material_palette_create();

// separate material list entities read from; the world keeps using material_list
#define ENTITY_PALETTE_COLOR_COUNT 250

typedef enum {
    ENTITY_COLOR_BASE = 1, // 0 stays AIR (no voxel), matching material_list's convention
    ENTITY_WHITE_LIGHT = ENTITY_COLOR_BASE + ENTITY_PALETTE_COLOR_COUNT,
    ENTITY_SKIN, // varnish finish
    ENTITY_BLACK,
    ENTITY_MIRROR,
    ENTITY_MATERIAL_COUNT

} EntityMaterialTypes;

extern Material entity_material_list[0xFF];

void entity_material_palette_create();

#endif //MATERIAL_H
