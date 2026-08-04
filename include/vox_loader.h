#ifndef VOX_LOADER_H
#define VOX_LOADER_H

#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"
#include "material.h"
#include "model.h"

//dont tell anyone i genuenly just asked claude to make this 

#define VOX_MAX_MODELS 64
#define VOX_MAX_NODES 256
#define VOX_MAX_GROUP_CHILDREN 64
#define VOX_MAX_PLACEMENTS 256


typedef struct {
    uint32_t dim[3]; // vox-space (x,y,z), Z-up, as read from SIZE
    uint8_t* xyzi; // num_voxels * (x, y, z, colorIndex)
    int32_t num_voxels;
} VoxRawModel;

typedef struct {
    int32_t node_id;
    int32_t child_id;
    bool has_t;
    int32_t t[3];
    bool has_r;
    int r[3][3];
} VoxTransformNode;

typedef struct {
    int32_t node_id;
    int32_t num_children;
    int32_t child_ids[VOX_MAX_GROUP_CHILDREN];
} VoxGroupNode;

typedef struct {
    int32_t node_id;
    int32_t model_id;
} VoxShapeNode;

// a resolved shape instance: which model, and its composed world transform (still in vox-space)
typedef struct {
    int32_t model_id;
    int32_t t[3];
    int r[3][3];
} VoxPlacement;

static const int VOX_IDENTITY_R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

static int32_t vox_read_i32(FILE* fp) {
    int32_t v = 0;
    fread(&v, 4, 1, fp);
    return v;
}

// caller must free the returned buffer
static char* vox_read_string(FILE* fp) {
    int32_t len = vox_read_i32(fp);
    if (len < 0 || len > 65536) { // sane upper bound; real vox metadata strings are tiny
        len = 0; // treat a corrupt/garbage length as an empty string instead of malloc'ing garbage
    }
    char* s = malloc((size_t)len + 1);
    fread(s, 1, (size_t)len, fp);
    s[len] = '\0';
    return s;
}


static void vox_decode_rotation(uint8_t packed, int r[3][3]) {
    memset(r, 0, sizeof(int) * 9);
    int row0_col = packed & 3;
    int row1_col = (packed >> 2) & 3;
    int row2_col = 3 - row0_col - row1_col;
    r[0][row0_col] = (packed & 0x10) ? -1 : 1;
    r[1][row1_col] = (packed & 0x20) ? -1 : 1;
    r[2][row2_col] = (packed & 0x40) ? -1 : 1;
}


static void vox_read_transform_dict(FILE* fp, bool* has_t, int32_t t[3], bool* has_r, int r[3][3]) {
    *has_t = false;
    *has_r = false;
    int32_t num_pairs = vox_read_i32(fp);
    for (int32_t i = 0; i < num_pairs; i++) {
        char* key = vox_read_string(fp);
        char* value = vox_read_string(fp);

        if (strcmp(key, "_t") == 0) {
            sscanf(value, "%d %d %d", &t[0], &t[1], &t[2]);
            *has_t = true;
        }
        else if (strcmp(key, "_r") == 0) {
            vox_decode_rotation((uint8_t)atoi(value), r);
            *has_r = true;
        }

        free(key);
        free(value);
    }
}

static VoxTransformNode* vox_find_transform(VoxTransformNode* nodes, int32_t count, int32_t id) {
    for (int32_t i = 0; i < count; i++) { if (nodes[i].node_id == id) { return &nodes[i]; } }
    return NULL;
}
static VoxGroupNode* vox_find_group(VoxGroupNode* nodes, int32_t count, int32_t id) {
    for (int32_t i = 0; i < count; i++) { if (nodes[i].node_id == id) { return &nodes[i]; } }
    return NULL;
}
static VoxShapeNode* vox_find_shape(VoxShapeNode* nodes, int32_t count, int32_t id) {
    for (int32_t i = 0; i < count; i++) { if (nodes[i].node_id == id) { return &nodes[i]; } }
    return NULL;
}

static void vox_mat3_mul(int a[3][3], int b[3][3], int out[3][3]) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
}
static void vox_mat3_apply(int m[3][3], const int32_t v[3], int32_t out[3]) {
    for (int i = 0; i < 3; i++) {
        out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    }
}

static uint8_t vox_match_palette_color(uint8_t r, uint8_t g, uint8_t b) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    uint8_t best = PALETTE_MATERIAL_BASE;
    float bestDist = FLT_MAX;
    for (int i = PALETTE_MATERIAL_BASE; i < MATERIAL_COUNT; i++) {
        Material* m = &material_list[i];
        float dr = m->color[0] - rf;
        float dg = m->color[1] - gf;
        float db = m->color[2] - bf;
        float dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = (uint8_t)(i);
        }
    }
    return best;
}

