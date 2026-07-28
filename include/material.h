#ifndef MATERIAL_H
#define MATERIAL_H

#include "compute_res.h"

#define MAX_MATERIAL_COUNT 0xFF

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
    RUBY,
    PALETTE_MATERIAL_BASE,
    MATERIAL_COUNT = MAX_MATERIAL_COUNT

} MaterialTypes;

extern Material material_list[0xFF];

void material_palette_create();

#endif //MATERIAL_H
