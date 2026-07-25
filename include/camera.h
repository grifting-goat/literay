#ifndef CAMERA_H
#define CAMERA_H


#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>

#include "vector.h"


typedef struct Camera {

    Vector_t pos;
    Vector_t angle;

    float fov;

    float fov_v;
    float fov_h;

    float aspect;

} Camera;

Camera camera_create_default();
Camera camera_create(float fov, float aspect, Vector_t* pos, Vector_t* angle);
void camera_update_fov(Camera* cam, float fov, float aspect);

#endif //CAMERA_H