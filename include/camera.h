#ifndef CAMERA_H
#define CAMERA_H


#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vector.h"


typedef struct Camera {

    Vector_t pos;
    Vector_t angle;

    float fov;

    float fov_v;
    float fov_h;

    float aspect;

    Vector_t prevPos;
    Vector_t prevAngle;
    bool hasPrevCamera;

} Camera;

Camera camera_create_default();
Camera camera_create(float fov, float aspect, Vector_t* pos, Vector_t* angle);
void camera_update_fov(Camera* cam, float fov, float aspect);

// true if pos/angle differ from the last call, false otherwise; always updates prev state as a side effect
bool camera_check_moved(Camera* cam);

struct Window_t; 
void camera_position_controller(struct Window_t* window, Camera* cam, float frameTime);

#endif //CAMERA_H