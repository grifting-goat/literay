#ifndef VECTOR_H
#define VECTOR_H

#include <math.h>

typedef struct Vector_t {
    float x;
    float y;
    float z;
} Vector_t;


Vector_t vector_create(float x, float y, float z);

float vector_magnitude(const Vector_t* vec);
Vector_t vector_add(const Vector_t* a, const Vector_t* b);
Vector_t vector_subtract(const Vector_t* a, const Vector_t* b);
Vector_t vector_scale(const Vector_t* vec, float scalar);
float vector_dot(const Vector_t* a, const Vector_t* b);
Vector_t vector_lerp(const Vector_t* a, const Vector_t* b, float t);





#endif //VECTOR_H