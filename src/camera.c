#include "camera.h"
#include "display.h"

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

bool camera_check_moved(Camera* cam) {
    const float moveEpsilon = 1e-3f;
    bool moved = !cam->hasPrevCamera ||
        fabsf(cam->pos.x - cam->prevPos.x) > moveEpsilon ||
        fabsf(cam->pos.y - cam->prevPos.y) > moveEpsilon ||
        fabsf(cam->pos.z - cam->prevPos.z) > moveEpsilon ||
        fabsf(cam->angle.x - cam->prevAngle.x) > moveEpsilon ||
        fabsf(cam->angle.y - cam->prevAngle.y) > moveEpsilon ||
        fabsf(cam->angle.z - cam->prevAngle.z) > moveEpsilon;

    cam->prevPos = cam->pos;
    cam->prevAngle = cam->angle;
    cam->hasPrevCamera = true;

    return moved;
}

void camera_position_controller(Window_t* window, Camera* cam, float frameTime) {
	float yaw = cam->angle.y;
	float forwardX = -sinf(yaw);
	float forwardZ = cosf(yaw);
	float rightX = -cosf(yaw);
	float rightZ = -sinf(yaw);

	float moveSpeed = 10.0f;

	float moveX = 0.0f;
	float moveZ = 0.0f;

	if (glfwGetKey(window->glfw_window, GLFW_KEY_W) == GLFW_PRESS) { moveX += forwardX; moveZ += forwardZ; }
	if (glfwGetKey(window->glfw_window, GLFW_KEY_S) == GLFW_PRESS) { moveX -= forwardX; moveZ -= forwardZ; }
	if (glfwGetKey(window->glfw_window, GLFW_KEY_D) == GLFW_PRESS) { moveX += rightX; moveZ += rightZ; }
	if (glfwGetKey(window->glfw_window, GLFW_KEY_A) == GLFW_PRESS) { moveX -= rightX; moveZ -= rightZ; }

	if (glfwGetKey(window->glfw_window, GLFW_KEY_E) == GLFW_PRESS) { cam->pos.y += 5.0f * frameTime; }
	if (glfwGetKey(window->glfw_window, GLFW_KEY_Q) == GLFW_PRESS) { cam->pos.y -= 5.0f * frameTime; }

	if (glfwGetKey(window->glfw_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) { moveSpeed = 25.0f; }

	if (glfwGetKey(window->glfw_window, GLFW_KEY_G) == GLFW_PRESS) {
		cam->pos.x = VOXEL_GRID_DIM / 2;
		cam->pos.y = VOXEL_GRID_DIM / 2;
		cam->pos.z = VOXEL_GRID_DIM / 2;
	}

	float moveLenSq = moveX * moveX + moveZ * moveZ;
	if (moveLenSq > 0.0f) {
		float invLen = 1.0f / sqrtf(moveLenSq);
		moveX *= invLen;
		moveZ *= invLen;

		cam->pos.x += moveX * moveSpeed * frameTime;
		cam->pos.z += moveZ * moveSpeed * frameTime;
	}
}