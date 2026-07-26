#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"



#include "camera.h"
#include "display.h"

int main() {

    Camera cam = camera_create_default();
    Window_t window = window_create();

    window_attach_device(&window);

    while (!window_should_close(&window)) {
        window_poll_events(&window);


    }

    printf("hello wrld!");
}