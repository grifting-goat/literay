#ifndef DISPLAY_H
#define DISPAY_H

#include <stdlib.h>
#include <stdint.h>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"


// Configuration constants
#define VULKAN_VERSION        VK_API_VERSION_1_4
#define MAX_FRAMES_IN_FLIGHT  2
#define SWAPCHAIN_FORMAT      VK_FORMAT_B8G8R8A8_SRGB
#define DEPTH_FORMAT          VK_FORMAT_D32_SFLOAT
#define OUTPUT_IMAGE_FORMAT   VK_FORMAT_R8G8B8A8_UNORM
#define MAX_MATERIALS          256U // one per possible voxel byte value

typedef struct {
	VkPipelineLayout layout;
	VkPipeline handle;
} Pipeline_t;

typedef struct {
	uint32_t lastFrameId;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkSemaphore imageAcquiredSemaphore;
	VkSemaphore workCompleteSemaphore;
} FrameResources_t;

typedef struct {

    VkImage outputImage;
	VkImageView outputImageView;
	VkDeviceMemory outputImageMemory;

} Vk_Image_t;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory bufferMemory;
	void* mapped;
} Vk_Buffer_t;

typedef struct {

    VkApplicationInfo appInfo;

    VkInstance vulkanInstance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkSurfaceKHR surface;

    VkCommandPool commandPool;

    VkSemaphore timelineSemaphore;
	FrameResources_t frameResources[MAX_FRAMES_IN_FLIGHT];

    Vk_Image_t outputImageRes;
    VkShaderModule computeShader;

    Vk_Buffer_t worldGridBuffer;
    Vk_Buffer_t cameraDataBuffer;

} Vk_Objects_t;


typedef struct Window_t{
	GLFWwindow *window;
	uint32_t width;
	uint32_t height;

    char window_name[VK_MAX_EXTENSION_NAME_SIZE];

    Vk_Objects_t vk_objects;

} Window_t;

Window_t window_create();


VkApplicationInfo AppInfoCreate(Window_t* window);
VkInstance VulkanInstanceCreate(Window_t* window);
GLFWwindow* WindowCreate(Window_t* window);
VkSurfaceKHR SurfaceCreate(Window_t* window);




#endif //DISPLAY_H