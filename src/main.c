#include <stdio.h>
#include <string.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"


#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"



#include "camera.h"
#include "display.h"
#include "material.h" //material_list
#include "model.h"

#include "world.h"


int main() {

    Vector_t spawn = vector_create((CHUNK_DIM), (CHUNK_DIM) + 100.0f, (CHUNK_DIM));
    Vector_t inital_angle = {0.0f};

    Window_t window = window_create();
    Camera cam = camera_create(103.0f * (M_PI / 180.0f), (float)window.width / (float)window.height, &spawn, &inital_angle);

	window_attach_device(&window);

	material_palette_create(); //dynamic for iterative purposes

	model_instance_list[0] = model_create_from_vox("./res/models/cute.vox" , 0, 0, AIR, true);


	World wrld = world_create(67);
	world_load_spawn_chunk(&wrld);
    window_world_spawn_load(&window, &wrld);
	

	window_material_buffer_load(&window, material_list);
	//window_model_buffer_load(&window, model_instance_list);

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

	world_chunk_memory_queue(&wrld, &cam.pos);

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

		if (floor(cam.pos.x / CHUNK_DIM) != floor(cam.prevPos.x / CHUNK_DIM)
			|| floor(cam.pos.y / CHUNK_DIM) != floor(cam.prevPos.y / CHUNK_DIM)
			|| floor(cam.pos.z / CHUNK_DIM) != floor(cam.prevPos.z / CHUNK_DIM)
		) {
			world_chunk_memory_queue(&wrld, &cam.pos);
		}

		window_world_chunk_streaming(&window, &wrld);
		

        window_render(&window, &cam, sun_direction);

    }

    window_close(&window);
    world_destroy(&wrld);
}