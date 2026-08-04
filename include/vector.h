#ifndef VECTOR_H
#define VECTOR_H

#include <math.h>
#include <stdint.h>


typedef struct Vector_t {
    float x;
    float y;
    float z;
} Vector_t;

typedef struct uVector_t {
    uint32_t x;
    uint32_t y;
    uint32_t z;
} uVector_t;

typedef struct iVector_t {
    int32_t x;
    int32_t y;
    int32_t z;
} iVector_t;


Vector_t vector_create(float x, float y, float z);
uVector_t uVector_create(uint32_t x, uint32_t y, uint32_t z);
iVector_t iVector_create(int32_t x, int32_t y, int32_t z);

float vector_magnitude(const Vector_t* vec);
Vector_t vector_add(const Vector_t* a, const Vector_t* b);
Vector_t vector_subtract(const Vector_t* a, const Vector_t* b);
Vector_t vector_scale(const Vector_t* vec, float scalar);
float vector_dot(const Vector_t* a, const Vector_t* b);
Vector_t vector_lerp(const Vector_t* a, const Vector_t* b, float t);

uVector_t uVector_add(const uVector_t* a, const uVector_t* b);
uVector_t uVector_subtract(const uVector_t* a, const uVector_t* b);
uVector_t uVector_scale(const uVector_t* vec, uint32_t scalar);

iVector_t iVector_add(const iVector_t* a, const iVector_t* b);
iVector_t iVector_subtract(const iVector_t* a, const iVector_t* b);
iVector_t iVector_scale(const iVector_t* vec, int32_t scalar);





#endif //VECTOR_H