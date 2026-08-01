#include "vector.h"


Vector_t vector_create(float x, float y, float z) {
    return (Vector_t){x, y, z};

}

uVector_t uVector_create(uint32_t x, uint32_t y, uint32_t z) {
    return (uVector_t){x, y, z};
}

iVector_t iVector_create(int32_t x, int32_t y, int32_t z) {
    return (iVector_t){x, y, z};
}

float vector_magnitude(const Vector_t* vec) {
    double mag2 = ((double)vec->x * vec->x) + ((double)vec->y * vec->y) + ((double)vec->z * vec->z); //avoiding some headache
    return (float)sqrt(mag2);
}

Vector_t vector_add(const Vector_t* a, const Vector_t* b) {
    return (Vector_t){a->x + b->x, a->y + b->y, a->z + b->z};
}
Vector_t vector_subtract(const Vector_t* a, const Vector_t* b) {
    return (Vector_t){a->x - b->x, a->y - b->y, a->z - b->z};
}

Vector_t vector_scale(const Vector_t* vec, const float scalar) {
    return (Vector_t){vec->x * scalar, vec->y * scalar, vec->z * scalar};
}

float vector_dot(const Vector_t* a, const Vector_t* b) {
    return (a->x * b->x) + (a->y * b->y) + (a->z * b->z);
}

Vector_t vector_lerp(const Vector_t* a, const Vector_t* b, const float t) {
    return (Vector_t){
        a->x + (b->x - a->x) * t,
        a->y + (b->y - a->y) * t,
        a->z + (b->z - a->z) * t
    };
}