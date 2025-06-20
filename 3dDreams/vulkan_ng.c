#include "arena.h"
#include "common.h"
#include "graphics.h"

#include <volk.c>

#include "vulkan_ng.h"

#include "hash.c"
#include "win32_file_io.c"
#include "vulkan_spirv_loader.c"
#include "meshlet.c"

#pragma comment(lib,	"vulkan-1.lib")

static void obj_file_read(void *ctx, const char *filename, int is_mtl, const char *obj_filename, char **buf, size_t *len)
{
   char shader_path[MAX_PATH];

   obj_user_ctx* user_data = (obj_user_ctx*)ctx;

   arena project_dir = vk_project_directory(&user_data->scratch);

   wsprintf(shader_path, project_dir.beg, array_count(shader_path));
   wsprintf(shader_path, "%s\\assets\\objs\\%s", project_dir.beg, filename);

   arena file_read = win32_file_read(&user_data->scratch, shader_path);

   *len = scratch_left(file_read);
   *buf = file_read.beg;
}

static void vk_shader_load(VkDevice logical_device, arena scratch, const char* shader_name, vk_shader_modules* shader_modules)
{
   assert(vk_valid_handle(logical_device));

   arena project_dir = vk_project_directory(&scratch);

   size shader_len = strlen(shader_name);
   assert(shader_len != 0u);

   VkShaderStageFlagBits shader_stage = 0;

   for(size i = 0; i < shader_len; ++i)
   {
      usize mesh_len = strlen("mesh.spv");
      if(strncmp(shader_name+i, "mesh.spv", mesh_len) == 0)
      {
         shader_stage = VK_SHADER_STAGE_MESH_BIT_EXT;
         break;
      }

      usize vert_len = strlen("vert.spv");
      if(strncmp(shader_name+i, "vert.spv", vert_len) == 0)
      {
         shader_stage = VK_SHADER_STAGE_VERTEX_BIT;
         break;
      }

      usize frag_len = strlen("frag.spv");
      if(strncmp(shader_name+i, "frag.spv", frag_len) == 0)
      {
         shader_stage = VK_SHADER_STAGE_FRAGMENT_BIT;
         break;
      }
   }

   VkShaderModule shader_module = vk_shader_spv_module_load(logical_device, &scratch, project_dir.beg, shader_name);

   switch(shader_stage)
   {
      case VK_SHADER_STAGE_MESH_BIT_EXT:
         shader_modules->ms = shader_module;
         break;
      case VK_SHADER_STAGE_VERTEX_BIT:
         shader_modules->vs = shader_module;
         break;
      case VK_SHADER_STAGE_FRAGMENT_BIT:
         shader_modules->fs = shader_module;
         break;
      default: break;
   }
}

static void spv_lookup(VkDevice logical_device, arena scratch, spv_hash_table* table, const char** shader_names, bool rtx_supported)
{
   assert(vk_valid_handle(logical_device));

   // TODO: make function for hash tables
   table->keys = push(&scratch, const char*, table->max_count);
   table->values = push(&scratch, vk_shader_modules, table->max_count);

   memset(table->values, 0, table->max_count * sizeof(vk_shader_modules));

   for(size i = 0; i < table->max_count; ++i)
      table->keys[i] = 0;

   // TODO: routine to iterate over hash values

   // Compile all the shaders
   for(const char** p = shader_names; p && *p; ++p)
   {
      usize shader_len = strlen(*p);
      const char* shader_name = *p;

      for(usize i = 0; i < shader_len; ++i)
      {
         // TODO: cleanup this mess
         if(rtx_supported)
         {
            if(strncmp(shader_name + i, "meshlet", strlen("meshlet")) == 0)
            {
               vk_shader_modules ms = spv_hash_lookup(table, "meshlet");
               vk_shader_load(logical_device, scratch, *p, &ms);
               spv_hash_insert(table, "meshlet", ms);
               break;
            }
         }
         else
         {
            if(strncmp(shader_name + i, "graphics", strlen("graphics")) == 0)
            {
               vk_shader_modules gm = spv_hash_lookup(table, "graphics");
               vk_shader_load(logical_device, scratch, *p, &gm);
               spv_hash_insert(table, "graphics", gm);
               break;
            }
         }
         if(strncmp(shader_name + i, "axis", strlen("axis")) == 0)
         {
            vk_shader_modules am = spv_hash_lookup(table, "axis");
            vk_shader_load(logical_device, scratch, *p, &am);
            spv_hash_insert(table, "axis", am);
            break;
         }
      }
   }
}

static void vk_buffer_upload(VkDevice device, VkQueue queue, VkCommandBuffer cmd_buffer, VkCommandPool cmd_pool, vk_buffer buffer, vk_buffer scratch, const void* data, VkDeviceSize size, bool rtx_supported)
{
   assert(data);
   assert(scratch.data && scratch.size >= size);
   memcpy(scratch.data, data, size);

   vk_assert(vkResetCommandPool(device, cmd_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vk_assert(vkBeginCommandBuffer(cmd_buffer, &buffer_begin_info));

   VkBufferCopy buffer_region = {0, 0, size};
   vkCmdCopyBuffer(cmd_buffer, scratch.handle, buffer.handle, 1, &buffer_region);

   VkBufferMemoryBarrier copy_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
   copy_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   copy_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier.buffer = buffer.handle;
   copy_barrier.size = size;
   copy_barrier.offset = 0;

if(rtx_supported)
   vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 1, &copy_barrier, 0, 0);
else
   vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 1, &copy_barrier, 0, 0);

   vk_assert(vkEndCommandBuffer(cmd_buffer));

   VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &cmd_buffer;

   vk_assert(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
   // instead of explicit memory sync between queue submissions we wait all gpu jobs to complete
   vk_assert(vkDeviceWaitIdle(device));
}

static void vk_buffers_upload(vk_context* context, vk_buffer scratch_buffer)
{
   tinyobj_shape_t* shapes = 0;
   tinyobj_material_t* materials = 0;
   tinyobj_attrib_t attrib = {};

   size_t shape_count = 0;
   size_t material_count = 0;

   tinyobj_attrib_init(&attrib);

   obj_user_ctx user_data = {};
   user_data.scratch = *context->storage;

   const char* filename = "buddha.obj";
   if(tinyobj_parse_obj(&attrib, &shapes, &shape_count, &materials, &material_count, filename, obj_file_read, &user_data, TINYOBJ_FLAG_TRIANGULATE) != TINYOBJ_SUCCESS)
      hw_message_box("Could not load .obj file");

   obj_load(context, *context->storage, &attrib, scratch_buffer);

   tinyobj_materials_free(materials, material_count);
   tinyobj_shapes_free(shapes, shape_count);
   tinyobj_attrib_free(&attrib);
}

static vk_buffer vk_buffer_create(VkDevice device, size size, VkPhysicalDeviceMemoryProperties memory_properties, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags)
{
   vk_buffer buffer = {};

   VkBufferCreateInfo create_info = {vk_info(BUFFER)};
   create_info.size = size;
   create_info.usage = usage;

   if(!vk_valid(vkCreateBuffer(device, &create_info, 0, &buffer.handle)))
      return (vk_buffer){};

   VkMemoryRequirements memory_reqs;
   vkGetBufferMemoryRequirements(device, buffer.handle, &memory_reqs);

   u32 memory_index = memory_properties.memoryTypeCount;
   u32 i = 0;

   while(i < memory_index)
   {
      if(((memory_reqs.memoryTypeBits & (1 << i)) && memory_properties.memoryTypes[i].propertyFlags == memory_flags))
         memory_index = i;

      ++i;
   }

   if(i == memory_properties.memoryTypeCount)
      return (vk_buffer){};

   VkMemoryAllocateInfo allocate_info = {vk_info_allocate(MEMORY)};
   allocate_info.allocationSize = memory_reqs.size;
   allocate_info.memoryTypeIndex = memory_index;

   VkDeviceMemory memory = 0;
   if(!vk_valid(vkAllocateMemory(device, &allocate_info, 0, &memory)))
      return (vk_buffer){};

   if(!vk_valid(vkBindBufferMemory(device, buffer.handle, memory, 0)))
      return (vk_buffer){};

   if(memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      if(!vk_valid(vkMapMemory(device, memory, 0, allocate_info.allocationSize, 0, &buffer.data)))
         return (vk_buffer) {};

   buffer.size = allocate_info.allocationSize;
   buffer.memory = memory;

   return buffer;
}

static void vk_buffer_destroy(VkDevice device, vk_buffer* buffer)
{
   vkFreeMemory(device, buffer->memory, 0);
   vkDestroyBuffer(device, buffer->handle, 0);
}

static VkResult vk_create_debugutils_messenger_ext(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger)
{
   PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
   if(!func)
      return VK_ERROR_EXTENSION_NOT_PRESENT;
   return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_flags,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* pUserData)
{
   debug_message("Validation layer message: %s\n", data->pMessage);
#ifdef vk_break_on_validation
   assert((type & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0);
#endif

   return VK_FALSE;
}

static VkFormat vk_swapchain_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_device));
   assert(vk_valid_handle(surface));

   VkSurfaceFormatKHR formats[MAX_VULKAN_OBJECT_COUNT] = {};
   u32 format_count = array_count(formats);
   if(!vk_valid(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats)))
      return VK_FORMAT_UNDEFINED;

   return formats[0].format;
}

