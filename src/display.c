#include "display.h"

#include "stb_image.h"
#include <string.h>


#define ICON_32_PATH "./res/icon/icon_32.png"
#define ICON_64_PATH "./res/icon/icon_64.png"
#define ICON_128_PATH "./res/icon/icon_128.png"


void appInfoCreate(Window_t* window);
void vulkanInstanceCreate(Window_t* window);
void glfw_app_window_create(Window_t* window);


void surfaceCreate(Window_t* window);

void createDevice(Window_t* window);

void chooseBestPhysicalDevice(Window_t* window);

void createOutputImage(Window_t* window);

uint32_t findMemoryTypeIndex(Window_t* window, uint32_t typeBits, VkMemoryPropertyFlags required);

void createSwapchain(Window_t* window);

void createComputePipeline(Window_t* window);

void createComputeShader(Window_t* window);

Vk_Buffer_t createHostVisibleBuffer(Window_t* window, VkDeviceSize size, VkBufferUsageFlags usage);

void createComputeBuffers(Window_t* window);

void createComputeDescriptorSet(Window_t* window);

void createCommandBuffers(Window_t* window);

void createSyncResources(Window_t* window);



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
	createSwapchain(window);

	createComputeShader(window);
	createComputePipeline(window);
	createComputeBuffers(window);
	createComputeDescriptorSet(window);
	
	createSyncResources(window);
	createCommandBuffers(window);
}

