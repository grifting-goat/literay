#include "display.h"
#include "world.h"

#include "stb_image.h"
#include <string.h>
#include "stb_ds.h"


#define ICON_32_PATH "./res/icon/icon_32.png"
#define ICON_64_PATH "./res/icon/icon_64.png"
#define ICON_128_PATH "./res/icon/icon_128.png"

#define CHUNK_STREAM_BATCH_SIZE 16



static int32_t wrapChunkCoord(int32_t v) {
	int32_t m = v % CHUNK_STREAM_WINDOW;
	return m < 0 ? m + CHUNK_STREAM_WINDOW : m;
}


void appInfoCreate(Window_t* window);
void vulkanInstanceCreate(Window_t* window);
void glfw_app_window_create(Window_t* window);


void surfaceCreate(Window_t* window);

void createDevice(Window_t* window);

void chooseBestPhysicalDevice(Window_t* window);

void createOutputImage(Window_t* window);
void createAccumImage(Window_t* window);

uint32_t findMemoryTypeIndex(Window_t* window, uint32_t typeBits, VkMemoryPropertyFlags required);

void createSwapchain(Window_t* window);

void createComputePipeline(Window_t* window);

void createComputeShader(Window_t* window);

Vk_Buffer_t createHostVisibleBuffer(Window_t* window, VkDeviceSize size, VkBufferUsageFlags usage);
Vk_Buffer_t createDeviceLocalBuffer(Window_t* window, VkDeviceSize size, VkBufferUsageFlags usage);
void uploadBufferData(Window_t* window, VkBuffer dst, const void* data, VkDeviceSize size);
void createBrickTextures(Window_t* window);

void createComputeBuffers(Window_t* window);

void createComputeDescriptorSet(Window_t* window);

void createCommandBuffers(Window_t* window);

void createSyncResources(Window_t* window);

static void destroyVkBuffer(VkDevice device, Vk_Buffer_t* buf);
static void destroyVkImage(VkDevice device, Vk_Image_t* img);



Window_t window_create() {
    glfwInit();

    Window_t window = {0};
    const char name[64] = "Literay";
    strcpy(window.window_name, name);

    appInfoCreate(&window);
    vulkanInstanceCreate(&window);

    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);
	glfwWindowHint(GLFW_DECORATED,GLFW_FALSE);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    window.height = (uint32_t)mode->height;
    window.width = (uint32_t)mode->width;

    glfw_app_window_create(&window);
    surfaceCreate(&window);

    return window;
}

void window_attach_device(Window_t* window) {
    chooseBestPhysicalDevice(window);
    createDevice(window);

	createOutputImage(window);
	createAccumImage(window);
	createSwapchain(window);

	createComputeShader(window);
	createComputePipeline(window);
	createComputeBuffers(window);
	createComputeDescriptorSet(window);
	
	createSyncResources(window);
	createCommandBuffers(window);
}

void window_camera_buffer_update(Window_t* window, Camera* c, uint32_t frame_res_index) {
	CameraData* ubo = (CameraData*)window->vk_objects.cameraDataBuffer[frame_res_index].mapped;

	bool cameraMoved = camera_check_moved(c);
	window->vk_objects.accumCount = cameraMoved ? 1 : window->vk_objects.accumCount + 1;

	ubo->camera_position[0] = c->pos.x;
	ubo->camera_position[1] = c->pos.y;
	ubo->camera_position[2] = c->pos.z;

	float pitch = c->angle.x;
	float yaw = c->angle.y;

	float fwd[3] = { -sinf(yaw) * cosf(pitch), -sinf(pitch), cosf(yaw) * cosf(pitch) };
	float right[3] = { -cosf(yaw), 0.0f, -sinf(yaw) };
	float up[3] = {
		right[1] * fwd[2] - right[2] * fwd[1],
		right[2] * fwd[0] - right[0] * fwd[2],
		right[0] * fwd[1] - right[1] * fwd[0]
	};

	for (int i = 0; i < 3; i++) {
		ubo->camera_forward[i] = fwd[i];
		ubo->camera_right[i] = right[i];
		ubo->camera_up[i] = up[i];
	}

	ubo->tan_fov_v = tanf(c->fov_v);
	ubo->tan_fov_h = tanf(c->fov_h);
}

void window_entity_buffer_update(Window_t* window, Camera* c, uint32_t frame_res_index) {
	// slot 0 tracks the camera as a placeholder entity; the rest stay zeroed from buffer creation (scale == 0 == inactive)
	EntityData entities[1] = {0};
	EntityData* entity = &entities[0];

	entity->position[0] = c->pos.x;
	entity->position[1] = c->pos.y;
	entity->position[2] = c->pos.z;

	entity->rotation[0] = c->angle.x;
	entity->rotation[1] = c->angle.y;
	entity->rotation[2] = c->angle.z;

	entity->modelIdx = 0;

	Model_t* model = &model_instance_list[entity->modelIdx];
	float dimX = (float)model->dimensions[0];
	float dimY = (float)model->dimensions[1];
	float dimZ = (float)model->dimensions[2];

	entity->scale = 4.0f / dimY;
	entity->position[1] -= 3.0f;

	float halfDiag = 0.5f * sqrtf(dimX * dimX + dimZ * dimZ) * entity->scale;

	entity->worldMin[0] = entity->position[0] - halfDiag;
	entity->worldMin[1] = entity->position[1];
	entity->worldMin[2] = entity->position[2] - halfDiag;

	entity->worldMax[0] = entity->position[0] + halfDiag;
	entity->worldMax[1] = entity->position[1] + dimY * entity->scale;
	entity->worldMax[2] = entity->position[2] + halfDiag;



	memcpy(window->vk_objects.entityDataBuffer[frame_res_index].mapped, entity, sizeof(EntityData));
}

void window_test_entity_upload(Window_t* window) {
	EntityData entities[MAX_ENTITIES] = {0};
	float center = MAX_LOADED_VOXEL_DIM / 2 ;
	for (int i = 1; i < MAX_ENTITIES; i++) {
		EntityData* entity = &entities[i];
		entity->position[0] =  (float)(rand() % 100 - 50) + center;
		entity->position[1] =  (float)(rand() % 100 - 50) + center + 60.0f;
		entity->position[2] =  (float)(rand() % 100 - 50) + center;

		entity->rotation[0] = (float) (rand() % 360) * (M_PI / 180);
		entity->rotation[1] = (float) (rand() % 360) * (M_PI / 180);
		entity->rotation[2] = (float) (rand() % 360) * (M_PI / 180);

		entity->modelIdx = rand() % 3 + 1;

		
		Model_t* model = &model_instance_list[entity->modelIdx];
		float dimX = (float)model->dimensions[0];
		float dimY = (float)model->dimensions[1];
		float dimZ = (float)model->dimensions[2];

		entity->scale = ((float)(rand() % 10) * 0.5f + 1.0f) / dimY;
		entity->position[1] -= 3.0f;

		float halfDiag = 0.5f * sqrtf(dimX * dimX + dimZ * dimZ) * entity->scale;

		entity->worldMin[0] = entity->position[0] - halfDiag;
		entity->worldMin[1] = entity->position[1];
		entity->worldMin[2] = entity->position[2] - halfDiag;

		entity->worldMax[0] = entity->position[0] + halfDiag;
		entity->worldMax[1] = entity->position[1] + dimY * entity->scale;
		entity->worldMax[2] = entity->position[2] + halfDiag;
		
	}

	memcpy(window->vk_objects.entityDataBuffer[0].mapped, entities, sizeof(entities));
	memcpy(window->vk_objects.entityDataBuffer[1].mapped, entities, sizeof(entities));

}



