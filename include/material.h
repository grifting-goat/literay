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

#endif //MATERIAL_H
