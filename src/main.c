#include <stdio.h>
#include <string.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include "world.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"



#include "camera.h"
#include "display.h"
#include "material.h" //material_list
#include "model.h"
#include "command.h"

void sun_command(void* context, int argc, char* argv[]);


int main() {

    Vector_t spawn = vector_create((CHUNK_DIM), (CHUNK_DIM) + 100.0f, (CHUNK_DIM));
    Vector_t inital_angle = {0.0f};

    Window_t window = window_create();
    Camera cam = camera_create(103.0f * (M_PI / 180.0f), (float)window.width / (float)window.height, &spawn, &inital_angle);

	Commander cmd = commander_create();
	command_register(&cmd, "tp", camera_teleport_command, &cam);


	model_instance_list[0] = model_create_from_vox("./res/models/cute.vox" , 0, 0, AIR, true); 
	window_attach_device(&window); //fix the ordering bug here

	material_palette_create(); //dynamic for iterative purposes

	window_model_buffer_load(&window, model_instance_list);


	World wrld = world_create(67);
	world_chunk_gen_workers_start(&wrld);
	printf("loading spawn...\n");
	world_load_spawn_chunk(&wrld);
	printf("spawn loaded.\n");


    window_world_spawn_load(&window, &wrld);

	structure_manual_place(&wrld, CASTLE, iVector_create(10000, 128, -2000));
	structure_manual_place(&wrld, BOAT, iVector_create(350, 124, 200));



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
	double cpuTimeAccumMs = 0.0;
	double gpuTimeAccumMs = 0.0;
	const double fpsUpdateInterval = 0.2; // 5 Hz

	Vector_t sun_direction = vector_create(0.318f, 0.848f, 0.424f);
	command_register(&cmd, "sundir", sun_command, &sun_direction);

	world_chunk_memory_queue(&wrld, &cam.pos);

	commander_start(&cmd);

    while (!window_should_close(&window)) {
        double cpuFrameStart = glfwGetTime();
        window_poll_events(&window);


        double currentFrameTime = glfwGetTime();
        float frameTime = (float)(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

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
		double cpuFrameMs = (glfwGetTime() - cpuFrameStart) * 1000.0;
		double gpuRenderMs = fmax(0.0, (double)frameTime * 1000.0 - cpuFrameMs);

		fpsTimer += frameTime;
		fpsFrameCount++;
		cpuTimeAccumMs += cpuFrameMs;
		gpuTimeAccumMs += gpuRenderMs;
		if (fpsTimer >= fpsUpdateInterval) {
			double fps = (double)fpsFrameCount / fpsTimer;
			double avgFrameTimeMs = (fpsTimer / fpsFrameCount) * 1000.0;
			double avgCpuMs = cpuTimeAccumMs / fpsFrameCount;
			double avgGpuMs = gpuTimeAccumMs / fpsFrameCount;
			printf("\rFPS: %6.1f | frame: %6.3f ms | cpu: %6.3f ms | approx gpu: %6.3f ms | pos: (%.1f, %.1f, %.1f)", fps, avgFrameTimeMs, avgCpuMs, avgGpuMs, cam.pos.x, cam.pos.y, cam.pos.z);
			fflush(stdout);
			fpsTimer = 0.0;
			fpsFrameCount = 0;
			cpuTimeAccumMs = 0.0;
			gpuTimeAccumMs = 0.0;
		}

    }

	commander_end(&cmd);

    window_close(&window);
    world_destroy(&wrld);
}

void sun_command(void* context, int argc, char* argv[]) {
	Vector_t* direction = (Vector_t*)context;

	if (argc != 4) {printf("usage: sundir <x> <y> <z>\n"); return;}

	Vector_t new_direction = vector_create((float)atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3]));
	if (vector_magnitude(&new_direction) == 0.0f) {printf("sundir: direction cannot be zero\n"); return;}

	*direction = new_direction;
}