void window_render(Window_t* window, Camera* cam, Vector_t sun_direction) {
	
	static uint64_t timeline_value = 0;


	uint32_t frame_res_index = window->vk_objects.frameCounter++ % MAX_FRAMES_IN_FLIGHT;
	FrameResources_t *res = &window->vk_objects.frameResources[frame_res_index];

	//frame N and frame N - MAX_FRAMES_IN_FLIGHT share resources
	uint64_t frame_id = ++timeline_value;
	uint64_t wait_for_id = frame_id > MAX_FRAMES_IN_FLIGHT ? frame_id - MAX_FRAMES_IN_FLIGHT : 0;

	VkSemaphoreWaitInfo wait_info={0};
	wait_info.sType=VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	wait_info.semaphoreCount=1;
	wait_info.pSemaphores=&window->vk_objects.timelineSemaphore;
	wait_info.pValues=&wait_for_id;
	vkWaitSemaphores(window->vk_objects.device, &wait_info, UINT64_MAX);


	window_camera_buffer_update(window, cam, frame_res_index);
	window_entity_buffer_update(window, cam, frame_res_index);

	vkResetCommandPool(window->vk_objects.device, res->commandPool, 0);

	uint32_t image_index=0;
	VkResult acquire_result = vkAcquireNextImageKHR(window->vk_objects.device, window->vk_objects.swapchain_data.swapchain, UINT64_MAX, res->imageAcquiredSemaphore, VK_NULL_HANDLE, &image_index);
	if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
		printf("swapchain out of date\n");
		return;
	}

	VkCommandBufferBeginInfo cmd_begin_info={0};
	cmd_begin_info.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmd_begin_info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(res->commandBuffer, &cmd_begin_info);

	//transition output image so the compute shader can imageStore into it
	VkImageMemoryBarrier2 to_general_barrier={0};
	to_general_barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	to_general_barrier.srcStageMask=VK_PIPELINE_STAGE_2_NONE;
	to_general_barrier.srcAccessMask=0;
	to_general_barrier.dstStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	to_general_barrier.dstAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	to_general_barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
	to_general_barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
	to_general_barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	to_general_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	to_general_barrier.image=window->vk_objects.outputImageRes[frame_res_index].outputImage;
	to_general_barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	to_general_barrier.subresourceRange.baseMipLevel=0;
	to_general_barrier.subresourceRange.levelCount=1;
	to_general_barrier.subresourceRange.baseArrayLayer=0;
	to_general_barrier.subresourceRange.layerCount=1;

	
	VkImageMemoryBarrier2 accum_barrier={0};
	accum_barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	accum_barrier.srcStageMask= frame_id == 1 ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	accum_barrier.srcAccessMask= frame_id == 1 ? 0 : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	accum_barrier.dstStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	accum_barrier.dstAccessMask=VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	accum_barrier.oldLayout= frame_id == 1 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
	accum_barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
	accum_barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	accum_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	accum_barrier.image=window->vk_objects.accumImageRes.outputImage;
	accum_barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	accum_barrier.subresourceRange.baseMipLevel=0;
	accum_barrier.subresourceRange.levelCount=1;
	accum_barrier.subresourceRange.baseArrayLayer=0;
	accum_barrier.subresourceRange.layerCount=1;

	VkImageMemoryBarrier2 pre_dispatch_barriers[2] = { to_general_barrier, accum_barrier };

	VkDependencyInfo pre_dispatch_dep_info={0};
	pre_dispatch_dep_info.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	pre_dispatch_dep_info.imageMemoryBarrierCount=2;
	pre_dispatch_dep_info.pImageMemoryBarriers=pre_dispatch_barriers;
	vkCmdPipelineBarrier2(res->commandBuffer, &pre_dispatch_dep_info);

	vkCmdBindPipeline(res->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, window->vk_objects.computePipeline.handle);
	vkCmdBindDescriptorSets(res->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, window->vk_objects.computePipeline.layout, 0, 1, &window->vk_objects.computeDescriptorSet[frame_res_index], 0, NULL);

	PushConstants push_constants = {0};
	push_constants._screen_size[0] = (int)window->vk_objects.swapchain_data.swapchainWidth;
	push_constants._screen_size[1] = (int)window->vk_objects.swapchain_data.swapchainHeight;
	push_constants._voxel_grid_size[0] = (int)MAX_LOADED_VOXEL_DIM;
	push_constants._voxel_grid_size[1] = (int)MAX_LOADED_VOXEL_DIM;
	push_constants._voxel_grid_size[2] = (int)MAX_LOADED_VOXEL_DIM;
	push_constants.frame_idx = (unsigned int)frame_id;
	push_constants.accumCount = window->vk_objects.accumCount;

	float sun_mag = vector_magnitude(&sun_direction);
	push_constants.sun_direction[0] = sun_direction.x / sun_mag;
	push_constants.sun_direction[1] = sun_direction.y / sun_mag;
	push_constants.sun_direction[2] = sun_direction.z / sun_mag;

	vkCmdPushConstants(res->commandBuffer, window->vk_objects.computePipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push_constants);

	uint32_t group_count_x = (window->vk_objects.swapchain_data.swapchainWidth + 7) / 8;
	uint32_t group_count_y = (window->vk_objects.swapchain_data.swapchainHeight + 7) / 8;
	vkCmdDispatch(res->commandBuffer, group_count_x, group_count_y, 1);

	//transition output image (blit source) and swapchain image (blit destination)
	VkImageMemoryBarrier2 pre_blit_barriers[2];
	pre_blit_barriers[0].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	pre_blit_barriers[0].pNext=NULL;
	pre_blit_barriers[0].srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	pre_blit_barriers[0].srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	pre_blit_barriers[0].dstStageMask=VK_PIPELINE_STAGE_2_BLIT_BIT;
	pre_blit_barriers[0].dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;
	pre_blit_barriers[0].oldLayout=VK_IMAGE_LAYOUT_GENERAL;
	pre_blit_barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	pre_blit_barriers[0].srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	pre_blit_barriers[0].dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	pre_blit_barriers[0].image=window->vk_objects.outputImageRes[frame_res_index].outputImage;
	pre_blit_barriers[0].subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	pre_blit_barriers[0].subresourceRange.baseMipLevel=0;
	pre_blit_barriers[0].subresourceRange.levelCount=1;
	pre_blit_barriers[0].subresourceRange.baseArrayLayer=0;
	pre_blit_barriers[0].subresourceRange.layerCount=1;

	pre_blit_barriers[1].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	pre_blit_barriers[1].pNext=NULL;
	pre_blit_barriers[1].srcStageMask=VK_PIPELINE_STAGE_2_NONE;
	pre_blit_barriers[1].srcAccessMask=0;
	pre_blit_barriers[1].dstStageMask=VK_PIPELINE_STAGE_2_BLIT_BIT;
	pre_blit_barriers[1].dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
	pre_blit_barriers[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
	pre_blit_barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	pre_blit_barriers[1].srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	pre_blit_barriers[1].dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	pre_blit_barriers[1].image=window->vk_objects.swapchain_data.swapchainImages[image_index];
	pre_blit_barriers[1].subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	pre_blit_barriers[1].subresourceRange.baseMipLevel=0;
	pre_blit_barriers[1].subresourceRange.levelCount=1;
	pre_blit_barriers[1].subresourceRange.baseArrayLayer=0;
	pre_blit_barriers[1].subresourceRange.layerCount=1;

	VkDependencyInfo pre_blit_dep_info={0};
	pre_blit_dep_info.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	pre_blit_dep_info.imageMemoryBarrierCount=2;
	pre_blit_dep_info.pImageMemoryBarriers=pre_blit_barriers;
	vkCmdPipelineBarrier2(res->commandBuffer, &pre_blit_dep_info);

	VkImageBlit blit_region={0};
	blit_region.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	blit_region.srcSubresource.mipLevel=0;
	blit_region.srcSubresource.baseArrayLayer=0;
	blit_region.srcSubresource.layerCount=1;
	blit_region.srcOffsets[1].x=(int32_t)window->vk_objects.swapchain_data.swapchainWidth;
	blit_region.srcOffsets[1].y=(int32_t)window->vk_objects.swapchain_data.swapchainHeight;
	blit_region.srcOffsets[1].z=1;

	blit_region.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	blit_region.dstSubresource.mipLevel=0;
	blit_region.dstSubresource.baseArrayLayer=0;
	blit_region.dstSubresource.layerCount=1;
	blit_region.dstOffsets[1].x=(int32_t)window->vk_objects.swapchain_data.swapchainWidth;
	blit_region.dstOffsets[1].y=(int32_t)window->vk_objects.swapchain_data.swapchainHeight;
	blit_region.dstOffsets[1].z=1;

	vkCmdBlitImage(res->commandBuffer,
		window->vk_objects.outputImageRes[frame_res_index].outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		window->vk_objects.swapchain_data.swapchainImages[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit_region, VK_FILTER_NEAREST);

	//transition swapchain image to presentable layout
	VkImageMemoryBarrier2 present_barrier={0};
	present_barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	present_barrier.srcStageMask=VK_PIPELINE_STAGE_2_BLIT_BIT;
	present_barrier.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
	present_barrier.dstStageMask=VK_PIPELINE_STAGE_2_NONE;
	present_barrier.dstAccessMask=0;
	present_barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	present_barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	present_barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	present_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
	present_barrier.image=window->vk_objects.swapchain_data.swapchainImages[image_index];
	present_barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	present_barrier.subresourceRange.baseMipLevel=0;
	present_barrier.subresourceRange.levelCount=1;
	present_barrier.subresourceRange.baseArrayLayer=0;
	present_barrier.subresourceRange.layerCount=1;

	VkDependencyInfo present_dep_info={0};
	present_dep_info.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	present_dep_info.imageMemoryBarrierCount=1;
	present_dep_info.pImageMemoryBarriers=&present_barrier;
	vkCmdPipelineBarrier2(res->commandBuffer, &present_dep_info);

	vkEndCommandBuffer(res->commandBuffer);


	uint64_t prev_frame_id = frame_id > 1 ? frame_id - 1 : 0;

	VkSemaphoreSubmitInfo wait_semaphore_infos[2]={0};
	wait_semaphore_infos[0].sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait_semaphore_infos[0].semaphore=res->imageAcquiredSemaphore;
	wait_semaphore_infos[0].stageMask=VK_PIPELINE_STAGE_2_BLIT_BIT;

	wait_semaphore_infos[1].sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait_semaphore_infos[1].semaphore=window->vk_objects.computeTimelineSemaphore;
	wait_semaphore_infos[1].value=prev_frame_id;
	wait_semaphore_infos[1].stageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

	VkSemaphoreSubmitInfo signal_semaphore_infos[3];
	signal_semaphore_infos[0].sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal_semaphore_infos[0].pNext=NULL;
	signal_semaphore_infos[0].semaphore=window->vk_objects.swapchain_data.renderCompleteSemaphores[image_index];
	signal_semaphore_infos[0].value=0;
	signal_semaphore_infos[0].stageMask=VK_PIPELINE_STAGE_2_BLIT_BIT;
	signal_semaphore_infos[0].deviceIndex=0;

	signal_semaphore_infos[1].sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal_semaphore_infos[1].pNext=NULL;
	signal_semaphore_infos[1].semaphore=window->vk_objects.timelineSemaphore;
	signal_semaphore_infos[1].value=frame_id;
	signal_semaphore_infos[1].stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	signal_semaphore_infos[1].deviceIndex=0;

	signal_semaphore_infos[2].sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal_semaphore_infos[2].pNext=NULL;
	signal_semaphore_infos[2].semaphore=window->vk_objects.computeTimelineSemaphore;
	signal_semaphore_infos[2].value=frame_id;
	signal_semaphore_infos[2].stageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	signal_semaphore_infos[2].deviceIndex=0;

	VkCommandBufferSubmitInfo cmd_submit_info={0};
	cmd_submit_info.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmd_submit_info.commandBuffer=res->commandBuffer;

	VkSubmitInfo2 submit_info={0};
	submit_info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit_info.waitSemaphoreInfoCount=2;
	submit_info.pWaitSemaphoreInfos=wait_semaphore_infos;
	submit_info.commandBufferInfoCount=1;
	submit_info.pCommandBufferInfos=&cmd_submit_info;
	submit_info.signalSemaphoreInfoCount=3;
	submit_info.pSignalSemaphoreInfos=signal_semaphore_infos;

	vkQueueSubmit2(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);

	VkPresentInfoKHR present_info={0};
	present_info.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount=1;
	present_info.pWaitSemaphores=&window->vk_objects.swapchain_data.renderCompleteSemaphores[image_index];
	present_info.swapchainCount=1;
	present_info.pSwapchains=&window->vk_objects.swapchain_data.swapchain;
	present_info.pImageIndices=&image_index;
	present_info.pResults=NULL;

	VkResult present_result = vkQueuePresentKHR(window->vk_objects.deviceQueue, &present_info);
	if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
		printf("swapchain out of date/suboptimal on present\n");
	}
}

static void destroyVkBuffer(VkDevice device, Vk_Buffer_t* buf) {
	if (buf->mapped != NULL) {
		vkUnmapMemory(device, buf->bufferMemory);
	}
	if (buf->buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(device, buf->buffer, NULL);
	}
	if (buf->bufferMemory != VK_NULL_HANDLE) {
		vkFreeMemory(device, buf->bufferMemory, NULL);
	}
	*buf = (Vk_Buffer_t){0};
}

static void destroyVkImage(VkDevice device, Vk_Image_t* img) {
	if (img->outputImageView != VK_NULL_HANDLE) {
		vkDestroyImageView(device, img->outputImageView, NULL);
	}
	if (img->outputImage != VK_NULL_HANDLE) {
		vkDestroyImage(device, img->outputImage, NULL);
	}
	if (img->outputImageMemory != VK_NULL_HANDLE) {
		vkFreeMemory(device, img->outputImageMemory, NULL);
	}
	*img = (Vk_Image_t){0};
}

void window_close(Window_t* window) {
	Vk_Objects_t* vk = &window->vk_objects;

	if (vk->device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(vk->device);
	}

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		FrameResources_t* res = &vk->frameResources[i];
		if (res->commandPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(vk->device, res->commandPool, NULL); // also frees its commandBuffer
		}
		if (res->imageAcquiredSemaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(vk->device, res->imageAcquiredSemaphore, NULL);
		}
	}

	if (vk->timelineSemaphore != VK_NULL_HANDLE) {
		vkDestroySemaphore(vk->device, vk->timelineSemaphore, NULL);
	}
	if (vk->computeTimelineSemaphore != VK_NULL_HANDLE) {
		vkDestroySemaphore(vk->device, vk->computeTimelineSemaphore, NULL);
	}

	// descriptor pool destruction also frees the descriptor sets allocated from it
	if (vk->computeDescriptorPool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(vk->device, vk->computeDescriptorPool, NULL);
	}
	if (vk->computeDescriptorSetLayout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(vk->device, vk->computeDescriptorSetLayout, NULL);
	}

	if (vk->computePipeline.handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(vk->device, vk->computePipeline.handle, NULL);
	}
	if (vk->computePipeline.layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(vk->device, vk->computePipeline.layout, NULL);
	}

	destroyVkImage(vk->device, &vk->brickAtlasTexture);
	destroyVkImage(vk->device, &vk->brickIndirectionTexture);
	destroyVkImage(vk->device, &vk->voxelOccupancyTexture);
	destroyVkImage(vk->device, &vk->chunkSkipTexture);
	destroyVkBuffer(vk->device, &vk->materialProperitesBuffer);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		destroyVkBuffer(vk->device, &vk->cameraDataBuffer[i]);
	}

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		destroyVkImage(vk->device, &vk->outputImageRes[i]);
	}
	destroyVkImage(vk->device, &vk->accumImageRes);

	// swapchainImages is owned by the swapchain itself, only the handle array needs freeing
	for (uint32_t i = 0; i < vk->swapchain_data.swapchainImageCount; i++) {
		if (vk->swapchain_data.renderCompleteSemaphores[i] != VK_NULL_HANDLE) {
			vkDestroySemaphore(vk->device, vk->swapchain_data.renderCompleteSemaphores[i], NULL);
		}
	}
	free(vk->swapchain_data.renderCompleteSemaphores);
	free(vk->swapchain_data.swapchainImages);
	if (vk->swapchain_data.swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(vk->device, vk->swapchain_data.swapchain, NULL);
	}

	if (vk->device != VK_NULL_HANDLE) {
		vkDestroyDevice(vk->device, NULL);
	}
	if (vk->surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(vk->vulkanInstance, vk->surface, NULL);
	}
	if (vk->vulkanInstance != VK_NULL_HANDLE) {
		vkDestroyInstance(vk->vulkanInstance, NULL);
	}

	if (window->glfw_window != NULL) {
		glfwDestroyWindow(window->glfw_window);
	}
	glfwTerminate();

	*window = (Window_t){0};
}

bool window_should_close(Window_t* window) {
    return glfwWindowShouldClose(window->glfw_window);
}

void window_poll_events(Window_t* window) {
    glfwPollEvents();
}


void glfw_app_window_create(Window_t* window) {

    window->glfw_window = glfwCreateWindow(window->width,window->height,window->window_name,NULL,NULL);

    int monitorX, monitorY;
	glfwGetMonitorPos(glfwGetPrimaryMonitor(), &monitorX, &monitorY);
	glfwSetWindowPos(window->glfw_window, monitorX, monitorY);

    //add window icon
    GLFWimage icons[3];
	icons[0].pixels = stbi_load(ICON_32_PATH, &icons[0].width, &icons[0].height, 0, 4);
	icons[1].pixels = stbi_load(ICON_64_PATH, &icons[1].width, &icons[1].height, 0, 4);
	icons[2].pixels = stbi_load(ICON_128_PATH, &icons[2].width, &icons[2].height, 0, 4);

    if (icons[0].pixels && icons[1].pixels && icons[2].pixels) {glfwSetWindowIcon(window->glfw_window, 3, icons);} 
    else {printf("Icon failed.\n");}
	
	if (icons[0].pixels) stbi_image_free(icons[0].pixels);
	if (icons[1].pixels) stbi_image_free(icons[1].pixels);
	if (icons[2].pixels) stbi_image_free(icons[2].pixels);

    printf("window created.\n");
}



void surfaceCreate(Window_t* window) {
	glfwCreateWindowSurface(window->vk_objects.vulkanInstance,window->glfw_window,NULL,&window->vk_objects.surface);
	printf("surface created.\n");
}


void vulkanInstanceCreate(Window_t* window) {
    // create instance info
	VkInstanceCreateInfo instance_create_info;
	instance_create_info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pNext=NULL;
	instance_create_info.flags=0;
	instance_create_info.pApplicationInfo=&window->vk_objects.appInfo;
	uint32_t instance_layer_count = 0U; //bypass instance layers for now
	instance_create_info.enabledLayerCount=instance_layer_count;
	char (*pp_instance_layers)[VK_MAX_EXTENSION_NAME_SIZE] = malloc(1 * sizeof(*pp_instance_layers));
	strcpy(pp_instance_layers[0],"VK_LAYER_KHRONOS_validation");
	char **pp_instance_layer_names = malloc(1 * sizeof(char*));
	for(uint32_t i=0;i<instance_layer_count;i++){
		pp_instance_layer_names[i]=
			pp_instance_layers[i];
	}
	instance_create_info.ppEnabledLayerNames=(const char * const *)pp_instance_layer_names;
	uint32_t instance_extension_count=0;
	const char * const *pp_instance_extension_names=glfwGetRequiredInstanceExtensions(&instance_extension_count);
	instance_create_info.enabledExtensionCount=instance_extension_count;
	instance_create_info.ppEnabledExtensionNames=pp_instance_extension_names;

	VkResult result = vkCreateInstance(&instance_create_info, NULL, &window->vk_objects.vulkanInstance);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "vkCreateInstance failed with VkResult %d\n", result);
		exit(EXIT_FAILURE);
	}

	free(pp_instance_layer_names);
	free(pp_instance_layers);
}