static uint8_t vox_match_light_color(uint8_t r, uint8_t g, uint8_t b) {
    static const MaterialTypes lights[] = { WHITE_LIGHT, RED_LIGHT, GREEN_LIGHT, BLUE_LIGHT, YELLOW_LIGHT };
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    uint8_t best = WHITE_LIGHT;
    float bestDist = FLT_MAX;
    for (size_t i = 0; i < sizeof(lights) / sizeof(lights[0]); i++) {
        Material* m = &material_list[lights[i]];
        float dr = m->color[0] - rf;
        float dg = m->color[1] - gf;
        float db = m->color[2] - bf;
        float dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = (uint8_t)lights[i];
        }
    }
    return best;
}


static void vox_walk_node(
    int32_t node_id, int32_t parent_t[3], int parent_r[3][3],
    VoxTransformNode* trns, int32_t num_trns,
    VoxGroupNode* grps, int32_t num_grps,
    VoxShapeNode* shps, int32_t num_shps,
    VoxPlacement* placements, int32_t* num_placements
) {
    int32_t world_t[3] = {parent_t[0], parent_t[1], parent_t[2]};
    int world_r[3][3];
    memcpy(world_r, parent_r, sizeof(world_r));
    int32_t next_id = node_id;

    VoxTransformNode* trn = vox_find_transform(trns, num_trns, node_id);
    if (trn != NULL) {
        int32_t zero_t[3] = {0, 0, 0};
        int32_t rotated_t[3];
        vox_mat3_apply(parent_r, trn->has_t ? trn->t : zero_t, rotated_t);
        world_t[0] = parent_t[0] + rotated_t[0];
        world_t[1] = parent_t[1] + rotated_t[1];
        world_t[2] = parent_t[2] + rotated_t[2];

        int local_r[3][3];
        if (trn->has_r) { memcpy(local_r, trn->r, sizeof(local_r)); }
        else { memcpy(local_r, VOX_IDENTITY_R, sizeof(local_r)); }
        vox_mat3_mul(parent_r, local_r, world_r);

        next_id = trn->child_id;
    }

    VoxGroupNode* grp = vox_find_group(grps, num_grps, next_id);
    if (grp != NULL) {
        for (int32_t i = 0; i < grp->num_children; i++) {
            vox_walk_node(grp->child_ids[i], world_t, world_r, trns, num_trns, grps, num_grps, shps, num_shps, placements, num_placements);
        }
        return;
    }

    VoxShapeNode* shp = vox_find_shape(shps, num_shps, next_id);
    if (shp != NULL && *num_placements < VOX_MAX_PLACEMENTS) {
        VoxPlacement* p = &placements[(*num_placements)++];
        p->model_id = shp->model_id;
        memcpy(p->t, world_t, sizeof(p->t));
        memcpy(p->r, world_r, sizeof(p->r));
    }
}