static swapchain_surface_info vk_window_swapchain_surface_info(VkPhysicalDevice physical_device, u32 width, u32 height, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_device));
   assert(vk_valid_handle(surface));

   swapchain_surface_info result = {};

   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps)))
      return (swapchain_surface_info){0};

   // triple buffering
   if(surface_caps.minImageCount < 2)
      return (swapchain_surface_info){0};

   if(!implies(surface_caps.maxImageCount != 0, surface_caps.maxImageCount >= surface_caps.minImageCount + 1))
      return (swapchain_surface_info){0};

   u32 image_count = surface_caps.minImageCount + 1;

   result.image_width = width;
   result.image_height = height;
   result.image_count = image_count;
   result.format = vk_swapchain_format(physical_device, surface);
   result.surface = surface;

   return result;
}

static VkPhysicalDevice vk_physical_device_select(vk_context* context, arena scratch, VkInstance instance)
{
   assert(vk_valid_handle(instance));

   VkPhysicalDevice devs[MAX_VULKAN_OBJECT_COUNT] = {};
   VkPhysicalDevice fallback_gpu = 0;
   u32 dev_count = array_count(devs);
   vk_assert(vkEnumeratePhysicalDevices(instance, &dev_count, devs));

   for(u32 i = 0; i < dev_count; ++i)
   {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devs[i], &props);

      // match version
      if(props.apiVersion < VK_VERSION_1_1)
         continue;

      // timestamps
      if(!props.limits.timestampComputeAndGraphics)
         continue;

      // dedicated gpu
      if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      {
         context->time_period = props.limits.timestampPeriod;
         u32 extension_count = 0;
         vk_assert(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, 0));

         // TODO: log all the device extensions
         VkExtensionProperties* extensions = push(&scratch, VkExtensionProperties, extension_count);
         vk_assert(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, extensions));

#if _DEBUG
         debug_message("Supported device extensions:\n\n");
#endif
         for(u32 j = 0; j < extension_count; ++j)
         {
#if _DEBUG
            debug_message("%s\n", extensions[j].extensionName);
#endif
            if(strcmp(extensions[j].extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
            {
               context->rtx_supported = true;
               return devs[i];
            }
         }

         // use fallback as the first discrete gpu
         fallback_gpu = devs[i];
         break;
      }
   }

   return fallback_gpu;
}

static u32 vk_logical_device_select_family_index()
{
   // TODO: select the queue family
   return 0;
}

static u32 vk_mesh_shader_max_tasks(VkPhysicalDevice physical_device)
{
   assert(vk_valid_handle(physical_device));

   VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_props = {};
   mesh_shader_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;

   VkPhysicalDeviceProperties2 device_props2 = {};
   device_props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
   device_props2.pNext = &mesh_shader_props;

   vkGetPhysicalDeviceProperties2(physical_device, &device_props2);

   u32 max_mesh_tasks = mesh_shader_props.maxMeshWorkGroupInvocations;

   return max_mesh_tasks;
}

static VkDevice vk_logical_device_create(VkPhysicalDevice physical_device, arena scratch, u32 queue_family_index, bool rtx_supported)
{
   assert(vk_valid_handle(physical_device));
   f32 queue_prio = 1.0f;

   VkDeviceQueueCreateInfo queue_info = {vk_info(DEVICE_QUEUE)};
   queue_info.queueFamilyIndex = queue_family_index; // TODO: query the right queue family
   queue_info.queueCount = 1;
   queue_info.pQueuePriorities = &queue_prio;

   VkDeviceCreateInfo ldev_info = {vk_info(DEVICE)};

   // TODO: use an array with arena here
   const char** p = push(&scratch, const char*);

   const char** dev_ext_names = p;

   *p = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

   p = push(&scratch, const char*);
   *p = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;

   p = push(&scratch, const char*);
   *p = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;

   p = push(&scratch, const char*);
   *p = VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME;

   if(rtx_supported)
   {
      p = push(&scratch, const char*);
      *p = VK_EXT_MESH_SHADER_EXTENSION_NAME;

      p = push(&scratch, const char*);
      *p = VK_KHR_8BIT_STORAGE_EXTENSION_NAME;
   }

   VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

   VkPhysicalDeviceVulkan12Features vk12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
   vk12.storageBuffer8BitAccess = true;
   vk12.uniformAndStorageBuffer8BitAccess = true;
   vk12.storagePushConstant8 = true;

   VkPhysicalDeviceFragmentShadingRateFeaturesKHR frag_shading_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
   frag_shading_features.primitiveFragmentShadingRate = VK_TRUE;

   // TODO: Dont expose if no rtx was supported
   VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

   if(rtx_supported)
   {
      mesh_shader_features.meshShader = true;
      mesh_shader_features.taskShader = true;
      mesh_shader_features.multiviewMeshShader = true;
      mesh_shader_features.pNext = &frag_shading_features;
   }

   VkPhysicalDeviceMultiviewFeatures multiview = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
   multiview.multiview = true;

   if(rtx_supported)
      multiview.pNext = &mesh_shader_features;

   vk12.pNext = &multiview;
   features2.pNext = &vk12;

   vkGetPhysicalDeviceFeatures2(physical_device, &features2);

   features2.features.depthBounds = true;
   features2.features.wideLines = true;
   features2.features.fillModeNonSolid = true;
   features2.features.sampleRateShading = true;

   ldev_info.queueCreateInfoCount = 1;
   ldev_info.pQueueCreateInfos = &queue_info;
   //ldev_info.enabledExtensionCount = array_count(dev_ext_names);
   ldev_info.enabledExtensionCount = rtx_supported ? 6 : 4;   // TODO: use array_count(dev_ext_names) here
   ldev_info.ppEnabledExtensionNames = dev_ext_names;
   ldev_info.pNext = &features2;

   VkDevice logical_device;
   vk_assert(vkCreateDevice(physical_device, &ldev_info, 0, &logical_device));

   return logical_device;
}