void appInfoCreate(Window_t* window) {

    VkApplicationInfo* app_info = &window->vk_objects.appInfo;
    app_info->sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info->pNext=NULL;
	app_info->pApplicationName= window->window_name;
	app_info->applicationVersion=VK_MAKE_VERSION(0,0,1);
	app_info->pEngineName="vulkan_engine";
	app_info->engineVersion=VK_MAKE_VERSION(0,0,1);
	app_info->apiVersion=VULKAN_VERSION;

}


void chooseBestPhysicalDevice(Window_t* window) {
    uint32_t physical_device_count=0;
	vkEnumeratePhysicalDevices(window->vk_objects.vulkanInstance,&physical_device_count,NULL);

	VkPhysicalDevice *physical_device = malloc(physical_device_count * sizeof(VkPhysicalDevice));
	vkEnumeratePhysicalDevices(window->vk_objects.vulkanInstance,&physical_device_count,physical_device);

    VkPhysicalDeviceProperties *physical_device_properties = malloc(physical_device_count * sizeof(VkPhysicalDeviceProperties));
	uint32_t *discrete_gpu_list = malloc(physical_device_count * sizeof(uint32_t));
	uint32_t discrete_gpu_count=0;
	uint32_t *intergrated_gpu_list = malloc(physical_device_count * sizeof(uint32_t));
	uint32_t intergrated_gpu_count=0;

	VkPhysicalDeviceMemoryProperties *physical_device_memory_properties = malloc(physical_device_count * sizeof(VkPhysicalDeviceMemoryProperties));
	uint32_t *physical_device_memory_count = malloc(physical_device_count * sizeof(uint32_t));
	VkDeviceSize *physical_device_memory_total = malloc(physical_device_count * sizeof(VkDeviceSize));
	VkDeviceSize *physical_device_memory_vram = malloc(physical_device_count * sizeof(VkDeviceSize));

	for(uint32_t i=0;i<physical_device_count;i++){
		vkGetPhysicalDeviceProperties(physical_device[i],&physical_device_properties[i]);
		if(physical_device_properties[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
			discrete_gpu_list[discrete_gpu_count]=i;
			discrete_gpu_count++;
		} else if(physical_device_properties[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU){
			intergrated_gpu_list[intergrated_gpu_count]=i;
			intergrated_gpu_count++;
		}

		vkGetPhysicalDeviceMemoryProperties(physical_device[i],&physical_device_memory_properties[i]);
		physical_device_memory_count[i] = physical_device_memory_properties[i].memoryHeapCount;
		physical_device_memory_total[i]=0;
		physical_device_memory_vram[i]=0;
		for(uint32_t j=0;j<physical_device_memory_count[i];j++){
			physical_device_memory_total[i] += physical_device_memory_properties[i].memoryHeaps[j].size;
			if(physical_device_memory_properties[i].memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT){
				physical_device_memory_vram[i] += physical_device_memory_properties[i].memoryHeaps[j].size;
			}
		}
	}

	VkDeviceSize max_memory_size=0;
	uint32_t physical_device_best_index=0;

	if(discrete_gpu_count!=0){
		for(uint32_t i=0;i<discrete_gpu_count;i++){
			if(physical_device_memory_total[i]>max_memory_size){
				physical_device_best_index=discrete_gpu_list[i];
				max_memory_size=physical_device_memory_total[i];
			}
		}
	} else if(intergrated_gpu_count!=0){
		for(uint32_t i=0;i<intergrated_gpu_count;i++){
			if(physical_device_memory_total[i]>max_memory_size){
				physical_device_best_index=intergrated_gpu_list[i];
				max_memory_size=physical_device_memory_total[i];
			}
		}
	}

	// Print Best Device
	printf("best device index:%u\n",physical_device_best_index);
	printf("device name:%s\n",physical_device_properties[physical_device_best_index].deviceName);
	printf("device type:");

	if(discrete_gpu_count!=0){printf("discrete gpu\n");}
	else if(intergrated_gpu_count!=0){printf("intergrated gpu\n");}
	else{printf("unknown\n");}

	printf("memory total:%llu\n",physical_device_memory_total[physical_device_best_index]);
	double mem_gb = (double)physical_device_memory_vram[physical_device_best_index] / (1073741824.0);
	printf("VRAM total:%.3f GB\n", mem_gb);

	window->vk_objects.physicalDevice = physical_device[physical_device_best_index];

	//find a queue family that can do graphics+compute and present to our surface
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(window->vk_objects.physicalDevice, &queue_family_count, NULL);
	VkQueueFamilyProperties *queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(window->vk_objects.physicalDevice, &queue_family_count, queue_families);

	uint32_t gfx_queue_family_index = UINT32_MAX;
	for (uint32_t i = 0; i < queue_family_count; i++) {
		VkBool32 present_supported = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(window->vk_objects.physicalDevice, i, window->vk_objects.surface, &present_supported);
		if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && present_supported) {
			gfx_queue_family_index = i;
			break;
		}
	}
	if (gfx_queue_family_index == UINT32_MAX) {
		printf("failed to find a queue family with graphics+compute+present support.\n");
	}
	window->vk_objects.gfxQueueFamIdx = gfx_queue_family_index;
	free(queue_families);

	free(physical_device);
	free(physical_device_properties);
	free(discrete_gpu_list);
	free(intergrated_gpu_list);
	free(physical_device_memory_properties);
	free(physical_device_memory_count);
	free(physical_device_memory_total);
	free(physical_device_memory_vram);
}


void createDevice(Window_t* window) {

	VkPhysicalDeviceVulkan14Features supported_features1_4 = {0};
	supported_features1_4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
	supported_features1_4.pNext = NULL;

	VkPhysicalDeviceVulkan13Features supported_features1_3 = {0};
	supported_features1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	supported_features1_3.pNext = &supported_features1_4;

	VkPhysicalDeviceVulkan12Features supported_features1_2 = {0};
	supported_features1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	supported_features1_2.pNext = &supported_features1_3;

	VkPhysicalDeviceFeatures2 supported_features = {0};
	supported_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	supported_features.pNext = &supported_features1_2;

	vkGetPhysicalDeviceFeatures2(window->vk_objects.physicalDevice, &supported_features);

	if (!supported_features1_3.dynamicRendering || !supported_features1_3.synchronization2 ||
		!supported_features1_2.timelineSemaphore) {
		printf("physical device doesn't meet the feature requirements.\n");
		return;
	}

	VkPhysicalDeviceVulkan14Features features1_4 = {0};
	features1_4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
	features1_4.pNext = NULL;

	VkPhysicalDeviceVulkan13Features features1_3 = {0};
	features1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features1_3.pNext = &features1_4;
	features1_3.synchronization2 = VK_TRUE;
	features1_3.dynamicRendering = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features1_2 = {0};
	features1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features1_2.pNext = &features1_3;
	features1_2.timelineSemaphore = VK_TRUE;

	VkPhysicalDeviceFeatures2 features = {0};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features1_2;

	float queue_priorities[1] = {1.0f};
	VkDeviceQueueCreateInfo gfx_queue_info = {0};
	gfx_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	gfx_queue_info.pNext = NULL;
	gfx_queue_info.flags = 0;
	gfx_queue_info.queueFamilyIndex = window->vk_objects.gfxQueueFamIdx;
	gfx_queue_info.queueCount = 1;
	gfx_queue_info.pQueuePriorities = queue_priorities;

	// device specific extensions
	const char* device_extensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkDeviceCreateInfo device_create_info = {0};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.pNext = &features;
	device_create_info.flags = 0;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &gfx_queue_info;
	device_create_info.enabledLayerCount = 0;
	device_create_info.ppEnabledLayerNames = NULL;
	device_create_info.enabledExtensionCount = 1;
	device_create_info.ppEnabledExtensionNames = device_extensions;
	device_create_info.pEnabledFeatures = NULL;

	if (vkCreateDevice(window->vk_objects.physicalDevice, &device_create_info, NULL, &window->vk_objects.device) != VK_SUCCESS) {
		return;
	}

	vkGetDeviceQueue(window->vk_objects.device, window->vk_objects.gfxQueueFamIdx, 0, &window->vk_objects.deviceQueue);

    printf("Device created!\n");
}


void createOutputImage(Window_t* window) {

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkImageCreateInfo image_create_info = {0};
		image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = OUTPUT_IMAGE_FORMAT;
		image_create_info.extent.width = window->width;
		image_create_info.extent.height = window->height;
		image_create_info.extent.depth = 1;
		image_create_info.mipLevels = 1;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (vkCreateImage(window->vk_objects.device, &image_create_info, NULL, &window->vk_objects.outputImageRes[i].outputImage) != VK_SUCCESS) {
			printf("failed to create output image.\n");
			return;
		}

		VkMemoryRequirements mem_reqs;
		vkGetImageMemoryRequirements(window->vk_objects.device, window->vk_objects.outputImageRes[i].outputImage, &mem_reqs);

		VkMemoryAllocateInfo alloc_info = {0};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = findMemoryTypeIndex(window, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		if (vkAllocateMemory(window->vk_objects.device, &alloc_info, NULL, &window->vk_objects.outputImageRes[i].outputImageMemory) != VK_SUCCESS) {
			printf("failed to allocate output image memory.\n");
			return;
		}
		vkBindImageMemory(window->vk_objects.device, window->vk_objects.outputImageRes[i].outputImage, window->vk_objects.outputImageRes[i].outputImageMemory, 0);

		VkImageViewCreateInfo view_create_info = {0};
		view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_create_info.image = window->vk_objects.outputImageRes[i].outputImage;
		view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_create_info.format = OUTPUT_IMAGE_FORMAT;
		view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_create_info.subresourceRange.baseMipLevel = 0;
		view_create_info.subresourceRange.levelCount = 1;
		view_create_info.subresourceRange.baseArrayLayer = 0;
		view_create_info.subresourceRange.layerCount = 1;

		if (vkCreateImageView(window->vk_objects.device, &view_create_info, NULL, &window->vk_objects.outputImageRes[i].outputImageView) != VK_SUCCESS) {
			printf("failed to create output image view.\n");
			return;
		}
	}

	printf("output image created.\n");
}


// single persistent image (not double-buffered like outputImageRes) so it can hold a running
// average across frames; high-precision float format since it accumulates many samples over time
void createAccumImage(Window_t* window) {

	VkImageCreateInfo image_create_info = {0};
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = ACCUM_IMAGE_FORMAT;
	image_create_info.extent.width = window->width;
	image_create_info.extent.height = window->height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_STORAGE_BIT;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (vkCreateImage(window->vk_objects.device, &image_create_info, NULL, &window->vk_objects.accumImageRes.outputImage) != VK_SUCCESS) {
		printf("failed to create accumulation image.\n");
		return;
	}

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(window->vk_objects.device, window->vk_objects.accumImageRes.outputImage, &mem_reqs);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = findMemoryTypeIndex(window, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(window->vk_objects.device, &alloc_info, NULL, &window->vk_objects.accumImageRes.outputImageMemory) != VK_SUCCESS) {
		printf("failed to allocate accumulation image memory.\n");
		return;
	}
	vkBindImageMemory(window->vk_objects.device, window->vk_objects.accumImageRes.outputImage, window->vk_objects.accumImageRes.outputImageMemory, 0);

	VkImageViewCreateInfo view_create_info = {0};
	view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_create_info.image = window->vk_objects.accumImageRes.outputImage;
	view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_create_info.format = ACCUM_IMAGE_FORMAT;
	view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_create_info.subresourceRange.baseMipLevel = 0;
	view_create_info.subresourceRange.levelCount = 1;
	view_create_info.subresourceRange.baseArrayLayer = 0;
	view_create_info.subresourceRange.layerCount = 1;

	if (vkCreateImageView(window->vk_objects.device, &view_create_info, NULL, &window->vk_objects.accumImageRes.outputImageView) != VK_SUCCESS) {
		printf("failed to create accumulation image view.\n");
		return;
	}

	printf("accumulation image created.\n");
}


uint32_t findMemoryTypeIndex(Window_t* window, uint32_t typeBits, VkMemoryPropertyFlags required) {
	VkPhysicalDeviceMemoryProperties mem_props;
	vkGetPhysicalDeviceMemoryProperties(window->vk_objects.physicalDevice, &mem_props);

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
		bool type_supported = typeBits & (1 << i);
		bool has_properties = (mem_props.memoryTypes[i].propertyFlags & required) == required;
		if (type_supported && has_properties) {
			return i;
		}
	}

	printf("failed to find suitable memory type.\n");
	return UINT32_MAX;
}


