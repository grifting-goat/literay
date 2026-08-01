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
#include "material.h" //material_list
#include "model.h"

#include "world.h"


int main() {

    Vector_t spawn = vector_create((VOXEL_GRID_DIM / 2), (VOXEL_GRID_DIM / 2), (VOXEL_GRID_DIM / 2));
    Vector_t inital_angle = {0.0f};

    Window_t window = window_create();
    Camera cam = camera_create(103.0f * (M_PI / 180.0f), (float)window.width / (float)window.height, &spawn, &inital_angle);

	material_palette_create();

	model_instance_list[0] = model_create_from_vox("./res/models/cute.vox" , 0, 0, AIR, true);


	uint32_t wrld_dim[3] = { VOXEL_GRID_DIM, VOXEL_GRID_DIM, VOXEL_GRID_DIM };
	World wrld = world_create(wrld_dim);
	world_generate_terrain(&wrld);
	world_generate_structures(&wrld);

	uint32_t spawnX = (uint32_t)spawn.x;
	uint32_t spawnZ = (uint32_t)spawn.z;
	cam.pos.y = wrld.hieght_map[spawnX + spawnZ * wrld_dim[2]] + 6.0f;

    window_attach_device(&window);
	//window_test_entity_upload(&window);
    window_world_buffer_load(&window, &wrld);
	window_material_buffer_load(&window, material_list);
	window_model_buffer_load(&window, model_instance_list);

    glfwSetInputMode(window.glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	double lastMouseX = 0.0, lastMouseY = 0.0;
	glfwGetCursorPos(window.glfw_window, &lastMouseX, &lastMouseY);

    float roll = 0.0f;
	float pitch = 0.0f;
	float yaw = 0.0f;
	const float mouseSensitivity = 0.0007f;
    const float pitchLimit = 1.55334f;

    double lastFrameTime = glfwGetTime();
    double fpsTimer = 0.0;
	int fpsFrameCount = 0;
	const double fpsUpdateInterval = 0.2; // 5 Hz

	Vector_t sun_direction = vector_create(0.318f, 0.848f, 0.424f);

    while (!window_should_close(&window)) {
        window_poll_events(&window);


        double currentFrameTime = glfwGetTime();
        float frameTime = (float)(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

        fpsTimer += frameTime;
		fpsFrameCount++;
		if (fpsTimer >= fpsUpdateInterval) {
			double fps = (double)fpsFrameCount / fpsTimer;
			double avgFrameTimeMs = (fpsTimer / fpsFrameCount) * 1000.0;
			printf("\rFPS: %6.1f | frame time: %6.3f ms | pos: (%.1f, %.1f, %.1f)", fps, avgFrameTimeMs, cam.pos.x, cam.pos.y, cam.pos.z);
			fflush(stdout);
			fpsTimer = 0.0;
			fpsFrameCount = 0;
		}

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

        window_render(&window, &cam, sun_direction);

    }

    window_close(&window);
    world_destroy(&wrld);
}