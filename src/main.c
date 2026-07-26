#include <stdio.h>
#include <string.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"



#include "camera.h"
#include "display.h"


void window_camera_buffer_update(Window_t* window, Camera* c); //temp location
void window_world_buffer_load(Window_t* window); //temp location
void camera_position_controller(Window_t* window, Camera* cam, float frameTime); //temp location

int main() {

    Vector_t spawn = vector_create((VOXEL_GRID_DIM / 2), (VOXEL_GRID_DIM / 2), (VOXEL_GRID_DIM / 2));
    Vector_t inital_angle = {0.0f};

    Window_t window = window_create();
    Camera cam = camera_create(103.0f * (M_PI / 180.0f), (float)window.width / (float)window.height, &spawn, &inital_angle);

    window_attach_device(&window);
    window_world_buffer_load(&window);

    glfwSetInputMode(window.glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	double lastMouseX = 0.0, lastMouseY = 0.0;
	glfwGetCursorPos(window.glfw_window, &lastMouseX, &lastMouseY);

    float roll = 0.0f;
	float pitch = 0.0f;
	float yaw = 0.0f;
	const float mouseSensitivity = 0.0007f;
    const float pitchLimit = 1.55334f;

    double lastFrameTime = glfwGetTime();

    while (!window_should_close(&window)) {
        window_poll_events(&window);

        double currentFrameTime = glfwGetTime();
        float frameTime = (float)(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

        //mouse stuff
        double mouseX, mouseY;
		glfwGetCursorPos(window.glfw_window, &mouseX, &mouseY);
		double deltaX = mouseX - lastMouseX;
		double deltaY = mouseY - lastMouseY;
		lastMouseX = mouseX;
		lastMouseY = mouseY;

		yaw += (float)deltaX * mouseSensitivity;
		pitch += (float)deltaY * mouseSensitivity;
		if (pitch > pitchLimit) { pitch = pitchLimit; }
		if (pitch < -pitchLimit) { pitch = -pitchLimit; }


		cam.angle.x = pitch;
		cam.angle.y = yaw;
		cam.angle.z = roll;

        camera_position_controller(&window, &cam, frameTime);

        window_camera_buffer_update(&window, &cam);
        window_render(&window);

    }


}

void window_world_buffer_load(Window_t* window) {
    VkDeviceSize world_grid_size = (VkDeviceSize)VOXEL_GRID_DIM * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
    uint8_t* voxels = (uint8_t*)window->vk_objects.worldGridBuffer.mapped;
    memset(voxels, 0, world_grid_size);

    uint32_t cubeSize = VOXEL_GRID_DIM / 4;
    uint32_t cubeStart = (VOXEL_GRID_DIM - cubeSize) / 2;
    for (uint32_t cz = 0; cz < cubeSize; cz++) {
        for (uint32_t cy = 0; cy < cubeSize; cy++) {
            for (uint32_t cx = 0; cx < cubeSize; cx++) {
                uint32_t vx = cubeStart + cx;
                uint32_t vy = cubeStart + cy;
                uint32_t vz = cubeStart + cz;
                uint32_t idx = vx + vy * VOXEL_GRID_DIM + vz * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
                voxels[idx] = rand() % 300 ? 0 : (rand() % 4) + 1;
            }
        }
    }


    Material* materials = (Material*)window->vk_objects.materialProperitesBuffer.mapped;

    materials[1].color[0] = 1.0f;
    materials[1].color[1] = 0.0f;
    materials[1].color[2] = 0.0f;
    materials[1].color[3] = 1.0f;

    materials[2].color[0] = 0.0f;
    materials[2].color[1] = 1.0f;
    materials[2].color[2] = 0.0f;
    materials[2].color[3] = 1.0f;

    materials[3].color[0] = 0.0f;
    materials[3].color[1] = 0.0f;
    materials[3].color[2] = 1.0f;
    materials[3].color[3] = 1.0f;

    materials[4].color[0] = 1.0f;
    materials[4].color[1] = 1.0f;
    materials[4].color[2] = 1.0f;
    materials[4].color[3] = 1.0f;
    

    for (uint32_t i = 1; i <= 4; i++) {
        materials[i].emissionColor[0] = materials[i].color[0];
        materials[i].emissionColor[1] = materials[i].color[1];
        materials[i].emissionColor[2] = materials[i].color[2];
        materials[i].emissionColor[3] = materials[i].color[3];
        materials[i].emissionStrength = 0.0f;
        materials[i].smoothness = 0.0f;
        materials[i].specularProbability = 0.0f;
    }

    materials[1].emissionStrength = 10.0f;
    materials[4].smoothness = 0.9f;
    materials[4].specularProbability = 1.0f;
}



void window_camera_buffer_update(Window_t* window, Camera* c) {
	uint32_t frame_res_index = window->vk_objects.frameCounter % MAX_FRAMES_IN_FLIGHT;
	CameraData* ubo = (CameraData*)window->vk_objects.cameraDataBuffer[frame_res_index].mapped;

	ubo->camera_position[0] = c->pos.x;
	ubo->camera_position[1] = c->pos.y;
	ubo->camera_position[2] = c->pos.z;

	ubo->camera_rotation[0] = c->angle.x;
	ubo->camera_rotation[1] = c->angle.y;
	ubo->camera_rotation[2] = c->angle.z;

	ubo->tan_fov_v = tanf(c->fov_v);
	ubo->tan_fov_h = tanf(c->fov_h);
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