static VkQueue vk_graphics_queue_create(VkDevice logical_device, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_device));

   VkQueue graphics_queue = 0;

   // TODO: Get the queue index
   vkGetDeviceQueue(logical_device, queue_family_index, 0, &graphics_queue);

   return graphics_queue;
}

static VkSemaphore vk_semaphore_create(VkDevice logical_device)
{
   assert(vk_valid_handle(logical_device));

   VkSemaphore sema = 0;

   VkSemaphoreCreateInfo sema_info = {vk_info(SEMAPHORE)};
   vk_assert(vkCreateSemaphore(logical_device, &sema_info, 0, &sema));

   return sema;
}

static VkSwapchainKHR vk_swapchain_create(VkDevice logical_device, swapchain_surface_info* surface_info, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(surface_info->surface));
   assert(vk_valid_format(surface_info->format));

   VkSwapchainKHR swapchain = 0;

   VkSwapchainCreateInfoKHR swapchain_info = {vk_info_khr(SWAPCHAIN)};
   swapchain_info.surface = surface_info->surface;
   swapchain_info.minImageCount = surface_info->image_count;
   swapchain_info.imageFormat = surface_info->format;
   swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
   swapchain_info.imageExtent.width = surface_info->image_width;
   swapchain_info.imageExtent.height = surface_info->image_height;
   swapchain_info.imageArrayLayers = 1;
   swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   swapchain_info.queueFamilyIndexCount = 1;
   swapchain_info.pQueueFamilyIndices = &queue_family_index;
   swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
   swapchain_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   swapchain_info.clipped = true;
   swapchain_info.oldSwapchain = surface_info->swapchain;

   vk_assert(vkCreateSwapchainKHR(logical_device, &swapchain_info, 0, &swapchain));

   return swapchain;
}

static swapchain_surface_info vk_swapchain_info_create(vk_context* context, u32 swapchain_width, u32 swapchain_height, u32 queue_family_index)
{
   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, context->surface, &surface_caps)))
      return (swapchain_surface_info) {};

   swapchain_surface_info swapchain_info = vk_window_swapchain_surface_info(context->physical_device, swapchain_width, swapchain_height, context->surface);

   VkExtent2D swapchain_extent = {swapchain_width, swapchain_height};

   if(surface_caps.currentExtent.width != UINT32_MAX)
      swapchain_extent = surface_caps.currentExtent;
   else
   {
      // surface allows to choose the size
      VkExtent2D min_extent = surface_caps.minImageExtent;
      VkExtent2D max_extent = surface_caps.maxImageExtent;
      swapchain_extent.width = clamp(swapchain_extent.width, min_extent.width, max_extent.width);
      swapchain_extent.height = clamp(swapchain_extent.height, min_extent.height, max_extent.height);
   }

   swapchain_info.swapchain = vk_swapchain_create(context->logical_device, &swapchain_info, queue_family_index);

   return swapchain_info;
}

static VkCommandBuffer vk_command_buffer_create(VkDevice logical_device, VkCommandPool pool)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(pool));

   VkCommandBuffer buffer = 0;
   VkCommandBufferAllocateInfo buffer_allocate_info = {vk_info_allocate(COMMAND_BUFFER)};

   buffer_allocate_info.commandBufferCount = 1;
   buffer_allocate_info.commandPool = pool;
   buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

   vk_assert(vkAllocateCommandBuffers(logical_device, &buffer_allocate_info, &buffer));

   return buffer;
}

static VkCommandPool vk_command_pool_create(VkDevice logical_device, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_device));

   VkCommandPool pool = 0;

   VkCommandPoolCreateInfo pool_info = {vk_info(COMMAND_POOL)};
   pool_info.queueFamilyIndex = queue_family_index;

   vk_assert(vkCreateCommandPool(logical_device, &pool_info, 0, &pool));

   return pool;
}