void createSwapchain(Window_t* window) {

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(window->vk_objects.physicalDevice,window->vk_objects.surface,&surface_capabilities);
    printf("fetched capabilities from surface.\n");

    uint32_t requestedImageCount = surface_capabilities.minImageCount < 3U ? 3U : surface_capabilities.minImageCount;
    if (surface_capabilities.maxImageCount > 0) {
        requestedImageCount = requestedImageCount > surface_capabilities.maxImageCount ? surface_capabilities.maxImageCount :  requestedImageCount;
    }

    //fetch surface formats
	uint32_t surface_form_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(window->vk_objects.physicalDevice,window->vk_objects.surface,&surface_form_count,NULL);
	VkSurfaceFormatKHR *surface_formats = malloc(surface_form_count * sizeof(VkSurfaceFormatKHR));
	vkGetPhysicalDeviceSurfaceFormatsKHR(window->vk_objects.physicalDevice,window->vk_objects.surface,&surface_form_count,surface_formats);
	printf("fetched %d surface formats.\n",surface_form_count);
	for(uint32_t i=0;i<surface_form_count;i++){
		printf("format:%d\tcolorspace:%d\n",surface_formats[i].format,surface_formats[i].colorSpace);
	}

    //fetch surface present mode
	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(window->vk_objects.physicalDevice,window->vk_objects.surface,&present_mode_count,NULL);
	VkPresentModeKHR *present_modes = malloc(present_mode_count * sizeof(VkPresentModeKHR));
	vkGetPhysicalDeviceSurfacePresentModesKHR(window->vk_objects.physicalDevice,window->vk_objects.surface,&present_mode_count,present_modes);
	printf("fetched %d present modes.\n",present_mode_count);
	char mailbox_mode_supported=0;
	for(uint32_t i=0;i<present_mode_count;i++){
		printf("present mode:%d\n",present_modes[i]);
		if(present_modes[i]==VK_PRESENT_MODE_MAILBOX_KHR){
			printf("mailbox present mode supported.\n");
			mailbox_mode_supported=1;
		}
	}
	free(present_modes);

    //add checks here later
    VkExtent2D actual_extent;
	actual_extent.width=window->width;
	actual_extent.height=window->height;

    //pick the format the pipeline was built for, falling back to whatever the surface offers first
	VkSurfaceFormatKHR chosen_format = surface_formats[0];
	for(uint32_t i=0;i<surface_form_count;i++){
		if(surface_formats[i].format==SWAPCHAIN_FORMAT && surface_formats[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
			chosen_format = surface_formats[i];
			break;
		}
	}
	free(surface_formats);

    //create swapchain

	VkSwapchainCreateInfoKHR swap_create_info;

	swap_create_info.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swap_create_info.pNext=NULL;
	swap_create_info.flags=0;
	swap_create_info.surface=window->vk_objects.surface;
	swap_create_info.minImageCount=requestedImageCount;
	swap_create_info.imageFormat=chosen_format.format;
	swap_create_info.imageColorSpace=chosen_format.colorSpace;
	swap_create_info.imageExtent = actual_extent;
	swap_create_info.imageArrayLayers=1;
	swap_create_info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	swap_create_info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
	swap_create_info.queueFamilyIndexCount=0;
	swap_create_info.pQueueFamilyIndices=NULL;
	swap_create_info.preTransform=surface_capabilities.currentTransform;
	swap_create_info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swap_create_info.presentMode = mailbox_mode_supported ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
	swap_create_info.clipped=VK_TRUE;
	swap_create_info.oldSwapchain=VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(window->vk_objects.device, &swap_create_info, NULL, &window->vk_objects.swapchain_data.swapchain) != VK_SUCCESS){
		printf("Error creating swapchain\n");
		return;
	}
    printf("swapchain created\n");
	window->vk_objects.swapchain_data.swapchainWidth = actual_extent.width;
	window->vk_objects.swapchain_data.swapchainHeight = actual_extent.height;

    // ask for the swapchain images
	uint32_t swap_image_count = 0;
	vkGetSwapchainImagesKHR(window->vk_objects.device, window->vk_objects.swapchain_data.swapchain, &swap_image_count, NULL);
	window->vk_objects.swapchain_data.swapchainImageCount = swap_image_count;
	window->vk_objects.swapchain_data.swapchainImages = calloc(swap_image_count, sizeof(VkImage));
	vkGetSwapchainImagesKHR(window->vk_objects.device, window->vk_objects.swapchain_data.swapchain, &swap_image_count, window->vk_objects.swapchain_data.swapchainImages);
	window->vk_objects.swapchain_data.renderCompleteSemaphores = calloc(swap_image_count, sizeof(VkSemaphore));

    //one render-complete semaphore per swapchain image, since imageCount may differ from MAX_FRAMES_IN_FLIGHT
    VkSemaphoreCreateInfo render_complete_semaphore_info;
    render_complete_semaphore_info.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    render_complete_semaphore_info.pNext=NULL;
    render_complete_semaphore_info.flags=0;
    for(uint32_t i=0;i<swap_image_count;i++) {
        vkCreateSemaphore(window->vk_objects.device,&render_complete_semaphore_info,NULL,&window->vk_objects.swapchain_data.renderCompleteSemaphores[i]);
    }
    printf("render-complete semaphores created.\n");
}



void createComputePipeline(Window_t* window) {
	Pipeline_t pipeline = {0};

	VkDescriptorSetLayoutBinding bindings[11] = {0};
	bindings[0].binding = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; // brick atlas texture (voxel data)
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[1].binding = 3;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[2].binding = 4;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[3].binding = 2;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[4].binding = 5;
	bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[4].descriptorCount = 1;
	bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[5].binding = 6;
	bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; // brick indirection texture (brick coord -> atlas slot)
	bindings[5].descriptorCount = 1;
	bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[6].binding = 7;
	bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; // voxel occupancy texture, per chunk
	bindings[6].descriptorCount = 1;
	bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[7].binding = 8;
	bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // entity data (array)
	bindings[7].descriptorCount = 1;
	bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[8].binding = 9;
	bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // model table
	bindings[8].descriptorCount = 1;
	bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[9].binding = 10;
	bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // model voxel data
	bindings[9].descriptorCount = 1;
	bindings[9].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[10].binding = 11;
	bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; // chunk skip texture (coarsest DDA skip)
	bindings[10].descriptorCount = 1;
	bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo set_layout_info = {0};
	set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout_info.bindingCount = 11;
	set_layout_info.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(window->vk_objects.device, &set_layout_info, NULL, &window->vk_objects.computeDescriptorSetLayout) != VK_SUCCESS) {
		printf("Unable to create the compute descriptor set layout\n");
		return;
	}

	VkPushConstantRange push_constant_range = {0};
	push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_constant_range.offset = 0;
	push_constant_range.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &window->vk_objects.computeDescriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_constant_range;

	if (vkCreatePipelineLayout(window->vk_objects.device, &pipeline_layout_info, NULL, &pipeline.layout) != VK_SUCCESS) {
		printf("Unable to create the compute pipeline layout\n");
		return;
	}

	VkPipelineShaderStageCreateInfo stage_info = {0};
	stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_info.module = window->vk_objects.computeShader;
	stage_info.pName = "main";

	VkComputePipelineCreateInfo compute_pipeline_info = {0};
	compute_pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	compute_pipeline_info.stage = stage_info;
	compute_pipeline_info.layout = pipeline.layout;

	if (vkCreateComputePipelines(window->vk_objects.device, VK_NULL_HANDLE, 1, &compute_pipeline_info, NULL, &pipeline.handle) != VK_SUCCESS) {
		printf("Error creating the compute pipeline\n");
		return;
	}

	vkDestroyShaderModule(window->vk_objects.device, window->vk_objects.computeShader, NULL);
	printf("compute shader module destroyed.\n");

	window->vk_objects.computePipeline = pipeline;
	printf("compute pipeline created.\n");
}


void createComputeShader(Window_t* window) {
	FILE *fp_compute=NULL;

	fp_compute=fopen("shaders/compute.spv","rb+");

	if(fp_compute==NULL){
		printf("can't find compute SPIR-V binary.\n");
		return;
	}

	fseek(fp_compute,0,SEEK_END);
	uint32_t compute_size=ftell(fp_compute);

	char *p_compute_code=(char *)malloc(compute_size*sizeof(char));

	rewind(fp_compute);
	fread(p_compute_code,1,compute_size,fp_compute);
	printf("compute shader binary loaded.\n");

	fclose(fp_compute);

	//create shader module
	VkShaderModuleCreateInfo compute_shader_module_create_info;
	compute_shader_module_create_info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	compute_shader_module_create_info.pNext=NULL;
	compute_shader_module_create_info.flags=0;
	compute_shader_module_create_info.codeSize=compute_size;
	compute_shader_module_create_info.pCode=(const uint32_t *)p_compute_code;

	vkCreateShaderModule(window->vk_objects.device,&compute_shader_module_create_info,NULL,&window->vk_objects.computeShader);
	printf("compute shader module created.\n");

	free(p_compute_code);
	printf("compute shader binary released.\n");
}


Vk_Buffer_t createHostVisibleBuffer(Window_t* window, VkDeviceSize size, VkBufferUsageFlags usage) {
	Vk_Buffer_t buf = {0};

	VkBufferCreateInfo buffer_create_info = {0};
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = usage;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(window->vk_objects.device, &buffer_create_info, NULL, &buf.buffer) != VK_SUCCESS) {
		printf("failed to create buffer.\n");
		return buf;
	}

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(window->vk_objects.device, buf.buffer, &mem_reqs);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = findMemoryTypeIndex(window, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	if (vkAllocateMemory(window->vk_objects.device, &alloc_info, NULL, &buf.bufferMemory) != VK_SUCCESS) {
		printf("failed to allocate buffer memory.\n");
		return buf;
	}
	vkBindBufferMemory(window->vk_objects.device, buf.buffer, buf.bufferMemory, 0);

	vkMapMemory(window->vk_objects.device, buf.bufferMemory, 0, size, 0, &buf.mapped);
	memset(buf.mapped, 0, size);

	return buf;
}

Vk_Buffer_t createDeviceLocalBuffer(Window_t* window, VkDeviceSize size, VkBufferUsageFlags usage) {
	Vk_Buffer_t buf = {0};

	VkBufferCreateInfo buffer_create_info = {0};
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(window->vk_objects.device, &buffer_create_info, NULL, &buf.buffer) != VK_SUCCESS) {
		printf("failed to create device-local buffer.\n");
		return buf;
	}

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(window->vk_objects.device, buf.buffer, &mem_reqs);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = findMemoryTypeIndex(window, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(window->vk_objects.device, &alloc_info, NULL, &buf.bufferMemory) != VK_SUCCESS) {
		printf("failed to allocate device-local buffer memory.\n");
		return buf;
	}
	vkBindBufferMemory(window->vk_objects.device, buf.buffer, buf.bufferMemory, 0);

	return buf;
}

// one-time staging upload; blocks until the copy finishes (startup-only path)
void uploadBufferData(Window_t* window, VkBuffer dst, const void* data, VkDeviceSize size) {
	Vk_Buffer_t staging = createHostVisibleBuffer(window, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	memcpy(staging.mapped, data, size);

	VkCommandBufferAllocateInfo cmd_alloc_info = {0};
	cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin_info);

	VkBufferCopy region = {0};
	region.size = size;
	vkCmdCopyBuffer(cmd, staging.buffer, dst, 1, &region);

	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;
	vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(window->vk_objects.deviceQueue);

	vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);
	destroyVkBuffer(window->vk_objects.device, &staging);
}

// shared creation path for the brick atlas / indirection / occupancy textures -- same layout, different extents+formats
static void createVoxelTexture3D(Window_t* window, Vk_Image_t* target, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, const char* debug_name) {
	VkImageCreateInfo image_create_info = {0};
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_3D;
	image_create_info.format = format;
	image_create_info.extent.width = width;
	image_create_info.extent.height = height;
	image_create_info.extent.depth = depth;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (vkCreateImage(window->vk_objects.device, &image_create_info, NULL, &target->outputImage) != VK_SUCCESS) {
		printf("failed to create %s.\n", debug_name);
		return;
	}

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(window->vk_objects.device, target->outputImage, &mem_reqs);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = findMemoryTypeIndex(window, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(window->vk_objects.device, &alloc_info, NULL, &target->outputImageMemory) != VK_SUCCESS) {
		printf("failed to allocate %s memory.\n", debug_name);
		return;
	}
	vkBindImageMemory(window->vk_objects.device, target->outputImage, target->outputImageMemory, 0);

	VkImageViewCreateInfo view_create_info = {0};
	view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_create_info.image = target->outputImage;
	view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
	view_create_info.format = format;
	view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_create_info.subresourceRange.baseMipLevel = 0;
	view_create_info.subresourceRange.levelCount = 1;
	view_create_info.subresourceRange.baseArrayLayer = 0;
	view_create_info.subresourceRange.layerCount = 1;

	if (vkCreateImageView(window->vk_objects.device, &view_create_info, NULL, &target->outputImageView) != VK_SUCCESS) {
		printf("failed to create %s view.\n", debug_name);
		return;
	}

	printf("%s created.\n", debug_name);
}

