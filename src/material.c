#include <math.h>

#include "material.h"

// standard HSV -> RGB conversion, h in degrees [0,360), s and v in [0,1]
static void hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b) {
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    if (hp < 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    float m = v - c;
    *r = r1 + m; *g = g1 + m; *b = b1 + m;
}

Material material_list[0xFF] = {
    [RED_LIGHT] = {
        .color = {1.0f, 0.0f, 0.0f, 1.0f},
        .emissionColor = {1.0f, 0.0f, 0.0f, 1.0f},
        .emissionStrength = 1.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [GREEN_LIGHT] = {
        .color = {0.0f, 1.0f, 0.0f, 1.0f},
        .emissionColor = {0.0f, 1.0f, 0.0f, 1.0f},
        .emissionStrength = 1.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [BLUE_LIGHT] = {
        .color = {0.0f, 0.0f, 1.0f, 1.0f},
        .emissionColor = {0.0f, 0.0f, 1.0f, 1.0f},
        .emissionStrength = 1.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [MIRROR] = {
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 1.0f,
        .specularProbability = 1.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [WHITE_LIGHT] = {
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionStrength = 10.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [GRASS] = {
        .color = {0.016f, 0.561f, 0.008f, 1.0f},
        .emissionColor = {0.016f, 0.561f, 0.008f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.4f,
        .specularProbability = 1.0f,
        .noise = 0.03f,
        .opaque = 1.0f
    },
    [DIRT] = {
        .color = {0.451f, 0.235f, 0.004f, 1.0f},
        .emissionColor = {0.451f, 0.235f, 0.004f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.04f,
        .opaque = 1.0f
    },
    [STONE] = {
        .color = {0.569f, 0.569f, 0.569f, 1.0f},
        .emissionColor = {0.569f, 0.569f, 0.569f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.04f,
        .opaque = 1.0f
    },
    [YELLOW_LIGHT] = {
        .color = {1.0f, 1.0f, 0.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 0.0f, 1.0f},
        .emissionStrength = 10.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [BLACK] = {
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
        .emissionColor = {0.0f, 0.0f, 0.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [BLACK_VARNISH] = {
        .color = {0.05f, 0.05f, 0.05f, 1.0f},
        .emissionColor = {0.10f, 0.10f, 0.10f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.9f,
        .specularProbability = 0.9f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [PURPLE] = {
        .color = {1.0f, 0.00f, 1.0f, 1.0f},
        .emissionColor = {1.0f, 0.00f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.0f,
        .opaque = 1.0f
    },
    [WATER] = {
        .color = {0.424f, 0.761f, 0.89f, 1.0f},
        .emissionColor = {0.424f, 0.761f, 0.89f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 1.0f,
        .specularProbability = 0.9f,
        .noise = 0.02f,
        .opaque = 1.0f
    },
    [SAND] = {
        .color = {0.929f, 0.855f, 0.11f, 1.0f},
        .emissionColor = {0.929f, 0.855f, 0.11f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.0f,
        .specularProbability = 0.0f,
        .noise = 0.1f,
        .opaque = 1.0f
    },
    [MARBLE] = {
        .color = {0.90f, 0.90f, 0.90f, 1.0f},
        .emissionColor = {0.90f, 0.90f, 0.90f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.98f,
        .specularProbability = 0.07f,
        .noise = 0.05f,
        .opaque = 1.0f
    },
    [COPPER] = {
        .color = {0.722f, 0.451f, 0.2f, 1.0f},
        .emissionColor = {0.722f, 0.451f, 0.2f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.7f,
        .specularProbability = 0.9f,
        .noise = 0.02f,
        .opaque = 1.0f
    },
    [RUBY] = {
        .color = {0.929f, 0.235f, 0.149f, 1.0f},
        .emissionColor = {0.929f, 0.235f, 0.149f, 1.0f},
        .emissionStrength = 1.0f,
        .smoothness = 0.8f,
        .specularProbability = 0.9f,
        .noise = 0.05f,
        .opaque = 1.0f
    },
    [GLASS] = {
        .color = {1.0f, 0.0f, 0.0f, 1.0f},
        .emissionColor = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
        .smoothness = 0.2f,
        .specularProbability = 0.05f,
        .noise = 0.05f,
        .opaque = 0.1f
    },
};

void material_palette_create() {

    static const float VALUES[4] = {0.3f, 0.55f, 0.8f, 1.0f};
    static const float SATURATIONS[2] = {0.55f, 0.95f};

    for (int i = PALETTE_MATERIAL_BASE; i < MATERIAL_COUNT; i++) {
        int hueIdx = i % 16;
        int valIdx = (i / 16) % 4;
        int satIdx = i / 64;

        float hue = hueIdx * (360.0f / 16.0f);
        float val = VALUES[valIdx];
        float sat = SATURATIONS[satIdx];

        float rf, gf, bf;
        hsv_to_rgb(hue, sat, val, &rf, &gf, &bf);

        Material m = {0};
        m.color[0] = rf; m.color[1] = gf; m.color[2] = bf; m.color[3] = 1.0f;
        m.emissionColor[0] = rf; m.emissionColor[1] = gf; m.emissionColor[2] = bf; m.emissionColor[3] = 1.0f;
        m.emissionStrength = 0.0f;
        m.smoothness = 0.0f;
        m.specularProbability = 0.0f;
        m.noise = 0.01f;
        m.opaque = 1.0f;

        material_list[i] = m;
    }
}
