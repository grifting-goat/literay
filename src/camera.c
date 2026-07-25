#include "camera.h"

Camera camera_create(float fov, float aspect, Vector_t* pos, Vector_t* angle) {
    Camera cam = {0};
    cam.fov = fov;
    cam.aspect = aspect;
    cam.fov_v = fov / 2.0f;
    cam.fov_h = atanf(tanf(cam.fov_v) * aspect);

    if (pos != NULL) {cam.pos = *pos;}
    if (angle != NULL) {cam.angle = *angle;}
    return cam;
}


Camera camera_create_default() {
    Vector_t pos = vector_create(0,1,0);
    Vector_t angle = vector_create(0,0,0);
    return camera_create(103.0f * (M_PI / 180.0f), 16.0f / 9.0f, &pos, &angle);
}

void camera_update_fov(Camera* cam, float fov, float aspect) {
    cam->fov = fov;
    cam->aspect = aspect;
    cam->fov_v = fov / 2.0f;
    cam->fov_h = atanf(tanf(cam->fov_v) * aspect);
}