void createBrickTextures(Window_t* window) {

	//voxel data, atlas-packed VOXEL_MASK_BLOCK_SIZE^3 bricks
	createVoxelTexture3D(window, &window->vk_objects.brickAtlasTexture,
		MAX_LOADED_VOXEL_DIM, MAX_LOADED_VOXEL_DIM, MAX_LOADED_VOXEL_DIM,
		WORLD_TEXTURE_FORMAT, "brick atlas texture");

	// brick coord, one texel per brick
	uint32_t bricksPerAxis = MAX_LOADED_VOXEL_DIM / VOXEL_BRICK_SIZE;
	createVoxelTexture3D(window, &window->vk_objects.brickIndirectionTexture,
		bricksPerAxis, bricksPerAxis, bricksPerAxis,
		VK_FORMAT_R32_UINT, "brick indirection texture");

	//fine per-voxel occupancy
	createVoxelTexture3D(window, &window->vk_objects.voxelOccupancyTexture,
		MAX_LOADED_VOXEL_DIM / 32, MAX_LOADED_VOXEL_DIM, MAX_LOADED_VOXEL_DIM,
		VK_FORMAT_R32_UINT, "voxel occupancy texture");

	// coarsest skip: 1 texel per chunk, nonzero if that chunk has any geometry at all
	createVoxelTexture3D(window, &window->vk_objects.chunkSkipTexture,
		CHUNK_STREAM_WINDOW, CHUNK_STREAM_WINDOW, CHUNK_STREAM_WINDOW,
		WORLD_TEXTURE_FORMAT, "chunk skip texture");
}

