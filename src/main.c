#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"



#include "camera.h"
#include "display.h"


void window_camera_buffer_update(Window_t* window, Camera* c); //temp location

int main() {

    Camera cam = camera_create_default();
    Window_t window = window_create();

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

    while (!window_should_close(&window)) {
        window_poll_events(&window);


        //mouse stuff
        double mouseX, mouseY;
		glfwGetCursorPos(window.glfw_window, &mouseX, &mouseY);
		double deltaX = mouseX - lastMouseX;
		double deltaY = mouseY - lastMouseY;
		lastMouseX = mouseX;
		lastMouseY = mouseY;

		yaw += (float)deltaX * mouseSensitivity;
		pitch -= (float)deltaY * mouseSensitivity;
		if (pitch > pitchLimit) { pitch = pitchLimit; }
		if (pitch < -pitchLimit) { pitch = -pitchLimit; }


		cam.angle.x = pitch;
		cam.angle.y = yaw;
		cam.angle.z = roll;

        window_camera_buffer_update(&window, &cam);
        window_render(&window);

    }


}

void window_world_buffer_load(Window_t* window) {
    VkDeviceSize world_grid_size = (VkDeviceSize)VOXEL_GRID_DIM * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
    uint8_t* voxels = (uint8_t*)window->vk_objects.worldGridBuffer.mapped;
    memset(voxels, 0, world_grid_size);

    uint32_t index = 10 + (10 * VOXEL_GRID_DIM) + (10 * VOXEL_GRID_DIM) * VOXEL_GRID_DIM;
    voxels[index] = 1;

    Material* materials = (Material*)window->vk_objects.materialProperitesBuffer.mapped;
    materials[1].color[0] = 1.0f;
    materials[1].color[1] = 0.0f;
    materials[1].color[2] = 0.0f;
    materials[1].color[3] = 1.0f;
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