void window_render(Window_t* window) {
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
	to_general_barrier.image=window->vk_objects.outputImageRes.outputImage;
	to_general_barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
	to_general_barrier.subresourceRange.baseMipLevel=0;
	to_general_barrier.subresourceRange.levelCount=1;
	to_general_barrier.subresourceRange.baseArrayLayer=0;
	to_general_barrier.subresourceRange.layerCount=1;

	VkDependencyInfo pre_dispatch_dep_info={0};
	pre_dispatch_dep_info.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	pre_dispatch_dep_info.imageMemoryBarrierCount=1;
	pre_dispatch_dep_info.pImageMemoryBarriers=&to_general_barrier;
	vkCmdPipelineBarrier2(res->commandBuffer, &pre_dispatch_dep_info);

	vkCmdBindPipeline(res->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, window->vk_objects.computePipeline.handle);
	vkCmdBindDescriptorSets(res->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, window->vk_objects.computePipeline.layout, 0, 1, &window->vk_objects.computeDescriptorSet[frame_res_index], 0, NULL);

	PushConstants push_constants = {0};
	push_constants._screen_size[0] = (int)window->vk_objects.swapchain_data.swapchainWidth;
	push_constants._screen_size[1] = (int)window->vk_objects.swapchain_data.swapchainHeight;
	push_constants.pixel_count = push_constants._screen_size[0] * push_constants._screen_size[1];
	push_constants._voxel_grid_size[0] = (int)VOXEL_GRID_DIM;
	push_constants._voxel_grid_size[1] = (int)VOXEL_GRID_DIM;
	push_constants._voxel_grid_size[2] = (int)VOXEL_GRID_DIM;
	push_constants.voxelCount = VOXEL_GRID_DIM * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
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
	pre_blit_barriers[0].image=window->vk_objects.outputImageRes.outputImage;
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
		window->vk_objects.outputImageRes.outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

	VkSemaphoreSubmitInfo wait_semaphore_info={0};
	wait_semaphore_info.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait_semaphore_info.semaphore=res->imageAcquiredSemaphore;
	wait_semaphore_info.stageMask=VK_PIPELINE_STAGE_2_BLIT_BIT;

	VkSemaphoreSubmitInfo signal_semaphore_infos[2];
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

	VkCommandBufferSubmitInfo cmd_submit_info={0};
	cmd_submit_info.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmd_submit_info.commandBuffer=res->commandBuffer;

	VkSubmitInfo2 submit_info={0};
	submit_info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit_info.waitSemaphoreInfoCount=1;
	submit_info.pWaitSemaphoreInfos=&wait_semaphore_info;
	submit_info.commandBufferInfoCount=1;
	submit_info.pCommandBufferInfos=&cmd_submit_info;
	submit_info.signalSemaphoreInfoCount=2;
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

	if (vkCreateImage(window->vk_objects.device, &image_create_info, NULL, &window->vk_objects.outputImageRes.outputImage) != VK_SUCCESS) {
		printf("failed to create output image.\n");
		return;
	}

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(window->vk_objects.device, window->vk_objects.outputImageRes.outputImage, &mem_reqs);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = findMemoryTypeIndex(window, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(window->vk_objects.device, &alloc_info, NULL, &window->vk_objects.outputImageRes.outputImageMemory) != VK_SUCCESS) {
		printf("failed to allocate output image memory.\n");
		return;
	}
	vkBindImageMemory(window->vk_objects.device, window->vk_objects.outputImageRes.outputImage, window->vk_objects.outputImageRes.outputImageMemory, 0);

	VkImageViewCreateInfo view_create_info = {0};
	view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_create_info.image = window->vk_objects.outputImageRes.outputImage;
	view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_create_info.format = OUTPUT_IMAGE_FORMAT;
	view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_create_info.subresourceRange.baseMipLevel = 0;
	view_create_info.subresourceRange.levelCount = 1;
	view_create_info.subresourceRange.baseArrayLayer = 0;
	view_create_info.subresourceRange.layerCount = 1;

	if (vkCreateImageView(window->vk_objects.device, &view_create_info, NULL, &window->vk_objects.outputImageRes.outputImageView) != VK_SUCCESS) {
		printf("failed to create output image view.\n");
		return;
	}

	printf("output image created.\n");
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

	VkDescriptorSetLayoutBinding bindings[4] = {0};
	bindings[0].binding = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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

	VkDescriptorSetLayoutCreateInfo set_layout_info = {0};
	set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout_info.bindingCount = 4;
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

//worldGridBuffer/materialProperitesBuffer aren't sampled by the shader yet, so they're just zeroed for now
void createComputeBuffers(Window_t* window) {
	VkDeviceSize world_grid_size = (VkDeviceSize)VOXEL_GRID_DIM * VOXEL_GRID_DIM * VOXEL_GRID_DIM;
	window->vk_objects.worldGridBuffer = createHostVisibleBuffer(window, world_grid_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	VkDeviceSize materials_size = (VkDeviceSize)MAX_MATERIALS * sizeof(Material);
	window->vk_objects.materialProperitesBuffer = createHostVisibleBuffer(window, materials_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		window->vk_objects.cameraDataBuffer[i] = createHostVisibleBuffer(window, sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	}

	printf("compute buffers created.\n");
}


void createComputeDescriptorSet(Window_t* window) {

	VkDescriptorPoolSize pool_sizes[3] = {0};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[0].descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[1].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[2].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo pool_info = {0};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
	pool_info.poolSizeCount = 3;
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

	//static across every frame: world grid, material properties, and the single output image
	VkDescriptorBufferInfo world_grid_info = {0};
	world_grid_info.buffer = window->vk_objects.worldGridBuffer.buffer;
	world_grid_info.offset = 0;
	world_grid_info.range = VK_WHOLE_SIZE;

	VkDescriptorImageInfo image_info = {0};
	image_info.imageView = window->vk_objects.outputImageRes.outputImageView;
	image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorBufferInfo material_properties_info = {0};
	material_properties_info.buffer = window->vk_objects.materialProperitesBuffer.buffer;
	material_properties_info.offset = 0;
	material_properties_info.range = VK_WHOLE_SIZE;

	//one camera data buffer per frame-in-flight, so writing next frame's camera data can't race the GPU still reading last frame's
	VkDescriptorBufferInfo camera_data_infos[MAX_FRAMES_IN_FLIGHT] = {0};
	VkWriteDescriptorSet writes[MAX_FRAMES_IN_FLIGHT * 4] = {0};

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		camera_data_infos[i].buffer = window->vk_objects.cameraDataBuffer[i].buffer;
		camera_data_infos[i].offset = 0;
		camera_data_infos[i].range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet *frame_writes = &writes[i * 4];

		frame_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[0].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[0].dstBinding = 1;
		frame_writes[0].descriptorCount = 1;
		frame_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		frame_writes[0].pBufferInfo = &world_grid_info;

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
		frame_writes[2].pImageInfo = &image_info;

		frame_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		frame_writes[3].dstSet = window->vk_objects.computeDescriptorSet[i];
		frame_writes[3].dstBinding = 2;
		frame_writes[3].descriptorCount = 1;
		frame_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		frame_writes[3].pBufferInfo = &material_properties_info;
	}

	vkUpdateDescriptorSets(window->vk_objects.device, MAX_FRAMES_IN_FLIGHT * 4, writes, 0, NULL);

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