void createComputeBuffers(Window_t* window) {
	createBrickTextures(window);

	VkDeviceSize materials_size = (VkDeviceSize)MAX_MATERIALS * sizeof(Material);
	window->vk_objects.materialProperitesBuffer = createDeviceLocalBuffer(window, materials_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		window->vk_objects.cameraDataBuffer[i] = createHostVisibleBuffer(window, sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	}

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		window->vk_objects.entityDataBuffer[i] = createHostVisibleBuffer(window, (VkDeviceSize)MAX_ENTITIES * sizeof(EntityData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	}

	VkDeviceSize model_size = (VkDeviceSize)MAX_MODELS * sizeof(GpuModel);
	window->vk_objects.modelBuffer = createDeviceLocalBuffer(window, model_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	VkDeviceSize model_voxels_size = 0;
	for (uint32_t i = 0; i < MAX_MODELS; i++) {
		model_voxels_size += model_instance_list[i].size;
	}
	if (model_voxels_size == 0) { model_voxels_size = 1; } // buffers can't be zero-sized
	window->vk_objects.modelVoxelBuffer = createDeviceLocalBuffer(window, model_voxels_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	printf("compute buffers created.\n");
}

void window_world_occupancy_mask_upload(Window_t* window, World* wrld) {
	uint32_t chunkTexelsX = CHUNK_DIM / 32;

	for (int i = 0; i < hmlen(wrld->chunk_map); i++) {
		Chunk* chunk = &wrld->chunk_map[i].value;
		if (chunk->voxelMask == NULL) { continue; }

		VkDeviceSize mask_size = (VkDeviceSize)(CHUNK_SIZE >> 5) * sizeof(uint32_t);
		Vk_Buffer_t staging = createHostVisibleBuffer(window, mask_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		memcpy(staging.mapped, chunk->voxelMask, mask_size);

		VkCommandBufferAllocateInfo cmd_alloc_info = {0};
		cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
		cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_alloc_info.commandBufferCount = 1;

		VkCommandBuffer cmd;
		vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

		VkCommandBufferBeginInfo begin_info = {0};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &begin_info);

		VkImageMemoryBarrier2 to_transfer = {0};
		to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		to_transfer.srcAccessMask = 0;
		to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; 
		to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer.image = window->vk_objects.voxelOccupancyTexture.outputImage;
		to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		to_transfer.subresourceRange.levelCount = 1;
		to_transfer.subresourceRange.layerCount = 1;

		VkDependencyInfo to_transfer_dep = {0};
		to_transfer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		to_transfer_dep.imageMemoryBarrierCount = 1;
		to_transfer_dep.pImageMemoryBarriers = &to_transfer;
		vkCmdPipelineBarrier2(cmd, &to_transfer_dep);

		iVector_t coord = wrld->chunk_map[i].key;

		VkBufferImageCopy region = {0};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset.x = wrapChunkCoord(coord.x) * (int32_t)chunkTexelsX;
		region.imageOffset.y = wrapChunkCoord(coord.y) * (int32_t)CHUNK_DIM;
		region.imageOffset.z = wrapChunkCoord(coord.z) * (int32_t)CHUNK_DIM;
		region.imageExtent.width = chunkTexelsX;
		region.imageExtent.height = CHUNK_DIM;
		region.imageExtent.depth = CHUNK_DIM;

		vkCmdCopyBufferToImage(cmd, staging.buffer, window->vk_objects.voxelOccupancyTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageMemoryBarrier2 to_shader_read = to_transfer;
		to_shader_read.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_shader_read.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDependencyInfo to_shader_dep = {0};
		to_shader_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		to_shader_dep.imageMemoryBarrierCount = 1;
		to_shader_dep.pImageMemoryBarriers = &to_shader_read;
		vkCmdPipelineBarrier2(cmd, &to_shader_dep);

		vkEndCommandBuffer(cmd);

		VkSubmitInfo submit_info = {0};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &cmd;
		vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
		vkQueueWaitIdle(window->vk_objects.deviceQueue);

		vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);
		destroyVkBuffer(window->vk_objects.device, &staging);
	}
}




void window_world_atlas_upload(Window_t* window, World* wrld) {
	const uint32_t bricksPerChunkAxis = CHUNK_DIM / VOXEL_BRICK_SIZE; // 4
	const uint32_t bricksPerChunk = bricksPerChunkAxis * bricksPerChunkAxis * bricksPerChunkAxis; // 64
	const uint32_t brickVoxels = VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE; // 512

	
	VkCommandBufferAllocateInfo cmd_alloc_info = {0};
	cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin_info);

	VkImageMemoryBarrier2 to_transfer[2] = {0};
	for (int b = 0; b < 2; b++) {
		to_transfer[b].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		to_transfer[b].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		to_transfer[b].srcAccessMask = 0;
		to_transfer[b].dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
		to_transfer[b].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_transfer[b].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		to_transfer[b].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_transfer[b].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer[b].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer[b].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		to_transfer[b].subresourceRange.levelCount = 1;
		to_transfer[b].subresourceRange.layerCount = 1;
	}
	to_transfer[0].image = window->vk_objects.brickIndirectionTexture.outputImage;
	to_transfer[1].image = window->vk_objects.chunkSkipTexture.outputImage;

	VkDependencyInfo to_transfer_dep = {0};
	to_transfer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_transfer_dep.imageMemoryBarrierCount = 2;
	to_transfer_dep.pImageMemoryBarriers = to_transfer;
	vkCmdPipelineBarrier2(cmd, &to_transfer_dep);

	VkClearColorValue empty_slot_value = {0};
	empty_slot_value.uint32[0] = EMPTY_SLOT;
	empty_slot_value.uint32[1] = EMPTY_SLOT;
	empty_slot_value.uint32[2] = EMPTY_SLOT;
	empty_slot_value.uint32[3] = EMPTY_SLOT;

	VkClearColorValue zero_value = {0}; // chunk skip mask: 0 = no geometry

	VkImageSubresourceRange clear_range = {0};
	clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	clear_range.levelCount = 1;
	clear_range.layerCount = 1;

	vkCmdClearColorImage(cmd, window->vk_objects.brickIndirectionTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &empty_slot_value, 1, &clear_range);
	vkCmdClearColorImage(cmd, window->vk_objects.chunkSkipTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero_value, 1, &clear_range);

	VkImageMemoryBarrier2 to_shader_read[2];
	for (int b = 0; b < 2; b++) {
		to_shader_read[b] = to_transfer[b];
		to_shader_read[b].srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
		to_shader_read[b].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_shader_read[b].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		to_shader_read[b].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		to_shader_read[b].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_shader_read[b].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VkDependencyInfo to_shader_dep = {0};
	to_shader_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_shader_dep.imageMemoryBarrierCount = 2;
	to_shader_dep.pImageMemoryBarriers = to_shader_read;
	vkCmdPipelineBarrier2(cmd, &to_shader_dep);

	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;
	vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(window->vk_objects.deviceQueue);

	vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);

	for (int ci = 0; ci < hmlen(wrld->chunk_map); ci++) {
		Chunk* chunk = &wrld->chunk_map[ci].value;
		if (chunk->voxels == NULL || chunk->brickMask == 0) { continue; }
		iVector_t coord = wrld->chunk_map[ci].key;

		uint32_t solidCount = 0;
		for (uint32_t b = 0; b < bricksPerChunk; b++) {
			if (chunk->brickMask & (1ull << b)) { solidCount++; }
		}
		if (solidCount == 0) { continue; }

		VkDeviceSize voxel_staging_size = (VkDeviceSize)solidCount * brickVoxels;
		VkDeviceSize slot_staging_size = (VkDeviceSize)solidCount * sizeof(uint32_t);

		Vk_Buffer_t voxel_staging = createHostVisibleBuffer(window, voxel_staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		Vk_Buffer_t slot_staging = createHostVisibleBuffer(window, slot_staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

		VkBufferImageCopy* atlas_regions = malloc(sizeof(VkBufferImageCopy) * solidCount);
		VkBufferImageCopy* indirection_regions = malloc(sizeof(VkBufferImageCopy) * solidCount);

		uint8_t* voxel_dst = (uint8_t*)voxel_staging.mapped;
		uint32_t* slot_dst = (uint32_t*)slot_staging.mapped;

		uint32_t writeIdx = 0;
		for (uint32_t b = 0; b < bricksPerChunk; b++) {
			if (!(chunk->brickMask & (1ull << b))) { continue; }

			uint32_t bx = b % bricksPerChunkAxis;
			uint32_t by = (b / bricksPerChunkAxis) % bricksPerChunkAxis;
			uint32_t bz = b / (bricksPerChunkAxis * bricksPerChunkAxis);

			// pull this brick's voxels out of the chunk's flat array; not contiguous, so copy row by row
			uint8_t* brickDst = voxel_dst + (VkDeviceSize)writeIdx * brickVoxels;
			for (uint32_t vz = 0; vz < VOXEL_BRICK_SIZE; vz++) {
				for (uint32_t vy = 0; vy < VOXEL_BRICK_SIZE; vy++) {
					uint32_t srcX = bx * VOXEL_BRICK_SIZE;
					uint32_t srcY = by * VOXEL_BRICK_SIZE + vy;
					uint32_t srcZ = bz * VOXEL_BRICK_SIZE + vz;
					uint32_t srcIdx = srcX + srcY * CHUNK_DIM + srcZ * CHUNK_DIM * CHUNK_DIM;
					uint32_t dstRowIdx = vy * VOXEL_BRICK_SIZE + vz * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE;
					memcpy(brickDst + dstRowIdx, &chunk->voxels[srcIdx], VOXEL_BRICK_SIZE);
				}
			}

			uint32_t worldBrickX = (uint32_t)wrapChunkCoord(coord.x) * bricksPerChunkAxis + bx;
			uint32_t worldBrickY = (uint32_t)wrapChunkCoord(coord.y) * bricksPerChunkAxis + by;
			uint32_t worldBrickZ = (uint32_t)wrapChunkCoord(coord.z) * bricksPerChunkAxis + bz;

			atlas_regions[writeIdx] = (VkBufferImageCopy){0};
			atlas_regions[writeIdx].bufferOffset = (VkDeviceSize)writeIdx * brickVoxels;
			atlas_regions[writeIdx].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			atlas_regions[writeIdx].imageSubresource.mipLevel = 0;
			atlas_regions[writeIdx].imageSubresource.baseArrayLayer = 0;
			atlas_regions[writeIdx].imageSubresource.layerCount = 1;
			atlas_regions[writeIdx].imageOffset.x = (int32_t)(worldBrickX * VOXEL_BRICK_SIZE);
			atlas_regions[writeIdx].imageOffset.y = (int32_t)(worldBrickY * VOXEL_BRICK_SIZE);
			atlas_regions[writeIdx].imageOffset.z = (int32_t)(worldBrickZ * VOXEL_BRICK_SIZE);
			atlas_regions[writeIdx].imageExtent.width = VOXEL_BRICK_SIZE;
			atlas_regions[writeIdx].imageExtent.height = VOXEL_BRICK_SIZE;
			atlas_regions[writeIdx].imageExtent.depth = VOXEL_BRICK_SIZE;

			slot_dst[writeIdx] = worldBrickX | (worldBrickY << 8) | (worldBrickZ << 16); // packed atlas slot coord, unpacked with shifts+masks in-shader

			indirection_regions[writeIdx] = (VkBufferImageCopy){0};
			indirection_regions[writeIdx].bufferOffset = (VkDeviceSize)writeIdx * sizeof(uint32_t);
			indirection_regions[writeIdx].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			indirection_regions[writeIdx].imageSubresource.mipLevel = 0;
			indirection_regions[writeIdx].imageSubresource.baseArrayLayer = 0;
			indirection_regions[writeIdx].imageSubresource.layerCount = 1;
			indirection_regions[writeIdx].imageOffset.x = (int32_t)worldBrickX;
			indirection_regions[writeIdx].imageOffset.y = (int32_t)worldBrickY;
			indirection_regions[writeIdx].imageOffset.z = (int32_t)worldBrickZ;
			indirection_regions[writeIdx].imageExtent.width = 1;
			indirection_regions[writeIdx].imageExtent.height = 1;
			indirection_regions[writeIdx].imageExtent.depth = 1;

			writeIdx++;
		}

		// this chunk definitely has geometry (solidCount == 0 already skipped above), so the
		// coarsest skip texture always gets a 1 here -- single texel, chunk-granularity coordinate
		Vk_Buffer_t skip_staging = createHostVisibleBuffer(window, sizeof(uint8_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		*(uint8_t*)skip_staging.mapped = 1;

		VkBufferImageCopy skip_region = {0};
		skip_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		skip_region.imageSubresource.mipLevel = 0;
		skip_region.imageSubresource.baseArrayLayer = 0;
		skip_region.imageSubresource.layerCount = 1;
		skip_region.imageOffset.x = wrapChunkCoord(coord.x);
		skip_region.imageOffset.y = wrapChunkCoord(coord.y);
		skip_region.imageOffset.z = wrapChunkCoord(coord.z);
		skip_region.imageExtent.width = 1;
		skip_region.imageExtent.height = 1;
		skip_region.imageExtent.depth = 1;

		VkCommandBufferAllocateInfo cmd_alloc_info = {0};
		cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
		cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_alloc_info.commandBufferCount = 1;

		VkCommandBuffer cmd;
		vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

		VkCommandBufferBeginInfo begin_info = {0};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &begin_info);

		VkImageMemoryBarrier2 to_transfer[3] = {0};
		for (int b = 0; b < 3; b++) {
			to_transfer[b].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			to_transfer[b].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
			to_transfer[b].srcAccessMask = 0;
			to_transfer[b].dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
			to_transfer[b].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			to_transfer[b].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // whole-image transition; fine while only one chunk ever uploads
			to_transfer[b].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_transfer[b].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer[b].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer[b].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			to_transfer[b].subresourceRange.levelCount = 1;
			to_transfer[b].subresourceRange.layerCount = 1;
		}
		to_transfer[0].image = window->vk_objects.brickAtlasTexture.outputImage;
		to_transfer[1].image = window->vk_objects.brickIndirectionTexture.outputImage;
		to_transfer[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // just came out of the clear pass above
		to_transfer[2].image = window->vk_objects.chunkSkipTexture.outputImage;
		to_transfer[2].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // just came out of the clear pass above

		VkDependencyInfo to_transfer_dep = {0};
		to_transfer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		to_transfer_dep.imageMemoryBarrierCount = 3;
		to_transfer_dep.pImageMemoryBarriers = to_transfer;
		vkCmdPipelineBarrier2(cmd, &to_transfer_dep);

		vkCmdCopyBufferToImage(cmd, voxel_staging.buffer, window->vk_objects.brickAtlasTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, solidCount, atlas_regions);
		vkCmdCopyBufferToImage(cmd, slot_staging.buffer, window->vk_objects.brickIndirectionTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, solidCount, indirection_regions);
		vkCmdCopyBufferToImage(cmd, skip_staging.buffer, window->vk_objects.chunkSkipTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &skip_region);

		VkImageMemoryBarrier2 to_shader_read[3];
		for (int b = 0; b < 3; b++) {
			to_shader_read[b] = to_transfer[b];
			to_shader_read[b].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
			to_shader_read[b].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			to_shader_read[b].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			to_shader_read[b].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			to_shader_read[b].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_shader_read[b].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VkDependencyInfo to_shader_dep = {0};
		to_shader_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		to_shader_dep.imageMemoryBarrierCount = 3;
		to_shader_dep.pImageMemoryBarriers = to_shader_read;
		vkCmdPipelineBarrier2(cmd, &to_shader_dep);

		vkEndCommandBuffer(cmd);

		VkSubmitInfo submit_info = {0};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &cmd;
		vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
		vkQueueWaitIdle(window->vk_objects.deviceQueue);

		vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);

		free(atlas_regions);
		free(indirection_regions);
		destroyVkBuffer(window->vk_objects.device, &voxel_staging);
		destroyVkBuffer(window->vk_objects.device, &slot_staging);
		destroyVkBuffer(window->vk_objects.device, &skip_staging);
	}
}

//one time loads the spawn area
void window_world_spawn_load(Window_t* window, World* wrld) {


	//texture to store bricks on the gpu 
	//+indirection texture which points to
	window_world_atlas_upload(window, wrld);

	// vox occupancy texture
	window_world_occupancy_mask_upload(window, wrld);

}

void window_world_chunk_occupancy_mask_upload(Window_t* window, World* wrld, Chunk** chunks, uint32_t count) {
	if (count == 0) { return; }

	uint32_t chunkTexelsX = CHUNK_DIM / 32;
	VkDeviceSize mask_size = (VkDeviceSize)(CHUNK_SIZE >> 5) * sizeof(uint32_t);

	Vk_Buffer_t staging = createHostVisibleBuffer(window, mask_size * count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	VkBufferImageCopy* regions = malloc(sizeof(VkBufferImageCopy) * count);
	uint8_t* dst = (uint8_t*)staging.mapped;
	uint32_t writeCount = 0;

	for (uint32_t c = 0; c < count; c++) {
		Chunk* chunk = chunks[c];
		if (chunk->voxelMask == NULL) { continue; }

		memcpy(dst + (VkDeviceSize)writeCount * mask_size, chunk->voxelMask, mask_size);

		VkBufferImageCopy region = {0};
		region.bufferOffset = (VkDeviceSize)writeCount * mask_size;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset.x = wrapChunkCoord(chunk->coord.x) * (int32_t)chunkTexelsX;
		region.imageOffset.y = wrapChunkCoord(chunk->coord.y) * (int32_t)CHUNK_DIM;
		region.imageOffset.z = wrapChunkCoord(chunk->coord.z) * (int32_t)CHUNK_DIM;
		region.imageExtent.width = chunkTexelsX;
		region.imageExtent.height = CHUNK_DIM;
		region.imageExtent.depth = CHUNK_DIM;

		regions[writeCount++] = region;
	}

	if (writeCount == 0) {
		free(regions);
		destroyVkBuffer(window->vk_objects.device, &staging);
		return;
	}

	VkCommandBufferAllocateInfo cmd_alloc_info = {0};
	cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin_info);

	VkImageMemoryBarrier2 to_transfer = {0};
	to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	to_transfer.srcAccessMask = 0;
	to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // this chunk's region hasn't been written before; fine as a whole-image transition too
	to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_transfer.image = window->vk_objects.voxelOccupancyTexture.outputImage;
	to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_transfer.subresourceRange.levelCount = 1;
	to_transfer.subresourceRange.layerCount = 1;

	VkDependencyInfo to_transfer_dep = {0};
	to_transfer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_transfer_dep.imageMemoryBarrierCount = 1;
	to_transfer_dep.pImageMemoryBarriers = &to_transfer;
	vkCmdPipelineBarrier2(cmd, &to_transfer_dep);

	vkCmdCopyBufferToImage(cmd, staging.buffer, window->vk_objects.voxelOccupancyTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, writeCount, regions);

	VkImageMemoryBarrier2 to_shader_read = to_transfer;
	to_shader_read.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	to_shader_read.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDependencyInfo to_shader_dep = {0};
	to_shader_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_shader_dep.imageMemoryBarrierCount = 1;
	to_shader_dep.pImageMemoryBarriers = &to_shader_read;
	vkCmdPipelineBarrier2(cmd, &to_shader_dep);

	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;
	vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(window->vk_objects.deviceQueue);

	vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);
	free(regions);
	destroyVkBuffer(window->vk_objects.device, &staging);
}


void window_world_chunk_atlas_upload(Window_t* window, World* wrld, Chunk** chunks, uint32_t count) {
	const uint32_t bricksPerChunkAxis = CHUNK_DIM / VOXEL_BRICK_SIZE; // 4
	const uint32_t bricksPerChunk = bricksPerChunkAxis * bricksPerChunkAxis * bricksPerChunkAxis; // 64
	const uint32_t brickVoxels = VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE; // 512

	if (count == 0) { return; }

	// pre-pass: count solid bricks so the atlas voxel-data buffer can be sized once, up front
	uint32_t* solidCounts = malloc(sizeof(uint32_t) * count);
	uint32_t totalSolid = 0;
	for (uint32_t c = 0; c < count; c++) {
		Chunk* chunk = chunks[c];
		uint32_t solidCount = 0;
		if (chunk->voxels != NULL && chunk->brickMask != 0) {
			for (uint32_t b = 0; b < bricksPerChunk; b++) {
				if (chunk->brickMask & (1ull << b)) { solidCount++; }
			}
		}
		solidCounts[c] = solidCount;
		totalSolid += solidCount;
	}

	// indirection is rewritten in full (all bricksPerChunk slots) for every chunk here, even
	// wholly-air ones: these GPU textures wrap and get reused by unrelated world coords, so a
	// brick slot this chunk doesn't claim must be reset to EMPTY_SLOT, or it keeps pointing at
	// whatever unrelated chunk last occupied that wrapped slot (ghost geometry in the sky/reflections).
	uint32_t totalBrickSlots = count * bricksPerChunk;
	Vk_Buffer_t slot_staging = createHostVisibleBuffer(window, (VkDeviceSize)totalBrickSlots * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	uint32_t* slot_dst = (uint32_t*)slot_staging.mapped;
	VkBufferImageCopy* indirection_regions = malloc(sizeof(VkBufferImageCopy) * totalBrickSlots);

	// same reasoning for the skip flag: every chunk in the batch writes it, even air ones,
	// so an air chunk correctly overwrites a previously-solid chunk at the same wrapped slot.
	Vk_Buffer_t skip_staging = createHostVisibleBuffer(window, (VkDeviceSize)count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	uint8_t* skip_dst = (uint8_t*)skip_staging.mapped;
	VkBufferImageCopy* skip_regions = malloc(sizeof(VkBufferImageCopy) * count);

	Vk_Buffer_t voxel_staging = {0};
	VkBufferImageCopy* atlas_regions = NULL;
	uint8_t* voxel_dst = NULL;
	if (totalSolid > 0) {
		voxel_staging = createHostVisibleBuffer(window, (VkDeviceSize)totalSolid * brickVoxels, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		atlas_regions = malloc(sizeof(VkBufferImageCopy) * totalSolid);
		voxel_dst = (uint8_t*)voxel_staging.mapped;
	}

	uint32_t writeIdx = 0; // into atlas_regions / voxel_staging (solid bricks only)
	uint32_t brickSlotIdx = 0; // into indirection_regions / slot_staging (every brick slot)
	for (uint32_t c = 0; c < count; c++) {
		Chunk* chunk = chunks[c];

		for (uint32_t b = 0; b < bricksPerChunk; b++) {
			uint32_t bx = b % bricksPerChunkAxis;
			uint32_t by = (b / bricksPerChunkAxis) % bricksPerChunkAxis;
			uint32_t bz = b / (bricksPerChunkAxis * bricksPerChunkAxis);

			uint32_t worldBrickX = (uint32_t)wrapChunkCoord(chunk->coord.x) * bricksPerChunkAxis + bx;
			uint32_t worldBrickY = (uint32_t)wrapChunkCoord(chunk->coord.y) * bricksPerChunkAxis + by;
			uint32_t worldBrickZ = (uint32_t)wrapChunkCoord(chunk->coord.z) * bricksPerChunkAxis + bz;

			slot_dst[brickSlotIdx] = EMPTY_SLOT;

			if (chunk->brickMask & (1ull << b)) {
				// pull this brick's voxels out of the chunk's flat array; not contiguous, so copy row by row
				uint8_t* brickDst = voxel_dst + (VkDeviceSize)writeIdx * brickVoxels;
				for (uint32_t vz = 0; vz < VOXEL_BRICK_SIZE; vz++) {
					for (uint32_t vy = 0; vy < VOXEL_BRICK_SIZE; vy++) {
						uint32_t srcX = bx * VOXEL_BRICK_SIZE;
						uint32_t srcY = by * VOXEL_BRICK_SIZE + vy;
						uint32_t srcZ = bz * VOXEL_BRICK_SIZE + vz;
						uint32_t srcIdx = srcX + srcY * CHUNK_DIM + srcZ * CHUNK_DIM * CHUNK_DIM;
						uint32_t dstRowIdx = vy * VOXEL_BRICK_SIZE + vz * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE;
						memcpy(brickDst + dstRowIdx, &chunk->voxels[srcIdx], VOXEL_BRICK_SIZE);
					}
				}

				atlas_regions[writeIdx] = (VkBufferImageCopy){0};
				atlas_regions[writeIdx].bufferOffset = (VkDeviceSize)writeIdx * brickVoxels;
				atlas_regions[writeIdx].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				atlas_regions[writeIdx].imageSubresource.mipLevel = 0;
				atlas_regions[writeIdx].imageSubresource.baseArrayLayer = 0;
				atlas_regions[writeIdx].imageSubresource.layerCount = 1;
				atlas_regions[writeIdx].imageOffset.x = (int32_t)(worldBrickX * VOXEL_BRICK_SIZE);
				atlas_regions[writeIdx].imageOffset.y = (int32_t)(worldBrickY * VOXEL_BRICK_SIZE);
				atlas_regions[writeIdx].imageOffset.z = (int32_t)(worldBrickZ * VOXEL_BRICK_SIZE);
				atlas_regions[writeIdx].imageExtent.width = VOXEL_BRICK_SIZE;
				atlas_regions[writeIdx].imageExtent.height = VOXEL_BRICK_SIZE;
				atlas_regions[writeIdx].imageExtent.depth = VOXEL_BRICK_SIZE;

				slot_dst[brickSlotIdx] = worldBrickX | (worldBrickY << 8) | (worldBrickZ << 16); // packed atlas slot coord, unpacked with shifts+masks in-shader

				writeIdx++;
			}

			indirection_regions[brickSlotIdx] = (VkBufferImageCopy){0};
			indirection_regions[brickSlotIdx].bufferOffset = (VkDeviceSize)brickSlotIdx * sizeof(uint32_t);
			indirection_regions[brickSlotIdx].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			indirection_regions[brickSlotIdx].imageSubresource.mipLevel = 0;
			indirection_regions[brickSlotIdx].imageSubresource.baseArrayLayer = 0;
			indirection_regions[brickSlotIdx].imageSubresource.layerCount = 1;
			indirection_regions[brickSlotIdx].imageOffset.x = (int32_t)worldBrickX;
			indirection_regions[brickSlotIdx].imageOffset.y = (int32_t)worldBrickY;
			indirection_regions[brickSlotIdx].imageOffset.z = (int32_t)worldBrickZ;
			indirection_regions[brickSlotIdx].imageExtent.width = 1;
			indirection_regions[brickSlotIdx].imageExtent.height = 1;
			indirection_regions[brickSlotIdx].imageExtent.depth = 1;

			brickSlotIdx++;
		}

		skip_dst[c] = solidCounts[c] > 0 ? 1 : 0;

		skip_regions[c] = (VkBufferImageCopy){0};
		skip_regions[c].bufferOffset = (VkDeviceSize)c * sizeof(uint8_t);
		skip_regions[c].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		skip_regions[c].imageSubresource.mipLevel = 0;
		skip_regions[c].imageSubresource.baseArrayLayer = 0;
		skip_regions[c].imageSubresource.layerCount = 1;
		skip_regions[c].imageOffset.x = wrapChunkCoord(chunk->coord.x);
		skip_regions[c].imageOffset.y = wrapChunkCoord(chunk->coord.y);
		skip_regions[c].imageOffset.z = wrapChunkCoord(chunk->coord.z);
		skip_regions[c].imageExtent.width = 1;
		skip_regions[c].imageExtent.height = 1;
		skip_regions[c].imageExtent.depth = 1;
	}

	VkCommandBufferAllocateInfo cmd_alloc_info = {0};
	cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin_info);

	uint32_t imageCount = totalSolid > 0 ? 3 : 2;
	VkImageMemoryBarrier2 to_transfer[3] = {0};
	for (uint32_t b = 0; b < imageCount; b++) {
		to_transfer[b].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		to_transfer[b].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		to_transfer[b].srcAccessMask = 0;
		to_transfer[b].dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		to_transfer[b].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_transfer[b].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // these textures are already initialized by this point
		to_transfer[b].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_transfer[b].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer[b].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer[b].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		to_transfer[b].subresourceRange.levelCount = 1;
		to_transfer[b].subresourceRange.layerCount = 1;
	}
	to_transfer[0].image = window->vk_objects.brickIndirectionTexture.outputImage;
	to_transfer[1].image = window->vk_objects.chunkSkipTexture.outputImage;
	if (totalSolid > 0) {
		to_transfer[2].image = window->vk_objects.brickAtlasTexture.outputImage;
	}

	VkDependencyInfo to_transfer_dep = {0};
	to_transfer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_transfer_dep.imageMemoryBarrierCount = imageCount;
	to_transfer_dep.pImageMemoryBarriers = to_transfer;
	vkCmdPipelineBarrier2(cmd, &to_transfer_dep);

	vkCmdCopyBufferToImage(cmd, slot_staging.buffer, window->vk_objects.brickIndirectionTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, totalBrickSlots, indirection_regions);
	vkCmdCopyBufferToImage(cmd, skip_staging.buffer, window->vk_objects.chunkSkipTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, skip_regions);
	if (totalSolid > 0) {
		vkCmdCopyBufferToImage(cmd, voxel_staging.buffer, window->vk_objects.brickAtlasTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, totalSolid, atlas_regions);
	}

	VkImageMemoryBarrier2 to_shader_read[3];
	for (uint32_t b = 0; b < imageCount; b++) {
		to_shader_read[b] = to_transfer[b];
		to_shader_read[b].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		to_shader_read[b].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_shader_read[b].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		to_shader_read[b].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		to_shader_read[b].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_shader_read[b].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VkDependencyInfo to_shader_dep = {0};
	to_shader_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_shader_dep.imageMemoryBarrierCount = imageCount;
	to_shader_dep.pImageMemoryBarriers = to_shader_read;
	vkCmdPipelineBarrier2(cmd, &to_shader_dep);

	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;
	vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(window->vk_objects.deviceQueue);

	vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);

	free(indirection_regions);
	free(skip_regions);
	free(solidCounts);
	destroyVkBuffer(window->vk_objects.device, &slot_staging);
	destroyVkBuffer(window->vk_objects.device, &skip_staging);
	if (totalSolid > 0) {
		free(atlas_regions);
		destroyVkBuffer(window->vk_objects.device, &voxel_staging);
	}
}

//loads whenever
void window_world_chunk_load(Window_t* window, World* wrld, Chunk** chunks, uint32_t count) {

	window_world_chunk_atlas_upload(window, wrld, chunks, count);
	window_world_chunk_occupancy_mask_upload(window, wrld, chunks, count);

}

void window_world_chunk_unload(Window_t* window, World* wrld, Chunk* chunk) {
	const uint32_t bricksPerChunkAxis = CHUNK_DIM / VOXEL_BRICK_SIZE; // 4
	const uint32_t bricksPerChunk = bricksPerChunkAxis * bricksPerChunkAxis * bricksPerChunkAxis; // 64

	VkDeviceSize slot_staging_size = (VkDeviceSize)bricksPerChunk * sizeof(uint32_t);
	Vk_Buffer_t slot_staging = createHostVisibleBuffer(window, slot_staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	uint32_t* slot_dst = (uint32_t*)slot_staging.mapped;
	for (uint32_t b = 0; b < bricksPerChunk; b++) { slot_dst[b] = EMPTY_SLOT; }

	VkBufferImageCopy* regions = malloc(sizeof(VkBufferImageCopy) * bricksPerChunk);
	for (uint32_t b = 0; b < bricksPerChunk; b++) {
		uint32_t bx = b % bricksPerChunkAxis;
		uint32_t by = (b / bricksPerChunkAxis) % bricksPerChunkAxis;
		uint32_t bz = b / (bricksPerChunkAxis * bricksPerChunkAxis);

		uint32_t worldBrickX = (uint32_t)wrapChunkCoord(chunk->coord.x) * bricksPerChunkAxis + bx;
		uint32_t worldBrickY = (uint32_t)wrapChunkCoord(chunk->coord.y) * bricksPerChunkAxis + by;
		uint32_t worldBrickZ = (uint32_t)wrapChunkCoord(chunk->coord.z) * bricksPerChunkAxis + bz;

		regions[b] = (VkBufferImageCopy){0};
		regions[b].bufferOffset = (VkDeviceSize)b * sizeof(uint32_t);
		regions[b].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		regions[b].imageSubresource.mipLevel = 0;
		regions[b].imageSubresource.baseArrayLayer = 0;
		regions[b].imageSubresource.layerCount = 1;
		regions[b].imageOffset.x = (int32_t)worldBrickX;
		regions[b].imageOffset.y = (int32_t)worldBrickY;
		regions[b].imageOffset.z = (int32_t)worldBrickZ;
		regions[b].imageExtent.width = 1;
		regions[b].imageExtent.height = 1;
		regions[b].imageExtent.depth = 1;
	}

	Vk_Buffer_t skip_staging = createHostVisibleBuffer(window, sizeof(uint8_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	*(uint8_t*)skip_staging.mapped = 0;

	VkBufferImageCopy skip_region = {0};
	skip_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	skip_region.imageSubresource.mipLevel = 0;
	skip_region.imageSubresource.baseArrayLayer = 0;
	skip_region.imageSubresource.layerCount = 1;
	skip_region.imageOffset.x = wrapChunkCoord(chunk->coord.x);
	skip_region.imageOffset.y = wrapChunkCoord(chunk->coord.y);
	skip_region.imageOffset.z = wrapChunkCoord(chunk->coord.z);
	skip_region.imageExtent.width = 1;
	skip_region.imageExtent.height = 1;
	skip_region.imageExtent.depth = 1;

	VkCommandBufferAllocateInfo cmd_alloc_info = {0};
	cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_alloc_info.commandPool = window->vk_objects.frameResources[0].commandPool;
	cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(window->vk_objects.device, &cmd_alloc_info, &cmd);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin_info);

	VkImageMemoryBarrier2 to_transfer[2] = {0};
	for (int b = 0; b < 2; b++) {
		to_transfer[b].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		to_transfer[b].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		to_transfer[b].srcAccessMask = 0;
		to_transfer[b].dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		to_transfer[b].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_transfer[b].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // already initialized by the time anything can unload
		to_transfer[b].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_transfer[b].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer[b].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_transfer[b].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		to_transfer[b].subresourceRange.levelCount = 1;
		to_transfer[b].subresourceRange.layerCount = 1;
	}
	to_transfer[0].image = window->vk_objects.brickIndirectionTexture.outputImage;
	to_transfer[1].image = window->vk_objects.chunkSkipTexture.outputImage;

	VkDependencyInfo to_transfer_dep = {0};
	to_transfer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_transfer_dep.imageMemoryBarrierCount = 2;
	to_transfer_dep.pImageMemoryBarriers = to_transfer;
	vkCmdPipelineBarrier2(cmd, &to_transfer_dep);

	vkCmdCopyBufferToImage(cmd, slot_staging.buffer, window->vk_objects.brickIndirectionTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bricksPerChunk, regions);
	vkCmdCopyBufferToImage(cmd, skip_staging.buffer, window->vk_objects.chunkSkipTexture.outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &skip_region);

	VkImageMemoryBarrier2 to_shader_read[2];
	for (int b = 0; b < 2; b++) {
		to_shader_read[b] = to_transfer[b];
		to_shader_read[b].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		to_shader_read[b].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_shader_read[b].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		to_shader_read[b].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		to_shader_read[b].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_shader_read[b].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VkDependencyInfo to_shader_dep = {0};
	to_shader_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	to_shader_dep.imageMemoryBarrierCount = 2;
	to_shader_dep.pImageMemoryBarriers = to_shader_read;
	vkCmdPipelineBarrier2(cmd, &to_shader_dep);

	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;
	vkQueueSubmit(window->vk_objects.deviceQueue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(window->vk_objects.deviceQueue);

	vkFreeCommandBuffers(window->vk_objects.device, window->vk_objects.frameResources[0].commandPool, 1, &cmd);

	free(regions);
	destroyVkBuffer(window->vk_objects.device, &slot_staging);
	destroyVkBuffer(window->vk_objects.device, &skip_staging);
}

void window_world_chunk_streaming(Window_t* window, World* wrld) {
	uint32_t chunks_per_frame = CHUNK_STREAM_BATCH_SIZE;
	iVector_t loaded_coords[CHUNK_STREAM_BATCH_SIZE];
	uint32_t loaded_count = 0;

	for (uint32_t i = 0; i < chunks_per_frame; i++) {
		if (!q_emtpy(&wrld->chunk_load_queue)) {
			iVector_t chunkCoord = q_pop(&wrld->chunk_load_queue);
			hmdel(wrld->pending_map, chunkCoord);
			int idx = world_chunk_load(wrld, chunkCoord);
			if (idx != -1) {

				int collisionIdx = world_chunk_find_collision(wrld, chunkCoord, idx);
				if (collisionIdx != -1) {
					Chunk* collidingChunk = &wrld->chunk_map[collisionIdx].value;
					window_world_chunk_unload(window, wrld, collidingChunk);
					world_chunk_unload_by_index(wrld, collisionIdx);
				}

				loaded_coords[loaded_count++] = chunkCoord;
			}
		} else {break;}
	}

	if (loaded_count > 0) {
		// resolved after the loop above, since earlier hmput/hmdel calls can reallocate/reshuffle chunk_map
		Chunk* loaded_chunks[CHUNK_STREAM_BATCH_SIZE];
		for (uint32_t i = 0; i < loaded_count; i++) {
			int idx = hmgeti(wrld->chunk_map, loaded_coords[i]);
			loaded_chunks[i] = &wrld->chunk_map[idx].value;
		}
		window_world_chunk_load(window, wrld, loaded_chunks, loaded_count);
	}

	for (uint32_t i = 0; i < chunks_per_frame; i++) {
		if (q_emtpy(&wrld->chunk_rmf_queue)) {return;}
		iVector_t chunkCoord = q_pop(&wrld->chunk_rmf_queue);
		hmdel(wrld->pending_map, chunkCoord);
		int unloadIdx = hmgeti(wrld->chunk_map, chunkCoord);
		if (unloadIdx != -1) {
			Chunk* chunk = &wrld->chunk_map[unloadIdx].value;
			window_world_chunk_unload(window, wrld, chunk); //clear GPU first
			world_chunk_unload_by_index(wrld, unloadIdx);
		} else {break;}
	}
}



/*
void window_world_buffer_load(Window_t* window, World* wrld) {
	VkDeviceSize world_grid_size = (VkDeviceSize)VOXEL_GRID_DIM * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
	const uint8_t* voxels = wrld->voxels;

	uploadWorldTextureData(window, voxels);

	// fine occupancy bits: 
	VkDeviceSize occ_size = world_grid_size / 8;
	uint8_t* occ = calloc(occ_size, 1);
	for (VkDeviceSize i = 0; i < world_grid_size; i++) {
		if (voxels[i] != 0) {
			occ[i >> 3] |= (uint8_t)(1u << (i & 7u));
		}
	}
	uploadBufferData(window, window->vk_objects.worldGridOccBuffer.buffer, occ, occ_size);

	// coarse block mask
	uint32_t blocks_per_axis = VOXEL_GRID_DIM / VOXEL_MASK_BLOCK_SIZE;
	VkDeviceSize mask_size = ((VkDeviceSize)blocks_per_axis * blocks_per_axis * blocks_per_axis + 7) >> 3;
	uint8_t* mask = calloc(mask_size, 1);
	for (uint32_t bz = 0; bz < blocks_per_axis; bz++) {
		for (uint32_t by = 0; by < blocks_per_axis; by++) {
			for (uint32_t bx = 0; bx < blocks_per_axis; bx++) {
				bool solid = false;
				for (uint32_t vz = 0; vz < VOXEL_MASK_BLOCK_SIZE && !solid; vz++) {
					for (uint32_t vy = 0; vy < VOXEL_MASK_BLOCK_SIZE && !solid; vy++) {
						for (uint32_t vx = 0; vx < VOXEL_MASK_BLOCK_SIZE; vx++) {
							uint32_t x = bx * VOXEL_MASK_BLOCK_SIZE + vx;
							uint32_t y = by * VOXEL_MASK_BLOCK_SIZE + vy;
							uint32_t z = bz * VOXEL_MASK_BLOCK_SIZE + vz;
							uint32_t idx = x + y * VOXEL_GRID_DIM + z * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
							if (voxels[idx] != 0) { solid = true; break; }
						}
					}
				}
				if (solid) {
					uint32_t blockIndex = bx + by * blocks_per_axis + bz * blocks_per_axis * blocks_per_axis;
					mask[blockIndex >> 3] |= (uint8_t)(1u << (blockIndex & 7u));
				}
			}
		}
	}
	uploadBufferData(window, window->vk_objects.worldGridMaskBuffer.buffer, mask, mask_size);

	free(occ);
	free(mask);
}*/

void window_material_buffer_load(Window_t* window, Material* mat_list) {

	Material materials[MAX_MATERIALS] = {0};

	for (uint32_t i = 0; i <MAX_MATERIALS; i++) {
		materials[i] = mat_list[i];
		for (uint32_t k = 0; k < 4; k++) {
			materials[i].emissionColor[k] *= materials[i].emissionStrength; //precomp this to gain an fps maybe
		}
	}

	uploadBufferData(window, window->vk_objects.materialProperitesBuffer.buffer, materials, sizeof(materials));
}


void window_model_buffer_load(Window_t* window, Model_t* model_list) {

	GpuModel gpu_models[MAX_MODELS] = {0};

	VkDeviceSize total_voxels_size = 0;
	for (uint32_t i = 0; i < MAX_MODELS; i++) {
		printf("model[%u]: dims=(%u,%u,%u) size=%u voxels=%p\n", i,
			model_list[i].dimensions[0], model_list[i].dimensions[1], model_list[i].dimensions[2],
			model_list[i].size, (void*)model_list[i].voxels);
		total_voxels_size += model_list[i].size;
	}
	printf("window_model_buffer_load: total_voxels_size=%llu bytes\n", (unsigned long long)total_voxels_size);
	if (total_voxels_size == 0) { total_voxels_size = 1; }

	uint8_t* packed_voxels = calloc((size_t)total_voxels_size, 1);
	if (packed_voxels == NULL) {
		printf("failed to allocate packed model voxel buffer (%llu bytes)\n", (unsigned long long)total_voxels_size);
		return;
	}
	uint32_t offset = 0;

	for (uint32_t i = 0; i < MAX_MODELS; i++) {
		Model_t* model = &model_list[i];

		gpu_models[i].voxelOffset = offset;
		gpu_models[i].axes = (uint32_t)model->up_axis | ((uint32_t)model->cardinal_axis << 8);
		gpu_models[i].dimensions[0] = model->dimensions[0];
		gpu_models[i].dimensions[1] = model->dimensions[1];
		gpu_models[i].dimensions[2] = model->dimensions[2];
		gpu_models[i].size = model->size;

		if (model->size > 0 && model->voxels != NULL) {
			memcpy(packed_voxels + offset, model->voxels, model->size);
		}
		offset += model->size;
	}

	uploadBufferData(window, window->vk_objects.modelBuffer.buffer, gpu_models, sizeof(gpu_models));
	uploadBufferData(window, window->vk_objects.modelVoxelBuffer.buffer, packed_voxels, total_voxels_size);

	free(packed_voxels);
}


void createComputeDescriptorSet(Window_t* window) {

	VkDescriptorPoolSize pool_sizes[4] = {0};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[0].descriptorCount = 4 * MAX_FRAMES_IN_FLIGHT; // materials + model table + model voxels + entity data, per set
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[1].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT; // camera, per set
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[2].descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT; // output image + shared accum image, per set
	pool_sizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	pool_sizes[3].descriptorCount = 4 * MAX_FRAMES_IN_FLIGHT; // brick atlas + brick indirection + voxel occupancy + chunk skip textures, per set

	VkDescriptorPoolCreateInfo pool_info = {0};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
	pool_info.poolSizeCount = 4;
	pool_info.pPoolSizes = pool_sizes;

	if (vkCreateDescriptorPool(window->vk_objects.device, &pool_info, NULL, &window->vk_objects.computeDescriptorPool) != VK_SUCCESS) {
		printf("Unable to create the compute descriptor pool\n");
		return;
	}

	//one descriptor set per frame-in-flight, so each frame can point at its own camera data buffer
	VkDescriptorSetLayout set_layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		set_layouts[i] = window->vk_objects.computeDescriptorSetLayout;
	}

	VkDescriptorSetAllocateInfo set_alloc_info = {0};
	set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc_info.descriptorPool = window->vk_objects.computeDescriptorPool;
	set_alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
	set_alloc_info.pSetLayouts = set_layouts;

	if (vkAllocateDescriptorSets(window->vk_objects.device, &set_alloc_info, window->vk_objects.computeDescriptorSet) != VK_SUCCESS) {
		printf("Unable to allocate the compute descriptor set\n");
		return;
	}

	//static across every frame
	VkDescriptorImageInfo brick_atlas_info = {0};
	brick_atlas_info.imageView = window->vk_objects.brickAtlasTexture.outputImageView;
	brick_atlas_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo brick_indirection_info = {0};
	brick_indirection_info.imageView = window->vk_objects.brickIndirectionTexture.outputImageView;
	brick_indirection_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo voxel_occupancy_info = {0};
	voxel_occupancy_info.imageView = window->vk_objects.voxelOccupancyTexture.outputImageView;
	voxel_occupancy_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo chunk_skip_info = {0};
	chunk_skip_info.imageView = window->vk_objects.chunkSkipTexture.outputImageView;
	chunk_skip_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorBufferInfo material_properties_info = {0};
	material_properties_info.buffer = window->vk_objects.materialProperitesBuffer.buffer;
	material_properties_info.offset = 0;
	material_properties_info.range = VK_WHOLE_SIZE;

	VkDescriptorBufferInfo model_buffer_info = {0};
	model_buffer_info.buffer = window->vk_objects.modelBuffer.buffer;
	model_buffer_info.offset = 0;
	model_buffer_info.range = VK_WHOLE_SIZE;

	VkDescriptorBufferInfo model_voxel_buffer_info = {0};
	model_voxel_buffer_info.buffer = window->vk_objects.modelVoxelBuffer.buffer;
	model_voxel_buffer_info.offset = 0;
	model_voxel_buffer_info.range = VK_WHOLE_SIZE;

	VkDescriptorBufferInfo camera_data_infos[MAX_FRAMES_IN_FLIGHT] = {0};
	VkDescriptorBufferInfo entity_data_infos[MAX_FRAMES_IN_FLIGHT] = {0};
	VkDescriptorImageInfo image_infos[MAX_FRAMES_IN_FLIGHT] = {0};
	VkWriteDescriptorSet writes[MAX_FRAMES_IN_FLIGHT * 11] = {0};

	//shared across every set
	VkDescriptorImageInfo accum_image_info = {0};
	accum_image_info.imageView = window->vk_objects.accumImageRes.outputImageView;
	accum_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		camera_data_infos[i].buffer = window->vk_objects.cameraDataBuffer[i].buffer;
		camera_data_infos[i].offset = 0;
		camera_data_infos[i].range = VK_WHOLE_SIZE;

		entity_data_infos[i].buffer = window->vk_objects.entityDataBuffer[i].buffer;
		entity_data_infos[i].offset = 0;
		entity_data_infos[i].range = VK_WHOLE_SIZE;

		image_infos[i].imageView = window->vk_objects.outputImageRes[i].outputImageView;
		image_infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet *frame_writes = &writes[i * 11];

		frame_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[0].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[0].dstBinding = 1;
		frame_writes[0].descriptorCount = 1;
		frame_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		frame_writes[0].pImageInfo = &brick_atlas_info;

		frame_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[1].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[1].dstBinding = 3;
		frame_writes[1].descriptorCount = 1;
		frame_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		frame_writes[1].pBufferInfo = &camera_data_infos[i];

		frame_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[2].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[2].dstBinding = 4;
		frame_writes[2].descriptorCount = 1;
		frame_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		frame_writes[2].pImageInfo = &image_infos[i];

		frame_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[3].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[3].dstBinding = 2;
		frame_writes[3].descriptorCount = 1;
		frame_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		frame_writes[3].pBufferInfo = &material_properties_info;

		frame_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[4].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[4].dstBinding = 5;
		frame_writes[4].descriptorCount = 1;
		frame_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		frame_writes[4].pImageInfo = &accum_image_info;

		frame_writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[5].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[5].dstBinding = 6;
		frame_writes[5].descriptorCount = 1;
		frame_writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		frame_writes[5].pImageInfo = &brick_indirection_info;

		frame_writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[6].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[6].dstBinding = 7;
		frame_writes[6].descriptorCount = 1;
		frame_writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		frame_writes[6].pImageInfo = &voxel_occupancy_info;

		frame_writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[7].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[7].dstBinding = 8;
		frame_writes[7].descriptorCount = 1;
		frame_writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		frame_writes[7].pBufferInfo = &entity_data_infos[i];

		frame_writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[8].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[8].dstBinding = 9;
		frame_writes[8].descriptorCount = 1;
		frame_writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		frame_writes[8].pBufferInfo = &model_buffer_info;

		frame_writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[9].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[9].dstBinding = 10;
		frame_writes[9].descriptorCount = 1;
		frame_writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		frame_writes[9].pBufferInfo = &model_voxel_buffer_info;

		frame_writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[10].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[10].dstBinding = 11;
		frame_writes[10].descriptorCount = 1;
		frame_writes[10].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		frame_writes[10].pImageInfo = &chunk_skip_info;
	}

	vkUpdateDescriptorSets(window->vk_objects.device, MAX_FRAMES_IN_FLIGHT * 11, writes, 0, NULL);

	printf("compute descriptor set created.\n");
}

void createSyncResources(Window_t* window) {

	VkSemaphoreTypeCreateInfo semaphore_type_info;
	semaphore_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	semaphore_type_info.pNext = NULL;
	semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	semaphore_type_info.initialValue = 0;

	VkSemaphoreCreateInfo semaphore_create_info;
	semaphore_create_info.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphore_create_info.pNext=&semaphore_type_info;
	semaphore_create_info.flags=0;

	if (vkCreateSemaphore(window->vk_objects.device, &semaphore_create_info, NULL, &window->vk_objects.timelineSemaphore) != VK_SUCCESS) {
		printf("Unable to create the timeline semaphore\n");
		return;
	}

	if (vkCreateSemaphore(window->vk_objects.device, &semaphore_create_info, NULL, &window->vk_objects.computeTimelineSemaphore) != VK_SUCCESS) {
		printf("Unable to create the compute timeline semaphore\n");
		return;
	}

	//per-frame image-acquire semaphores
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

		VkSemaphoreCreateInfo semaphoreInfo;
		semaphoreInfo.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreInfo.pNext=NULL;
		semaphoreInfo.flags=0;

		FrameResources_t *res = &window->vk_objects.frameResources[i];

		if (vkCreateSemaphore(window->vk_objects.device, &semaphoreInfo, NULL, &res->imageAcquiredSemaphore) != VK_SUCCESS) {
			printf("Error creating the per-frame image-acquire semaphore\n");
			return;
		}
	}

}


void createCommandBuffers(Window_t* window) {

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		FrameResources_t *res = &window->vk_objects.frameResources[i];

		VkCommandPoolCreateInfo cmd_pool_create_info;
		cmd_pool_create_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmd_pool_create_info.pNext=NULL;
		cmd_pool_create_info.flags=0;
		cmd_pool_create_info.queueFamilyIndex=window->vk_objects.gfxQueueFamIdx;

		if (vkCreateCommandPool(window->vk_objects.device,&cmd_pool_create_info,NULL,&res->commandPool) != VK_SUCCESS) {
			printf("Unable to create command buffer pool\n");
			return;
		}

		VkCommandBufferAllocateInfo cmd_buffer_alloc_info;
		cmd_buffer_alloc_info.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd_buffer_alloc_info.pNext=NULL;
		cmd_buffer_alloc_info.commandPool=res->commandPool;
		cmd_buffer_alloc_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_buffer_alloc_info.commandBufferCount=1;

		if (vkAllocateCommandBuffers(window->vk_objects.device, &cmd_buffer_alloc_info, &res->commandBuffer) != VK_SUCCESS) {
			printf("Unable to allocate command buffer\n");
			return;
		}

		printf("command pool and buffer %u created.\n", i);
	}
}

