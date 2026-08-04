#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "world.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include "camera.h"
#include "model.h"


// Configuration constants
#define VULKAN_VERSION        VK_API_VERSION_1_4
#define MAX_FRAMES_IN_FLIGHT  2
#define SWAPCHAIN_FORMAT      VK_FORMAT_B8G8R8A8_SRGB
#define DEPTH_FORMAT          VK_FORMAT_D32_SFLOAT
#define OUTPUT_IMAGE_FORMAT   VK_FORMAT_R8G8B8A8_UNORM
#define ACCUM_IMAGE_FORMAT    VK_FORMAT_R32G32B32A32_SFLOAT // needs full float precision; accumulated over many frames
#define WORLD_TEXTURE_FORMAT  VK_FORMAT_R8_UINT // 3D voxel material ids
#define MAX_MATERIALS         256U // one per possible voxel byte value
#define MAX_MODELS            16U
#define MAX_ENTITIES          16U


#define MAX_LOADED_VOXEL_DIM_XZ (CHUNK_STREAM_WINDOW_XZ * CHUNK_DIM)
#define MAX_LOADED_VOXEL_DIM_Y  (CHUNK_STREAM_WINDOW_Y * CHUNK_DIM)
#define VOXEL_BRICK_SIZE 8U // match BRICK_SIZE in shader

#define EMPTY_SLOT 0xFFFFFFFFu

#define CHUNK_STREAM_RING_DEPTH 2 
#define CHUNK_UNLOAD_RING_DEPTH 4


#include "compute_res.h"

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

// persistent per-slot resources for the chunk-streaming upload rings (display.c),
// reused across calls instead of allocating/freeing a command buffer + staging buffer every time
typedef struct {
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	Vk_Buffer_t staging;
	VkBufferImageCopy* regions;
	uint64_t submittedValue; // streamTimelineSemaphore value to wait for before reusing this slot; 0 = never used
} OccupancyMaskStreamSlot_t;

typedef struct {
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	Vk_Buffer_t slotStaging;
	Vk_Buffer_t skipStaging;
	Vk_Buffer_t voxelStaging;
	VkBufferImageCopy* indirectionRegions;
	VkBufferImageCopy* skipRegions;
	VkBufferImageCopy* atlasRegions;
	uint64_t submittedValue;
} AtlasStreamSlot_t;

typedef struct {
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	Vk_Buffer_t slotStaging;
	Vk_Buffer_t skipStaging;
	VkBufferImageCopy* regions;
	VkBufferImageCopy* skipRegions;
	uint64_t submittedValue;
} UnloadStreamSlot_t;

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
    VkSemaphore computeTimelineSemaphore; // signaled right after the compute dispatch
	FrameResources_t frameResources[MAX_FRAMES_IN_FLIGHT];
	uint32_t frameCounter;

	VkSemaphore streamTimelineSemaphore; // chunk-streaming uploads chain off this instead of vkQueueWaitIdle
	uint64_t streamTimelineValue; // last value signaled by a streaming submission; 0 = none yet
	OccupancyMaskStreamSlot_t occupancyMaskStreamSlots[CHUNK_STREAM_RING_DEPTH];
	uint32_t occupancyMaskStreamSlotIndex;
	AtlasStreamSlot_t atlasStreamSlots[CHUNK_STREAM_RING_DEPTH];
	uint32_t atlasStreamSlotIndex;
	UnloadStreamSlot_t unloadStreamSlots[CHUNK_UNLOAD_RING_DEPTH];
	uint32_t unloadStreamSlotIndex;

    Vk_Image_t outputImageRes[MAX_FRAMES_IN_FLIGHT];
    Vk_Image_t accumImageRes; // single, persistent across frames
    uint32_t accumCount;
    VkPipelineLayout computePipelineLayout;
    VkPipeline computePipelineA;
    VkPipeline computePipelineB;
    uint32_t activeShaderIndex;
    bool forceAccumReset;
    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkDescriptorPool computeDescriptorPool;
    VkDescriptorSet computeDescriptorSet[MAX_FRAMES_IN_FLIGHT];

    Vk_Image_t brickAtlasTexture; //voxel data
    Vk_Image_t brickIndirectionTexture; //brick location in atlas //world shaped
    Vk_Image_t voxelOccupancyTexture; //is a voxel occupied //organized by chunk
    Vk_Image_t chunkSkipTexture; //does a chunk have any geometry at all -- coarsest DDA skip

    //Vk_Image_t worldVoxelTexture; // 3D R8_UINT -> keeps 3D-adjacent voxels cache-adjacent
    //Vk_Buffer_t worldGridMaskBuffer;
    //Vk_Buffer_t worldGridOccBuffer; // 1 bit per voxel, for the fine DDA loop



    Vk_Buffer_t cameraDataBuffer[MAX_FRAMES_IN_FLIGHT];
    Vk_Buffer_t materialProperitesBuffer;


    Vk_Buffer_t entityDataBuffer[MAX_FRAMES_IN_FLIGHT];
    Vk_Buffer_t modelBuffer;
    Vk_Buffer_t modelVoxelBuffer;

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

void window_render(Window_t* window, Camera* cam, Vector_t sun_direction);

void window_world_spawn_load(Window_t* window, World* wrld);

void window_world_chunk_streaming(Window_t* window, World* wrld);

void window_material_buffer_load(Window_t* window, Material* mat_list);

void window_test_entity_upload(Window_t* window);

void window_model_buffer_load(Window_t* window, Model_t* model_list);

void window_close(Window_t* window);

bool window_should_close(Window_t* window);

void window_poll_events(Window_t* window);

void window_toggle_shader(Window_t* window);

void window_reload_shaders(Window_t* window);





#endif //DISPLAY_H