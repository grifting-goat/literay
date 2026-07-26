#ifndef MATERIAL_H
#define MATERIAL_H

#include "compute_res.h"

typedef enum {
    AIR = 0,
    RED,
    GREEN,
    BLUE,
    MIRROR,
    WHITE_LIGHT,
    GRASS,
    DIRT,
    STONE,
    YELLOW_LIGHT,
    BLACK,
    BLACK_VARNISH,
    PURPLE,
    MATERIAL_COUNT

} MaterialTypes;

static Material material_list[0xFF] = {
    [RED] = {
        .color = {1.0f, 0.0f, 0.0f, 1.0f},
        .emissionColor = {1.0f, 0.0f, 0.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    },
    [GREEN] = {
        .color = {0.0f, 1.0f, 0.0f, 1.0f},
        .emissionColor = {0.0f, 1.0f, 0.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    },
    [BLUE] = {
        .color = {0.0f, 0.0f, 1.0f, 1.0f},
        .emissionColor = {0.0f, 0.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    },
    [MIRROR] = {
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 1.0f,
        .specularProbability = 1.0f,
        .noise = 0.0f
    },
    [WHITE_LIGHT] = {
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionStrength = 10.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    },
    [GRASS] = {
        .color = {0.016f, 0.561f, 0.008f, 1.0f},
        .emissionColor = {0.016f, 0.561f, 0.008f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.4f,
        .specularProbability = 1.0f,
        .noise = 0.03f
    },
    [DIRT] = {
        .color = {0.451f, 0.235f, 0.004f, 1.0f},
        .emissionColor = {0.451f, 0.235f, 0.004f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.04f
    },
    [STONE] = {
        .color = {0.569f, 0.569f, 0.569f, 1.0f},
        .emissionColor = {0.569f, 0.569f, 0.569f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.04f
    },
    [YELLOW_LIGHT] = {
        .color = {1.0f, 1.0f, 0.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 0.0f, 1.0f},
        .emissionStrength = 10.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    },
    [BLACK] = {
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
        .emissionColor = {0.0f, 0.0f, 0.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    },
    [BLACK_VARNISH] = {
        .color = {0.05f, 0.05f, 0.05f, 1.0f},
        .emissionColor = {0.10f, 0.10f, 0.10f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.9f,
        .specularProbability = 0.9f,
        .noise = 0.0f
    },
    [PURPLE] = {
        .color = {1.0f, 0.00f, 1.0f, 1.0f},
        .emissionColor = {1.0f, 0.00f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f
    }
};

#endif //MATERIAL_H
