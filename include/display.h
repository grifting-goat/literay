#ifndef DISPLAY_H
#define DISPAY_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include "camera.h"


// Configuration constants
#define VULKAN_VERSION        VK_API_VERSION_1_4
#define MAX_FRAMES_IN_FLIGHT  2
#define SWAPCHAIN_FORMAT      VK_FORMAT_B8G8R8A8_SRGB
#define DEPTH_FORMAT          VK_FORMAT_D32_SFLOAT
#define OUTPUT_IMAGE_FORMAT   VK_FORMAT_R8G8B8A8_UNORM
#define ACCUM_IMAGE_FORMAT    VK_FORMAT_R32G32B32A32_SFLOAT // needs full float precision; accumulated over many frames
#define MAX_MATERIALS          256U // one per possible voxel byte value


#define VOXEL_GRID_DIM 512U
#define VOXEL_MASK_BLOCK_SIZE 8U // match BLOCK_SIZE in shader


#include "compute_res.h"

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
    VkSwapchainKHR swapchain;
	VkImage* swapchainImages;
	VkSemaphore* renderCompleteSemaphores;
	uint32_t swapchainWidth;
	uint32_t swapchainHeight;
	uint32_t swapchainImageCount;

} Vk_Swapchain_t;

typedef struct {

    VkApplicationInfo appInfo;

    VkInstance vulkanInstance;
	VkPhysicalDevice physicalDevice;
	uint32_t gfxQueueFamIdx;
	VkDevice device;
	VkQueue deviceQueue;
	VkSurfaceKHR surface;

    Vk_Swapchain_t swapchain_data;

    VkCommandPool commandPool;

    VkSemaphore timelineSemaphore;
    VkSemaphore computeTimelineSemaphore; // signaled right after the compute dispatch, not the whole frame
	FrameResources_t frameResources[MAX_FRAMES_IN_FLIGHT];
	uint32_t frameCounter;

    Vk_Image_t outputImageRes[MAX_FRAMES_IN_FLIGHT];
    Vk_Image_t accumImageRes; // single, persistent across frames (not double-buffered like outputImageRes)
    uint32_t accumCount;
    VkShaderModule computeShader;
    Pipeline_t computePipeline;
    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkDescriptorPool computeDescriptorPool;
    VkDescriptorSet computeDescriptorSet[MAX_FRAMES_IN_FLIGHT];

    Vk_Buffer_t worldGridBuffer;
    Vk_Buffer_t worldGridMaskBuffer;
    Vk_Buffer_t cameraDataBuffer[MAX_FRAMES_IN_FLIGHT];
    Vk_Buffer_t materialProperitesBuffer;

} Vk_Objects_t;


typedef struct Window_t{
	GLFWwindow *glfw_window;
	uint32_t width;
	uint32_t height;

    char window_name[VK_MAX_EXTENSION_NAME_SIZE];

    Vk_Objects_t vk_objects;

} Window_t;

Window_t window_create();

void window_attach_device(Window_t* window);

void window_render(Window_t* window, Camera* cam);

void window_world_buffer_load(Window_t* window);

void window_close(Window_t* window);

bool window_should_close(Window_t* window);

void window_poll_events(Window_t* window);






#endif //DISPLAY_H