static VkImage vk_depth_image_create(VkDevice logical_device, VkPhysicalDevice physical_device, VkFormat format, VkExtent3D extent)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(physical_device));
   assert(vk_valid_format(format));

   VkImage result = 0;

   VkImageCreateInfo image_info = {};
   image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.imageType = VK_IMAGE_TYPE_2D; 
   image_info.extent = extent;
   image_info.mipLevels = 1;
   image_info.arrayLayers = 1;
   image_info.format = format;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.queueFamilyIndexCount = 0;
   image_info.pQueueFamilyIndices = 0;

   if(vkCreateImage(logical_device, &image_info, 0, &result) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   VkMemoryRequirements memory_requirements;
   vkGetImageMemoryRequirements(logical_device, result, &memory_requirements);

   VkMemoryAllocateInfo alloc_info = {};
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.allocationSize = memory_requirements.size;

   VkPhysicalDeviceMemoryProperties memory_properties;
   vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

   uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
   for(uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
      if((memory_requirements.memoryTypeBits & (1 << i)) &&
          (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      {
         memory_type_index = i;
         break;
      }

   if(memory_type_index == VK_MAX_MEMORY_TYPES)
      return VK_NULL_HANDLE;

   alloc_info.memoryTypeIndex = memory_type_index;

   VkDeviceMemory memory;
   if(vkAllocateMemory(logical_device, &alloc_info, 0, &memory) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   if(vkBindImageMemory(logical_device, result, memory, 0) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return result;
}


static VkRenderPass vk_renderpass_create(VkDevice logical_device, VkFormat color_format, VkFormat depth_format)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_format(color_format));

   VkRenderPass renderpass = 0;

   const u32 color_attachment_index = 0;
   const u32 depth_attachment_index = 1;

   VkAttachmentReference color_attachment_ref = {color_attachment_index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; 
   VkAttachmentReference depth_attachment_ref = {depth_attachment_index, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; 

   VkSubpassDescription subpass = {};
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_attachment_ref;

   subpass.pDepthStencilAttachment = &depth_attachment_ref;

   VkAttachmentDescription attachments[2] = {};

   attachments[color_attachment_index].format = color_format;
   attachments[color_attachment_index].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[color_attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[color_attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[color_attachment_index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[color_attachment_index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[color_attachment_index].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachments[color_attachment_index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   attachments[depth_attachment_index].format = VK_FORMAT_D32_SFLOAT;
   attachments[depth_attachment_index].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[depth_attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[depth_attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[depth_attachment_index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[depth_attachment_index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[depth_attachment_index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[depth_attachment_index].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


   VkRenderPassCreateInfo renderpass_info = {vk_info(RENDER_PASS)}; 
   renderpass_info.subpassCount = 1;
   renderpass_info.pSubpasses = &subpass;
   renderpass_info.attachmentCount = array_count(attachments);
   renderpass_info.pAttachments = attachments;

   vk_assert(vkCreateRenderPass(logical_device, &renderpass_info, 0, &renderpass));

   return renderpass;
}

static VkFramebuffer vk_framebuffer_create(VkDevice logical_device, VkRenderPass renderpass, swapchain_surface_info* surface_info, VkImageView* attachments, u32 attachment_count)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(renderpass));

   VkFramebuffer framebuffer = 0;

   VkFramebufferCreateInfo framebuffer_info = {vk_info(FRAMEBUFFER)};
   framebuffer_info.renderPass = renderpass;
   framebuffer_info.attachmentCount = attachment_count;
   framebuffer_info.pAttachments = attachments;
   framebuffer_info.width = surface_info->image_width;
   framebuffer_info.height = surface_info->image_height;
   framebuffer_info.layers = 1;

   vk_assert(vkCreateFramebuffer(logical_device, &framebuffer_info, 0, &framebuffer));

   return framebuffer;
}

static VkImageView vk_image_view_create(VkDevice logical_device, VkFormat format, VkImage image, VkImageAspectFlags aspect_mask)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_format(format));
   assert(vk_valid_handle(image));

   VkImageView image_view = 0;

   VkImageViewCreateInfo view_info = {vk_info(IMAGE_VIEW)};
   view_info.image = image;
   view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view_info.format = format;
   view_info.subresourceRange.aspectMask = aspect_mask;
   view_info.subresourceRange.layerCount = 1;
   view_info.subresourceRange.levelCount = 1;

   vk_assert(vkCreateImageView(logical_device, &view_info, 0, &image_view));

   return image_view;
}

VkImageMemoryBarrier vk_pipeline_barrier(VkImage image, VkImageAspectFlags aspect,
                                         VkAccessFlags src_access, VkAccessFlags dst_access, 
                                         VkImageLayout old_layout, VkImageLayout new_layout)
{
   VkImageMemoryBarrier result = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

   result.srcAccessMask = src_access;
   result.dstAccessMask = dst_access;

   result.oldLayout = old_layout;
   result.newLayout = new_layout;

   result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

   result.image = image;

   result.subresourceRange.aspectMask = aspect;
   result.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
   result.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

   return result;
}

static void vk_swapchain_destroy(vk_context* context)
{
   vkDeviceWaitIdle(context->logical_device);
   for(u32 i = 0; i < context->swapchain_info.image_count; ++i)
   {
      vkDestroyFramebuffer(context->logical_device, context->framebuffers[i], 0);
      vkDestroyImageView(context->logical_device, context->swapchain_info.image_views[i], 0);
      vkDestroyImageView(context->logical_device, context->swapchain_info.depth_views[i], 0);
   }

   vkDestroySwapchainKHR(context->logical_device, context->swapchain_info.swapchain, 0);
}

static bool vk_swapchain_update(vk_context* context)
{
   vk_assert(vkGetSwapchainImagesKHR(context->logical_device, context->swapchain_info.swapchain, &context->swapchain_info.image_count, context->swapchain_info.images));

   VkExtent3D depth_extent = {context->swapchain_info.image_width, context->swapchain_info.image_height, 1};

   for(u32 i = 0; i < context->swapchain_info.image_count; ++i)
   {
      context->swapchain_info.depths[i] = vk_depth_image_create(context->logical_device, context->physical_device, VK_FORMAT_D32_SFLOAT, depth_extent);

      context->swapchain_info.image_views[i] = vk_image_view_create(context->logical_device, context->swapchain_info.format, context->swapchain_info.images[i], VK_IMAGE_ASPECT_COLOR_BIT);
      context->swapchain_info.depth_views[i] = vk_image_view_create(context->logical_device, VK_FORMAT_D32_SFLOAT, context->swapchain_info.depths[i], VK_IMAGE_ASPECT_DEPTH_BIT);

      VkImageView attachments[2] = {context->swapchain_info.image_views[i], context->swapchain_info.depth_views[i]};

      context->framebuffers[i] = vk_framebuffer_create(context->logical_device, context->renderpass, &context->swapchain_info, attachments, array_count(attachments));
   }

   return true;
}

static void vk_resize(hw* hw, u32 width, u32 height)
{
   if(width == 0 || height == 0)
      return;

   u32 renderer_index = hw->renderer.renderer_index;
   assert(renderer_index < renderer_count);

   vk_context* context = hw->renderer.backends[renderer_index];

   mvp_transform mvp = {};
   const f32 ar = (f32)width / height;

   mvp.n = 0.01f;
   mvp.f = 10000.0f;
   mvp.ar = ar;

   mvp.projection = mat4_perspective(ar, 65.0f, mvp.n, mvp.f);
   hw->renderer.mvp = mvp;

   vkDeviceWaitIdle(context->logical_device);

   vk_swapchain_destroy(context);
   context->swapchain_info = vk_swapchain_info_create(context, width, height, context->queue_family_index);
   vk_swapchain_update(context);
}

static VkQueryPool vk_query_pool_create(VkDevice device, u32 pool_size)
{
   VkQueryPool result = 0;

   VkQueryPoolCreateInfo info = {vk_info(QUERY_POOL)};
   info.queryType = VK_QUERY_TYPE_TIMESTAMP;
   info.queryCount = pool_size;

   vk_assert(vkCreateQueryPool(device, &info, 0, &result));

   return result;
}

static void vk_present(hw* hw, vk_context* context, app_state* state)
{
   u32 image_index = 0;
   VkResult next_image_result = vkAcquireNextImageKHR(context->logical_device, context->swapchain_info.swapchain, UINT64_MAX, context->image_ready_semaphore, VK_NULL_HANDLE, &image_index);

   if(next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(hw, context->swapchain_info.image_width, context->swapchain_info.image_height);

   if(next_image_result != VK_SUBOPTIMAL_KHR && next_image_result != VK_SUCCESS)
      return;

   vk_assert(vkResetCommandPool(context->logical_device, context->command_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   VkRenderPassBeginInfo renderpass_info = {vk_info_begin(RENDER_PASS)};
   renderpass_info.renderPass = context->renderpass;
   renderpass_info.framebuffer = context->framebuffers[image_index];
   renderpass_info.renderArea.extent = (VkExtent2D)
   {context->swapchain_info.image_width, context->swapchain_info.image_height};

   VkCommandBuffer command_buffer = context->command_buffer;
   // begin command buffer

   vk_assert(vkBeginCommandBuffer(command_buffer, &buffer_begin_info));

   vkCmdResetQueryPool(context->command_buffer, context->query_pool, 0, context->query_pool_size);
   vkCmdWriteTimestamp(context->command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query_pool, 0);

   const f32 ar = (f32)context->swapchain_info.image_width / context->swapchain_info.image_height;

   mvp_transform mvp = hw->renderer.mvp;
   assert(mvp.n > 0.0f);
   assert(mvp.ar != 0.0f);

   // world space origin
   vec3 eye = state->camera.pos;
   vec3 dir = state->camera.dir;
   vec3_normalize(dir);

   mvp.view = mat4_view(eye, dir);

   mvp.model = mat4_identity();
   //mvp.model = mat4_scale(mvp.model, 0.025f);

   const f32 c = 255.0f;
   VkClearValue clear[2] = {};
   clear[0].color = (VkClearColorValue){48 / c, 10 / c, 36 / c, 1.0f};
   clear[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};

   renderpass_info.clearValueCount = 2;
   renderpass_info.pClearValues = clear;

   VkImage color_image = context->swapchain_info.images[image_index];
   VkImageMemoryBarrier color_image_begin_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_begin_barrier);

   // TODO: Enable depth image barriers
   //VkImage depth_image = context->swapchain_info.depths[image_index];
   //VkImageMemoryBarrier depth_image_begin_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
   //vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        //VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_begin_barrier);


   vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

   if(context->rtx_supported)
      vkCmdPushConstants(command_buffer, context->pipeline_layout,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, 0,
                   sizeof(mvp), &mvp);
   else
      vkCmdPushConstants(command_buffer, context->pipeline_layout,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                   sizeof(mvp), &mvp);

   VkViewport viewport = {};

   // y-is-up
   viewport.x = 0.0f;
   viewport.y = (f32)context->swapchain_info.image_height;
   viewport.width = (f32)context->swapchain_info.image_width;
   viewport.height = -(f32)context->swapchain_info.image_height;

   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;

   //debug_message("viewport: %d %d\n", (int)viewport.width, (int)viewport.height);

   VkRect2D scissor = {};
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   scissor.extent.width = (u32)context->swapchain_info.image_width;
   scissor.extent.height = (u32)context->swapchain_info.image_height;

   vkCmdSetViewport(command_buffer, 0, 1, &viewport);
   vkCmdSetScissor(command_buffer, 0, 1, &scissor);

   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->graphics_pipeline);

   VkDescriptorBufferInfo vb_info = {};
   vb_info.buffer = context->vb.handle;
   vb_info.offset = 0;
   vb_info.range = context->vb.size;

if(context->rtx_supported)
{
   VkDescriptorBufferInfo mb_info = {};
   mb_info.buffer = context->mb.handle;
   mb_info.offset = 0;
   mb_info.range = context->mb.size;

   VkWriteDescriptorSet descriptors[2] = {};
   descriptors[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   descriptors[0].dstBinding = 0;
   descriptors[0].descriptorCount = 1;
   descriptors[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   descriptors[0].pBufferInfo = &vb_info;

   descriptors[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   descriptors[1].dstBinding = 1;
   descriptors[1].descriptorCount = 1;
   descriptors[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   descriptors[1].pBufferInfo = &mb_info;

   // TODO: toggle mesh shading and vanilla vertex IA by button
   vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout, 0, array_count(descriptors), descriptors);

   // do 10 draw calls for measurement
   assert(context->meshlet_count <= 0xffff);

   u32 draw_calls = 1;
   for(u32 i = 0; i < draw_calls; ++i)
      vkCmdDrawMeshTasksEXT(command_buffer, context->meshlet_count, 1, 1);
}
else
{

   VkWriteDescriptorSet descriptors[1] = {};
   descriptors[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   descriptors[0].dstBinding = 0;
   descriptors[0].descriptorCount = 1;
   descriptors[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   descriptors[0].pBufferInfo = &vb_info;

   vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout, 0, array_count(descriptors), descriptors);

   // TODO: toggle mesh shading and vanilla vertex IA by button
   vkCmdBindIndexBuffer(command_buffer, context->ib.handle, 0, VK_INDEX_TYPE_UINT32);
   u32 draw_calls = 1;
   for(u32 i = 0; i < draw_calls; ++i)
      vkCmdDrawIndexed(command_buffer, context->index_count, 1, 0, 0, 0);
}

#if 0
   // draw axis
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->axis_pipeline);
   vkCmdDraw(command_buffer, 18, 1, 0, 0);

   // draw frustum
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->frustum_pipeline);
   vkCmdDraw(command_buffer, 12, 1, 0, 0);
#endif

   vkCmdEndRenderPass(command_buffer);

   VkImageMemoryBarrier color_image_end_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_end_barrier);

   // TODO: Enable depth image barriers
   //VkImageMemoryBarrier depth_image_end_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
   //vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        //VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_end_barrier);

   vkCmdWriteTimestamp(context->command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query_pool, 1);

   // end command buffer
   vk_assert(vkEndCommandBuffer(command_buffer));

   VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
   submit_info.waitSemaphoreCount = 1;
   submit_info.pWaitSemaphores = &context->image_ready_semaphore;

   submit_info.pWaitDstStageMask = &(VkPipelineStageFlags) { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &command_buffer;

   submit_info.signalSemaphoreCount = 1;
   submit_info.pSignalSemaphores = &context->image_done_semaphore;

   vk_assert(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

   VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
   present_info.swapchainCount = 1;
   present_info.pSwapchains = &context->swapchain_info.swapchain;

   present_info.pImageIndices = &image_index;

   present_info.waitSemaphoreCount = 1;
   present_info.pWaitSemaphores = &context->image_done_semaphore;

   VkResult present_result = vkQueuePresentKHR(context->graphics_queue, &present_info);

   if(present_result == VK_SUBOPTIMAL_KHR || present_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(hw, context->swapchain_info.image_width, context->swapchain_info.image_height);

   if(present_result != VK_SUCCESS)
      return;

   // wait until all queue ops are done
   // essentialy run gpu and cpu in sync
   // TODO: This is bad way to do sync but who cares for now
   vk_assert(vkDeviceWaitIdle(context->logical_device));

   u64 query_results[2];
   vk_assert(vkGetQueryPoolResults(context->logical_device, context->query_pool, 0, array_count(query_results), sizeof(query_results), query_results, sizeof(query_results[0]), VK_QUERY_RESULT_64_BIT));

   f64 gpu_begin = (f64)query_results[0] * context->time_period * 1e-6;
   f64 gpu_end = (f64)query_results[1] * context->time_period * 1e-6;

   static u32 begin = 0;
   static u32 timer = 0;
   u32 time = hw->timer.time();
   if(begin == 0)
      begin = time;
   u32 end = hw->timer.time();

   if(hw->timer.time() - timer > 1000)
   {
      if(context->rtx_supported)
         hw->log(hw, s8("cpu: %u ms; gpu: %.2f ms; #Meshlets: %u"), end - begin, gpu_end - gpu_begin, context->meshlet_count > 0xffff ? 0xffff : context->meshlet_count);
      //hw->log(hw, s8("cpu: %u ms; gpu: %.2f ms; #Meshlets: %d"), end - begin, gpu_end - gpu_begin, meshlet_count);
      else
         hw->log(hw, s8("cpu: %u ms; gpu: %.2f ms"), end - begin, gpu_end - gpu_begin);
      timer = hw->timer.time();
   }
   begin = end;
}

static VkDescriptorSetLayout vk_pipeline_set_layout_create(VkDevice logical_device, bool rtx_supported)
{
   assert(vk_valid_handle(logical_device));
   VkDescriptorSetLayout set_layout = 0;

   // TODO: cleanup
   if(rtx_supported)
   {
      VkDescriptorSetLayoutBinding bindings[2] = {};
      bindings[0].binding = 0;
      bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[0].descriptorCount = 1;
      bindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

      bindings[1].binding = 1;
      bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[1].descriptorCount = 1;
      bindings[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

      VkDescriptorSetLayoutCreateInfo info = {vk_info(DESCRIPTOR_SET_LAYOUT)};

      info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
      info.bindingCount = array_count(bindings);
      info.pBindings = bindings;

      vkCreateDescriptorSetLayout(logical_device, &info, 0, &set_layout);
   }
   else
   {
      VkDescriptorSetLayoutBinding bindings[1] = {};
      bindings[0].binding = 0;
      bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[0].descriptorCount = 1;
      bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

      VkDescriptorSetLayoutCreateInfo info = {vk_info(DESCRIPTOR_SET_LAYOUT)};

      info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
      info.bindingCount = array_count(bindings);
      info.pBindings = bindings;

      vkCreateDescriptorSetLayout(logical_device, &info, 0, &set_layout);
   }

   return set_layout;
}

static VkPipelineLayout vk_pipeline_layout_create(VkDevice logical_device, bool rtx_supported)
{
   assert(vk_valid_handle(logical_device));
   VkPipelineLayout layout = 0;

   VkPipelineLayoutCreateInfo info = {vk_info(PIPELINE_LAYOUT)};

   VkDescriptorSetLayout set_layout = vk_pipeline_set_layout_create(logical_device, rtx_supported);
   if(!vk_valid_handle(set_layout))
      return VK_NULL_HANDLE;

   VkPushConstantRange push_constants = {};
   if(rtx_supported)
      push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;
   else
      push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

   push_constants.offset = 0;
   push_constants.size = sizeof(mvp_transform);

   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = &push_constants;
   info.setLayoutCount = 1;
   info.pSetLayouts = &set_layout;

   vk_assert(vkCreatePipelineLayout(logical_device, &info, 0, &layout));

   return layout;
}

// TODO: Cleanup these pipelines
static VkPipeline vk_mesh_pipeline_create(VkDevice logical_device, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(shaders->ms));
   assert(vk_valid_handle(shaders->fs));
   assert(!vk_valid_handle(cache));

   VkPipeline pipeline = 0;

   VkPipelineShaderStageCreateInfo stages[OBJECT_SHADER_COUNT] = {};
   stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
   stages[0].module = shaders->ms;
   stages[0].pName = "main";

   stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   stages[1].module = shaders->fs;
   stages[1].pName = "main";

   VkGraphicsPipelineCreateInfo pipeline_info = {vk_info(GRAPHICS_PIPELINE)};
   pipeline_info.stageCount = array_count(stages);
   pipeline_info.pStages = stages;

   VkPipelineVertexInputStateCreateInfo vertex_input_info = {vk_info(PIPELINE_VERTEX_INPUT_STATE)};

   pipeline_info.pVertexInputState = &vertex_input_info;

   VkPipelineInputAssemblyStateCreateInfo assembly_info = {vk_info(PIPELINE_INPUT_ASSEMBLY_STATE)};
   assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
   pipeline_info.pInputAssemblyState = &assembly_info;

   VkPipelineViewportStateCreateInfo viewport_info = {vk_info(PIPELINE_VIEWPORT_STATE)};
   viewport_info.scissorCount = 1;
   viewport_info.viewportCount = 1;
   pipeline_info.pViewportState = &viewport_info;

   VkPipelineRasterizationStateCreateInfo raster_info = {vk_info(PIPELINE_RASTERIZATION_STATE)};
   raster_info.lineWidth = 1.0f;
   raster_info.cullMode = VK_CULL_MODE_BACK_BIT;
   raster_info.polygonMode = VK_POLYGON_MODE_FILL;
   raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   pipeline_info.pRasterizationState = &raster_info;

   VkPipelineMultisampleStateCreateInfo sample_info = {vk_info(PIPELINE_MULTISAMPLE_STATE)};
   sample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   pipeline_info.pMultisampleState = &sample_info;

   VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {vk_info(PIPELINE_DEPTH_STENCIL_STATE)};
   depth_stencil_info.depthTestEnable = true;
   depth_stencil_info.depthWriteEnable = true;
   depth_stencil_info.depthBoundsTestEnable = true;
   depth_stencil_info.stencilTestEnable = true;
   depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // right handed NDC
   depth_stencil_info.minDepthBounds = 0.0f;
   depth_stencil_info.maxDepthBounds = 1.0f;
   pipeline_info.pDepthStencilState = &depth_stencil_info;

   VkPipelineColorBlendAttachmentState color_blend_attachment = {};
   color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   VkPipelineColorBlendStateCreateInfo color_blend_info = {vk_info(PIPELINE_COLOR_BLEND_STATE)};
   color_blend_info.attachmentCount = 1;
   color_blend_info.pAttachments = &color_blend_attachment;
   pipeline_info.pColorBlendState = &color_blend_info;

   VkPipelineDynamicStateCreateInfo dynamic_info = {vk_info(PIPELINE_DYNAMIC_STATE)};
   dynamic_info.pDynamicStates = (VkDynamicState[3]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH};
   dynamic_info.dynamicStateCount = 3;
   pipeline_info.pDynamicState = &dynamic_info;

   pipeline_info.renderPass = renderpass;
   pipeline_info.layout = layout;

   vk_assert(vkCreateGraphicsPipelines(logical_device, cache, 1, &pipeline_info, 0, &pipeline));

   return pipeline;
}

// TODO: Cleanup these pipelines
static VkPipeline vk_graphics_pipeline_create(VkDevice logical_device, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(shaders->vs));
   assert(vk_valid_handle(shaders->fs));
   assert(!vk_valid_handle(cache));

   VkPipeline pipeline = 0;

   VkPipelineShaderStageCreateInfo stages[OBJECT_SHADER_COUNT] = {};
   stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   stages[0].module = shaders->vs;
   stages[0].pName = "main";
   stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

   stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   stages[1].module = shaders->fs;
   stages[1].pName = "main";

   VkGraphicsPipelineCreateInfo pipeline_info = {vk_info(GRAPHICS_PIPELINE)};
   pipeline_info.stageCount = array_count(stages);
   pipeline_info.pStages = stages;

   VkPipelineVertexInputStateCreateInfo vertex_input_info = {vk_info(PIPELINE_VERTEX_INPUT_STATE)};

   pipeline_info.pVertexInputState = &vertex_input_info;

   VkPipelineInputAssemblyStateCreateInfo assembly_info = {vk_info(PIPELINE_INPUT_ASSEMBLY_STATE)};
   assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
   pipeline_info.pInputAssemblyState = &assembly_info;

   VkPipelineViewportStateCreateInfo viewport_info = {vk_info(PIPELINE_VIEWPORT_STATE)};
   viewport_info.scissorCount = 1;
   viewport_info.viewportCount = 1;
   pipeline_info.pViewportState = &viewport_info;

   VkPipelineRasterizationStateCreateInfo raster_info = {vk_info(PIPELINE_RASTERIZATION_STATE)};
   raster_info.lineWidth = 1.0f;
   raster_info.cullMode = VK_CULL_MODE_BACK_BIT;
   raster_info.polygonMode = VK_POLYGON_MODE_FILL;
   raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   pipeline_info.pRasterizationState = &raster_info;

   VkPipelineMultisampleStateCreateInfo sample_info = {vk_info(PIPELINE_MULTISAMPLE_STATE)};
   sample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   pipeline_info.pMultisampleState = &sample_info;

   VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {vk_info(PIPELINE_DEPTH_STENCIL_STATE)};
   depth_stencil_info.depthTestEnable = true;
   depth_stencil_info.depthWriteEnable = true;
   depth_stencil_info.depthBoundsTestEnable = true;
   depth_stencil_info.stencilTestEnable = true;
   depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // right handed NDC
   depth_stencil_info.minDepthBounds = 0.0f;
   depth_stencil_info.maxDepthBounds = 1.0f;
   pipeline_info.pDepthStencilState = &depth_stencil_info;

   VkPipelineColorBlendAttachmentState color_blend_attachment = {};
   color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   VkPipelineColorBlendStateCreateInfo color_blend_info = {vk_info(PIPELINE_COLOR_BLEND_STATE)};
   color_blend_info.attachmentCount = 1;
   color_blend_info.pAttachments = &color_blend_attachment;
   pipeline_info.pColorBlendState = &color_blend_info;

   VkPipelineDynamicStateCreateInfo dynamic_info = {vk_info(PIPELINE_DYNAMIC_STATE)};
   dynamic_info.pDynamicStates = (VkDynamicState[3]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH};
   dynamic_info.dynamicStateCount = 3;
   pipeline_info.pDynamicState = &dynamic_info;

   pipeline_info.renderPass = renderpass;
   pipeline_info.layout = layout;

   vk_assert(vkCreateGraphicsPipelines(logical_device, cache, 1, &pipeline_info, 0, &pipeline));

   return pipeline;
}

static VkPipeline vk_axis_pipeline_create(VkDevice logical_device, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(shaders->vs));
   assert(vk_valid_handle(shaders->fs));
   assert(!vk_valid_handle(cache));

   VkPipeline pipeline = 0;

   VkPipelineShaderStageCreateInfo stages[OBJECT_SHADER_COUNT] = {};

   stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   stages[0].module = shaders->vs;
   stages[0].pName = "main";

   stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   stages[1].module = shaders->fs;
   stages[1].pName = "main";

   VkGraphicsPipelineCreateInfo pipeline_info = {vk_info(GRAPHICS_PIPELINE)};
   pipeline_info.stageCount = array_count(stages);
   pipeline_info.pStages = stages;

   VkPipelineVertexInputStateCreateInfo vertex_input_info = {vk_info(PIPELINE_VERTEX_INPUT_STATE)};
   pipeline_info.pVertexInputState = &vertex_input_info;

   VkPipelineInputAssemblyStateCreateInfo assembly_info = {vk_info(PIPELINE_INPUT_ASSEMBLY_STATE)};
   assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
   pipeline_info.pInputAssemblyState = &assembly_info;

   VkPipelineViewportStateCreateInfo viewport_info = {vk_info(PIPELINE_VIEWPORT_STATE)};
   viewport_info.scissorCount = 1;
   viewport_info.viewportCount = 1;
   pipeline_info.pViewportState = &viewport_info;

   VkPipelineRasterizationStateCreateInfo raster_info = {vk_info(PIPELINE_RASTERIZATION_STATE)};
   raster_info.lineWidth = 2.0f;
   raster_info.cullMode = VK_CULL_MODE_BACK_BIT;
   raster_info.polygonMode = VK_POLYGON_MODE_FILL;
   raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   pipeline_info.pRasterizationState = &raster_info;

   VkPipelineMultisampleStateCreateInfo sample_info = {vk_info(PIPELINE_MULTISAMPLE_STATE)};
   sample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   pipeline_info.pMultisampleState = &sample_info;

   VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {vk_info(PIPELINE_DEPTH_STENCIL_STATE)};
   depth_stencil_info.depthBoundsTestEnable = true;
   depth_stencil_info.depthTestEnable = true;
   depth_stencil_info.depthWriteEnable = true;
   depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // right handed NDC
   depth_stencil_info.minDepthBounds = 0.0f;
   depth_stencil_info.maxDepthBounds = 1.0f;
   pipeline_info.pDepthStencilState = &depth_stencil_info;

   VkPipelineColorBlendAttachmentState color_blend_attachment = {};
   color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   VkPipelineColorBlendStateCreateInfo color_blend_info = {vk_info(PIPELINE_COLOR_BLEND_STATE)};
   color_blend_info.attachmentCount = 1;
   color_blend_info.pAttachments = &color_blend_attachment;
   pipeline_info.pColorBlendState = &color_blend_info;

   VkPipelineDynamicStateCreateInfo dynamic_info = {vk_info(PIPELINE_DYNAMIC_STATE)};
   dynamic_info.pDynamicStates = (VkDynamicState[3]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   dynamic_info.dynamicStateCount = 2;
   pipeline_info.pDynamicState = &dynamic_info;

   pipeline_info.renderPass = renderpass;
   pipeline_info.layout = layout;

   vk_assert(vkCreateGraphicsPipelines(logical_device, cache, 1, &pipeline_info, 0, &pipeline));

   return pipeline;
}

VkInstance vk_instance_create(arena scratch)
{
   VkInstance instance = 0;

   u32 ext_count = 0;
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, 0)))
      return 0;

   VkExtensionProperties* extensions = push(&scratch, VkExtensionProperties, ext_count);
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, extensions)))
      return 0;

   const char** ext_names = push(&scratch, const char*, ext_count);

   for(size_t i = 0; i < ext_count; ++i)
      ext_names[i] = extensions[i].extensionName;

   VkInstanceCreateInfo instance_info = {vk_info(INSTANCE)};
   instance_info.pApplicationInfo = &(VkApplicationInfo) { .apiVersion = VK_API_VERSION_1_2 };

   instance_info.enabledExtensionCount = ext_count;
   instance_info.ppEnabledExtensionNames = ext_names;

#ifdef _DEBUG
   {
      const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
      instance_info.enabledLayerCount = array_count(validation_layers);
      instance_info.ppEnabledLayerNames = validation_layers;
   }
#endif
   vk_assert(vkCreateInstance(&instance_info, 0, &instance));

   return instance;
}

bool vk_initialize(hw* hw)
{
   if(!hw->renderer.window.handle)
      return false;

   if(!vk_valid(volkInitialize()))
      return false;

   vk_context* context = push(&hw->vk_storage, vk_context);

   // app callbacks
   hw->renderer.backends[vk_renderer_index] = context;
   hw->renderer.frame_present = vk_present;
   hw->renderer.frame_resize = vk_resize;
   hw->renderer.renderer_index = vk_renderer_index;

   context->storage = &hw->vk_storage;

   VkInstance instance = vk_instance_create(*context->storage);

   if(!instance)
      return false;

   volkLoadInstance(instance);

   context->instance = instance;

#ifdef _DEBUG
   {
      VkDebugUtilsMessengerCreateInfoEXT messenger_info = {vk_info_ext(DEBUG_UTILS_MESSENGER)};
      messenger_info.messageSeverity =
         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      messenger_info.messageType =
         VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      messenger_info.pfnUserCallback = vk_debug_callback;

      VkDebugUtilsMessengerEXT messenger;

      if(!vk_valid(vk_create_debugutils_messenger_ext(instance, &messenger_info, 0, &messenger)))
         return false;
   }
#endif

   context->queue_family_index = vk_logical_device_select_family_index();
   context->physical_device = vk_physical_device_select(context, *context->storage, instance);
   context->logical_device = vk_logical_device_create(context->physical_device, *context->storage, context->queue_family_index, context->rtx_supported);
   context->surface = hw->renderer.window_surface_create(instance, hw->renderer.window.handle);
   context->image_ready_semaphore = vk_semaphore_create(context->logical_device);
   context->image_done_semaphore = vk_semaphore_create(context->logical_device);
   context->graphics_queue = vk_graphics_queue_create(context->logical_device, context->queue_family_index);
   context->query_pool_size = 128;
   context->query_pool = vk_query_pool_create(context->logical_device, context->query_pool_size);
   context->command_pool = vk_command_pool_create(context->logical_device, context->queue_family_index);
   context->command_buffer = vk_command_buffer_create(context->logical_device, context->command_pool);
   context->swapchain_info = vk_swapchain_info_create(context, hw->renderer.window.width, hw->renderer.window.height, context->queue_family_index);
   context->renderpass = vk_renderpass_create(context->logical_device, context->swapchain_info.format, VK_FORMAT_D32_SFLOAT);

   hw->renderer.frame_resize(hw, hw->renderer.window.width, hw->renderer.window.height);

   if(context->rtx_supported)
      assert(vk_mesh_shader_max_tasks(context->physical_device) >= 256);

   if(!vk_swapchain_update(context))
      return false;

   u32 shader_count = 0;
   const char** shader_names = vk_shader_folder_read(context->storage, "bin\\assets\\shaders");
   for(const char** p = shader_names; p && *p; ++p)
      shader_count++;

   // TODO: store this inside the context?
   spv_hash_table shader_hash_table = {};
   shader_hash_table.max_count = shader_count;

   spv_lookup(context->logical_device, *context->storage, &shader_hash_table, shader_names, context->rtx_supported);

   VkPipelineCache cache = 0; // TODO: enable
   VkPipelineLayout layout = vk_pipeline_layout_create(context->logical_device, context->rtx_supported);

   if(context->rtx_supported)
   {
      vk_shader_modules mm = spv_hash_lookup(&shader_hash_table, "meshlet");
      context->graphics_pipeline = vk_mesh_pipeline_create(context->logical_device, context->renderpass, cache, layout, &mm);
   }
   else
   {
      vk_shader_modules gm = spv_hash_lookup(&shader_hash_table, "graphics");
      context->graphics_pipeline = vk_graphics_pipeline_create(context->logical_device, context->renderpass, cache, layout, &gm);
   }

   vk_shader_modules am = spv_hash_lookup(&shader_hash_table, "axis");
   context->axis_pipeline = vk_axis_pipeline_create(context->logical_device, context->renderpass, cache, layout, &am);

   context->pipeline_layout = layout;

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_props);

   // TODO: fine tune these and get device memory limits
   // video memory
   size buffer_size = MB(1024);
   vk_buffer scratch_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   vk_buffer index_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   vk_buffer vertex_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   if(context->rtx_supported)
   {
      vk_buffer meshlet_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      context->mb = meshlet_buffer;
   }
   else
      context->ib = index_buffer;
   context->vb = vertex_buffer;

   vk_buffers_upload(context, scratch_buffer);

   return true;
}

bool vk_uninitialize(hw* hw)
{
   vk_context* context = hw->renderer.backends[vk_renderer_index];

   vkDestroyCommandPool(context->logical_device, context->command_pool, 0);
   vkDestroyQueryPool(context->logical_device, context->query_pool, 0);

   vkDestroySwapchainKHR(context->logical_device, context->swapchain_info.swapchain, 0);
   vkDestroyPipeline(context->logical_device, context->axis_pipeline, 0);
   vkDestroyPipeline(context->logical_device, context->frustum_pipeline, 0);
   vkDestroyPipeline(context->logical_device, context->graphics_pipeline, 0);

   // TODO this
   //vkDestroyShaderModule(context->logical_device, context->sha
   //vkDestroyShaderModule(context->logical_device, context->sha

   vkDestroyBuffer(context->logical_device, context->ib.handle, 0);
   vkDestroyBuffer(context->logical_device, context->vb.handle, 0);
   vkDestroyBuffer(context->logical_device, context->mb.handle, 0);

   vkDestroyRenderPass(context->logical_device, context->renderpass, 0);
   vkDestroySemaphore(context->logical_device, context->image_done_semaphore, 0);
   vkDestroySemaphore(context->logical_device, context->image_ready_semaphore, 0);

   vkDestroySurfaceKHR(context->instance, context->surface, 0);

   // TODO this - must destroy all buffers before instance
   //vkDestroyDevice(context->logical_device, 0);
   //vkDestroyInstance(context->instance, 0);

   return true;
}
