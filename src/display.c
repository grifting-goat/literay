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

    printf("Device created!\n");
}