static Model_t vox_load_model_data(const char* path, MaterialTypes type, bool matchPalette) {
    Model_t model = {0};

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        printf("Cant open vox file: %s\n", path);
        return model;
    }

    char magic[4];
    fread(magic, 1, 4, fp);
    vox_read_i32(fp); // version
    if (memcmp(magic, "VOX ", 4) != 0) {
        printf("Not a vox file: %s\n", path);
        fclose(fp);
        return model;
    }

    fseek(fp, 12, SEEK_CUR); // MAIN chunk header; it has no content of its own

    VoxRawModel models[VOX_MAX_MODELS] = {0};
    int32_t num_models = 0;

    VoxTransformNode transform_nodes[VOX_MAX_NODES];
    int32_t num_transform_nodes = 0;
    VoxGroupNode group_nodes[VOX_MAX_NODES];
    int32_t num_group_nodes = 0;
    VoxShapeNode shape_nodes[VOX_MAX_NODES];
    int32_t num_shape_nodes = 0;

    uint8_t vox_palette[256][4] = {0}; // index i holds the RGBA for colorIndex i+1 (see vox spec's off-by-one)
    bool has_palette = false;

    bool vox_emissive[256] = {0}; // indexed directly by material id, which shares numbering with colorIndex

    char chunk_id[4];
    int32_t content_size, children_size;
    while (fread(chunk_id, 1, 4, fp) == 4) {
        fread(&content_size, 4, 1, fp);
        fread(&children_size, 4, 1, fp);
        long chunk_end = ftell(fp) + content_size + children_size;

        if (memcmp(chunk_id, "RGBA", 4) == 0) {
            fread(vox_palette, 1, 256 * 4, fp);
            has_palette = true;
        }
        else if (memcmp(chunk_id, "MATL", 4) == 0) {
            int32_t material_id = vox_read_i32(fp);
            int32_t num_pairs = vox_read_i32(fp);
            bool is_emit = false;
            float emit = 0.0f;
            for (int32_t i = 0; i < num_pairs; i++) {
                char* key = vox_read_string(fp);
                char* value = vox_read_string(fp);
                if (strcmp(key, "_type") == 0 && strcmp(value, "_emit") == 0) { is_emit = true; }
                else if (strcmp(key, "_emit") == 0) { emit = (float)atof(value); }
                free(key);
                free(value);
            }
            if (material_id >= 0 && material_id < 256) {
                vox_emissive[material_id] = is_emit && emit >= 1.0f;
            }
        }
        else if (memcmp(chunk_id, "SIZE", 4) == 0 && num_models < VOX_MAX_MODELS) {
            fread(models[num_models].dim, 4, 3, fp);
        }
        else if (memcmp(chunk_id, "XYZI", 4) == 0 && num_models < VOX_MAX_MODELS) {
            int32_t n = vox_read_i32(fp);
            models[num_models].num_voxels = n;
            models[num_models].xyzi = malloc((size_t)n * 4);
            fread(models[num_models].xyzi, 4, n, fp);
            num_models++; // SIZE always immediately precedes its paired XYZI
        }
        else if (memcmp(chunk_id, "nTRN", 4) == 0 && num_transform_nodes < VOX_MAX_NODES) {
            VoxTransformNode* node = &transform_nodes[num_transform_nodes++];
            node->node_id = vox_read_i32(fp);
            bool dt; int32_t dti[3]; bool dr; int dri[3][3];
            vox_read_transform_dict(fp, &dt, dti, &dr, dri); // node attributes (e.g. "_name"), discarded
            node->child_id = vox_read_i32(fp);
            vox_read_i32(fp); // reserved, always -1
            vox_read_i32(fp); // layer id
            int32_t num_frames = vox_read_i32(fp);
            node->has_t = false;
            node->has_r = false;
            for (int32_t f = 0; f < num_frames; f++) {
                bool ht, hr; int32_t t[3]; int r[3][3];
                vox_read_transform_dict(fp, &ht, t, &hr, r);
                if (f == 0) { // multi-frame animation isn't handled; only the first frame is used
                    if (ht) { node->has_t = true; memcpy(node->t, t, sizeof(t)); }
                    if (hr) { node->has_r = true; memcpy(node->r, r, sizeof(r)); }
                }
            }
        }
        else if (memcmp(chunk_id, "nGRP", 4) == 0 && num_group_nodes < VOX_MAX_NODES) {
            VoxGroupNode* node = &group_nodes[num_group_nodes++];
            node->node_id = vox_read_i32(fp);
            bool dt; int32_t dti[3]; bool dr; int dri[3][3];
            vox_read_transform_dict(fp, &dt, dti, &dr, dri); // node attributes, discarded
            node->num_children = vox_read_i32(fp);
            if (node->num_children > VOX_MAX_GROUP_CHILDREN) { node->num_children = VOX_MAX_GROUP_CHILDREN; }
            fread(node->child_ids, 4, node->num_children, fp);
        }
        else if (memcmp(chunk_id, "nSHP", 4) == 0 && num_shape_nodes < VOX_MAX_NODES) {
            VoxShapeNode* node = &shape_nodes[num_shape_nodes++];
            node->node_id = vox_read_i32(fp);
            bool dt; int32_t dti[3]; bool dr; int dri[3][3];
            vox_read_transform_dict(fp, &dt, dti, &dr, dri); // node attributes, discarded
            int32_t num_shape_models = vox_read_i32(fp);
            node->model_id = 0;
            for (int32_t m = 0; m < num_shape_models; m++) {
                int32_t model_id = vox_read_i32(fp);
                bool dt2; int32_t dti2[3]; bool dr2; int dri2[3][3];
                vox_read_transform_dict(fp, &dt2, dti2, &dr2, dri2); // model attributes (e.g. "_f"), discarded
                if (m == 0) { node->model_id = model_id; }
            }
        }

        fseek(fp, chunk_end, SEEK_SET); // resync regardless of what the branch above actually consumed
    }

    fclose(fp);

    if (num_models == 0) {
        printf("vox file has no models: %s\n", path);
        return model;
    }

    // resolve every shape's world transform; files without a scene graph get one identity placement
    VoxPlacement placements[VOX_MAX_PLACEMENTS];
    int32_t num_placements = 0;
    if (num_transform_nodes > 0) {
        int32_t root_t[3] = {0, 0, 0};
        int root_r[3][3];
        memcpy(root_r, VOX_IDENTITY_R, sizeof(root_r));
        vox_walk_node(0, root_t, root_r, transform_nodes, num_transform_nodes, group_nodes, num_group_nodes, shape_nodes, num_shape_nodes, placements, &num_placements);
    }
    if (num_placements == 0) {
        placements[0].model_id = 0;
        placements[0].t[0] = placements[0].t[1] = placements[0].t[2] = 0;
        memcpy(placements[0].r, VOX_IDENTITY_R, sizeof(placements[0].r));
        num_placements = 1;
    }

    // bounding box of every placed+rotated model, in vox-space
    int32_t global_min[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t global_max[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    bool any_valid_placement = false;
    for (int32_t p = 0; p < num_placements; p++) {
        VoxPlacement* placement = &placements[p];
        if (placement->model_id < 0 || placement->model_id >= num_models) { continue; }
        VoxRawModel* rawModel = &models[placement->model_id];
        any_valid_placement = true;

        for (int32_t corner = 0; corner < 8; corner++) {
            int32_t local[3] = {
                (corner & 1) ? (int32_t)rawModel->dim[0] : 0,
                (corner & 2) ? (int32_t)rawModel->dim[1] : 0,
                (corner & 4) ? (int32_t)rawModel->dim[2] : 0
            };
            int32_t rotated[3];
            vox_mat3_apply(placement->r, local, rotated);
            for (int axis = 0; axis < 3; axis++) {
                int32_t w = placement->t[axis] + rotated[axis];
                if (w < global_min[axis]) { global_min[axis] = w; }
                if (w > global_max[axis]) { global_max[axis] = w; }
            }
        }
    }

    if (!any_valid_placement) {
        printf("vox file has no valid placements: %s\n", path);
        for (int32_t i = 0; i < num_models; i++) { free(models[i].xyzi); }
        return model;
    }

    // this engine is Y-up; remap vox-space (x,y,z) -> local (x,z,y)
    uint32_t dim[3] = {
        (uint32_t)(global_max[0] - global_min[0]),
        (uint32_t)(global_max[2] - global_min[2]),
        (uint32_t)(global_max[1] - global_min[1])
    };
    uint32_t size = dim[0] * dim[1] * dim[2];

    model.size = size;
    model.voxels = calloc(size, sizeof(uint8_t));
    if (model.voxels == NULL) {
        printf("vox file bounding box too large to allocate (%u voxels): %s\n", size, path);
        model.size = 0;
        for (int32_t i = 0; i < num_models; i++) { free(models[i].xyzi); }
        return model;
    }
    model.dimensions = uVector_create(dim[0], dim[1], dim[2]);

    for (int32_t p = 0; p < num_placements; p++) {
        VoxPlacement* placement = &placements[p];
        if (placement->model_id < 0 || placement->model_id >= num_models) { continue; }
        VoxRawModel* rawModel = &models[placement->model_id];

        for (int32_t i = 0; i < rawModel->num_voxels; i++) {
            int32_t local[3] = {
                rawModel->xyzi[i * 4 + 0],
                rawModel->xyzi[i * 4 + 1],
                rawModel->xyzi[i * 4 + 2]
            };
            uint8_t colorIndex = rawModel->xyzi[i * 4 + 3];
            int32_t rotated[3];
            vox_mat3_apply(placement->r, local, rotated);

            uint32_t wx = (uint32_t)(placement->t[0] + rotated[0] - global_min[0]);
            uint32_t wy = (uint32_t)(placement->t[2] + rotated[2] - global_min[2]);
            uint32_t wz = (uint32_t)(placement->t[1] + rotated[1] - global_min[1]);

            uint32_t idx = wx + wy * dim[0] + wz * dim[0] * dim[1];

            uint8_t material = (uint8_t)type;
            if (matchPalette && has_palette && colorIndex >= 1) {
                uint8_t* rgba = vox_palette[colorIndex - 1];
                material = vox_match_palette_color(rgba[0], rgba[1], rgba[2]);
            }
            if (colorIndex < 256 && vox_emissive[colorIndex]) { // fully emissive materials override color, matched to nearest light material
                material = WHITE_LIGHT;
                if (has_palette && colorIndex >= 1) {
                    uint8_t* rgba = vox_palette[colorIndex - 1];
                    material = vox_match_light_color(rgba[0], rgba[1], rgba[2]);
                }
            }
            model.voxels[idx] = material;
        }
    }

    for (int32_t i = 0; i < num_models; i++) {
        free(models[i].xyzi);
    }
    return model;
}

static Model_t vox_load_model(const char* path, uint8_t up_axis, uint8_t cardinal_axis, MaterialTypes material, bool colorMatch) {
    Model_t model = vox_load_model_data(path, material, colorMatch);
    model.up_axis = up_axis;
    model.cardinal_axis = cardinal_axis;
    return model;
}
#endif //VOX_LOADER_H
