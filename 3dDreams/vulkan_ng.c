#include "arena.h"
#include "common.h"
#include "graphics.h"
#include "vulkan_ng.h"

// TODO: extract out win32 shit out to platform layer
#include "win32_file_io.c"

#include "vulkan_spirv_loader.c"
#include "hash.c"
#include "mesh.c"
#include "textures.c"

static void obj_file_read_callback(void *ctx, const char *filename, int is_mtl, const char *obj_filename, char **buf, size_t *len)
{
   char file_path[MAX_PATH] = {};

   obj_user_ctx* user_data = (obj_user_ctx*)ctx;

   s8 project_dir = vk_project_directory(&user_data->scratch);

   wsprintf(file_path, "%s\\assets\\objs\\%s", (const char*)project_dir.data, filename);

   arena file_read = win32_file_read(&user_data->scratch, file_path);

   *len = arena_left(&file_read);
   *buf = file_read.beg;
}

// TODO: clean up these top-level read apis
static void vk_obj_file_read(vk_context* context, void *user_context, s8 filename)
{
   tinyobj_shape_t* shapes = 0;
   tinyobj_material_t* materials = 0;
   tinyobj_attrib_t attrib = {};

   size_t shape_count = 0;
   size_t material_count = 0;

   tinyobj_attrib_init(&attrib);

   obj_user_ctx* user_data = (obj_user_ctx*)user_context;

   if(tinyobj_parse_obj(&attrib, &shapes, &shape_count, &materials, &material_count, s8_data(filename), obj_file_read_callback, user_data, TINYOBJ_FLAG_TRIANGULATE) != TINYOBJ_SUCCESS)
      hw_message_box("Could not load .obj file");
   obj_load(context, *context->storage, &attrib);

   tinyobj_materials_free(materials, material_count);
   tinyobj_shapes_free(shapes, shape_count);
   tinyobj_attrib_free(&attrib);
}

static vk_buffer_objects vk_gltf_read(vk_context* context, arena scratch, void *user_context, s8 filename)
{
   vk_buffer_objects result = {};

   array(char) file_path = {context->storage};
   s8 prefix = s8("%s\\assets\\gltf\\%s");
   s8 project_dir = vk_project_directory(context->storage);

   file_path.count = project_dir.len + prefix.len + filename.len;
   array_resize(file_path, file_path.count);
   wsprintf(file_path.data, s8_data(prefix), (const char*)project_dir.data, filename.data);

   // TODO: array(char) to s8
   s8 gltf_path = {.data = (u8*)file_path.data, .len = file_path.count};

   result = vk_gltf_load(context, gltf_path);

   return result;
}

static void vk_shader_load(VkDevice logical_device, arena scratch, const char* shader_name, vk_shader_modules* shader_modules)
{
   assert(vk_valid_handle(logical_device));

   s8 project_dir = vk_project_directory(&scratch);

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

   VkShaderModule shader_module = vk_shader_spv_module_load(logical_device, &scratch, project_dir, s8(shader_name));

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

static void spv_lookup(VkDevice logical_device, arena* storage, spv_hash_table* table, const char** shader_names)
{
   assert(vk_valid_handle(logical_device));

   // TODO: make function for hash tables
   table->keys = push(storage, const char*, table->max_count);
   table->values = push(storage, vk_shader_modules, table->max_count);

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
         if(strncmp(shader_name + i, "meshlet", strlen("meshlet")) == 0)
         {
            vk_shader_modules ms = spv_hash_lookup(table, "meshlet");
            // TODO: optimize shader loading - load all the shaders at once
            vk_shader_load(logical_device, *storage, *p, &ms);
            spv_hash_insert(table, "meshlet", ms);
            break;
         }
         if(strncmp(shader_name + i, "graphics", strlen("graphics")) == 0)
         {
            vk_shader_modules gm = spv_hash_lookup(table, "graphics");
            vk_shader_load(logical_device, *storage, *p, &gm);
            spv_hash_insert(table, "graphics", gm);
            break;
         }
         if(strncmp(shader_name + i, "axis", strlen("axis")) == 0)
         {
            vk_shader_modules am = spv_hash_lookup(table, "axis");
            vk_shader_load(logical_device, *storage, *p, &am);
            spv_hash_insert(table, "axis", am);
            break;
         }
      }
   }
}

static void vk_buffer_destroy(VkDevice device, vk_buffer* buffer)
{
   vkFreeMemory(device, buffer->memory, 0);
   vkDestroyBuffer(device, buffer->handle, 0);
}

static void vk_buffer_to_image_upload(vk_context* context, vk_buffer scratch, VkImage image, VkExtent3D image_extent, const void* data, VkDeviceSize dev_size)
{
   assert(data);
   assert(dev_size > 0);
   assert(scratch.data && scratch.size >= (size)dev_size);
   assert(image_extent.width && image_extent.height && image_extent.depth);
   assert(vk_valid_handle(image));

   memcpy(scratch.data, data, dev_size);

   vk_assert(vkResetCommandPool(context->logical_device, context->command_pool, 0));

   VkCommandBufferBeginInfo begin_info = {};
   begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vk_assert(vkBeginCommandBuffer(context->command_buffer, &begin_info));

   VkImageMemoryBarrier img_barrier_to_transfer = {};
   img_barrier_to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   img_barrier_to_transfer.srcAccessMask = 0;
   img_barrier_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   img_barrier_to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   img_barrier_to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   img_barrier_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_transfer.image = image;
   img_barrier_to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   img_barrier_to_transfer.subresourceRange.baseMipLevel = 0;
   img_barrier_to_transfer.subresourceRange.levelCount = 1;
   img_barrier_to_transfer.subresourceRange.baseArrayLayer = 0;
   img_barrier_to_transfer.subresourceRange.layerCount = 1;

   vkCmdPipelineBarrier(
      context->command_buffer,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &img_barrier_to_transfer
   );

   VkBufferImageCopy region = {};
   region.bufferOffset = 0;
   region.bufferRowLength = 0;
   region.bufferImageHeight = 0;
   region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   region.imageSubresource.mipLevel = 0;
   region.imageSubresource.baseArrayLayer = 0;
   region.imageSubresource.layerCount = 1;
   region.imageOffset.x = 0;
   region.imageOffset.y = 0;
   region.imageOffset.z = 0;
   region.imageExtent = image_extent;

   vkCmdCopyBufferToImage(
      context->command_buffer,
      scratch.handle,
      image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region
   );

   VkImageMemoryBarrier img_barrier_to_shader = {};
   img_barrier_to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   img_barrier_to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   img_barrier_to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   img_barrier_to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   img_barrier_to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   img_barrier_to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_shader.image = image;
   img_barrier_to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   img_barrier_to_shader.subresourceRange.baseMipLevel = 0;
   img_barrier_to_shader.subresourceRange.levelCount = 1;
   img_barrier_to_shader.subresourceRange.baseArrayLayer = 0;
   img_barrier_to_shader.subresourceRange.layerCount = 1;

   vkCmdPipelineBarrier(
      context->command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &img_barrier_to_shader
   );

   vk_assert(vkEndCommandBuffer(context->command_buffer));

   VkSubmitInfo submit_info = {};
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.waitSemaphoreCount = 0;
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &context->command_buffer;
   submit_info.signalSemaphoreCount = 0;

   vk_assert(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
   // instead of explicit memory sync between queue submissions with fences etc we wait for all gpu jobs to complete before moving on
   // TODO: bad for perf
   vk_assert(vkDeviceWaitIdle(context->logical_device));
}

static void vk_buffer_upload(vk_context* context, vk_buffer buffer, vk_buffer scratch, const void* data, VkDeviceSize dev_size)
{
   assert(data);
   assert(dev_size > 0);
   assert(scratch.data && scratch.size >= (size)dev_size);
   memcpy(scratch.data, data, dev_size);

   vk_assert(vkResetCommandPool(context->logical_device, context->command_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vk_assert(vkBeginCommandBuffer(context->command_buffer, &buffer_begin_info));

   VkBufferCopy buffer_region = {0, 0, dev_size};
   vkCmdCopyBuffer(context->command_buffer, scratch.handle, buffer.handle, 1, &buffer_region);

   VkBufferMemoryBarrier copy_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
   copy_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   copy_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier.buffer = buffer.handle;
   copy_barrier.size = dev_size;
   copy_barrier.offset = 0;

   vkCmdPipelineBarrier(context->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 1, &copy_barrier, 0, 0);

   vk_assert(vkEndCommandBuffer(context->command_buffer));

   VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &context->command_buffer;

   vk_assert(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
   // instead of explicit memory sync between queue submissions with fences etc we wait for all gpu jobs to complete before moving on
   // TODO: bad for perf
   vk_assert(vkDeviceWaitIdle(context->logical_device));
}

static vk_buffer_objects vk_buffer_objects_create(vk_context* context, s8 asset_file)
{
   vk_buffer_objects result = {};

   if(s8_is_substr(asset_file, s8(".gltf")))
   {
      gltf_user_ctx user_data = {};
      user_data.scratch = *context->storage;

      result = vk_gltf_read(context, *context->storage, &user_data, asset_file);
   }
   else hw_message_box("Unsupported asset format");

   return result;
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
    void* user_data)
{
   hw* h = (hw*)user_data;
#if _DEBUG
   //debug_message("Validation layer message: %s\n", data->pMessage);
   h->log(s8("Validation layer message: %s\n"), data->pMessage);
#endif
#ifdef vk_break_on_validation
   assert((type & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0);
#endif

   return false;
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

static vk_swapchain_surface vk_window_swapchain_surface(VkPhysicalDevice physical_device, u32 width, u32 height, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_device));
   assert(vk_valid_handle(surface));

   vk_swapchain_surface result = {};

   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps)))
      return (vk_swapchain_surface){0};

   // triple buffering
   if(surface_caps.minImageCount < 2)
      return (vk_swapchain_surface){0};

   if(!implies(surface_caps.maxImageCount != 0, surface_caps.maxImageCount >= surface_caps.minImageCount + 1))
      return (vk_swapchain_surface){0};

   u32 image_count = surface_caps.minImageCount + 1;

   result.image_width = width;
   result.image_height = height;
   result.image_count = image_count;
   result.format = vk_swapchain_format(physical_device, surface);

   return result;
}

static VkPhysicalDevice vk_physical_device_select(hw* hw, vk_context* context, arena scratch, VkInstance instance)
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

         VkExtensionProperties* extensions = push(&scratch, VkExtensionProperties, extension_count);
         vk_assert(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, extensions));

         for(u32 j = 0; j < extension_count; ++j)
         {
            printf("Available Vulkan device extension[%u]: %s\n", j, extensions[j].extensionName);
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

static u32 vk_logical_device_select_family_index(VkPhysicalDevice physical_device, VkSurfaceKHR surface, arena scratch)
{
   u32 queue_family_count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, 0);

   VkQueueFamilyProperties* queue_families = push(&scratch, VkQueueFamilyProperties, queue_family_count);
   vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

   for(u32 i = 0; i < queue_family_count; i++)
   {
      VkBool32 present_support = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support);

      // graphics and presentation within same queue for simplicity
      if((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support)
         return i;
   }

   return (u32)-1;
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

static VkDevice vk_logical_device_create(hw* hw, VkPhysicalDevice physical_device, arena scratch, u32 queue_family_index, bool rtx_supported)
{
   assert(vk_valid_handle(physical_device));
   f32 queue_prio = 1.0f;

   VkDeviceQueueCreateInfo queue_info = {vk_info(DEVICE_QUEUE)};
   queue_info.queueFamilyIndex = queue_family_index; // TODO: query the right queue family
   queue_info.queueCount = 1;
   queue_info.pQueuePriorities = &queue_prio;

   VkDeviceCreateInfo ldev_info = {vk_info(DEVICE)};

   array(s8) extensions = {&scratch};

   array_push(extensions) = s8(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
   array_push(extensions) = s8(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
   array_push(extensions) = s8(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);

   if(rtx_supported)
   {
      array_push(extensions) = s8(VK_EXT_MESH_SHADER_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
   }

   VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

   VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
   features12.storageBuffer8BitAccess = true;
   features12.uniformAndStorageBuffer8BitAccess = true;
   features12.storagePushConstant8 = true;
   features12.shaderSampledImageArrayNonUniformIndexing = true;
   features12.descriptorBindingSampledImageUpdateAfterBind = true;
   features12.descriptorBindingUpdateUnusedWhilePending = true;
   features12.descriptorBindingPartiallyBound = true;

   VkPhysicalDeviceMultiviewFeatures features_multiview = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
   features_multiview.multiview = true;

   VkPhysicalDeviceFragmentShadingRateFeaturesKHR features_frag_shading = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
   features_frag_shading.primitiveFragmentShadingRate = true;

   features.pNext = &features12;
   features12.pNext = &features_multiview;
   features_multiview.pNext = &features_frag_shading;

   VkPhysicalDeviceMeshShaderFeaturesEXT features_mesh_shader = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

   if(rtx_supported)
   {
      features_frag_shading.pNext = &features_mesh_shader;
      features_mesh_shader.meshShader = true;
      features_mesh_shader.taskShader = true;
      features_mesh_shader.multiviewMeshShader = true;
   }

   vkGetPhysicalDeviceFeatures2(physical_device, &features);

   features.features.depthBounds = true;
   features.features.wideLines = true;
   features.features.fillModeNonSolid = true;
   features.features.sampleRateShading = true;

   const char** extension_names = push(&scratch, char*, extensions.count);

   for(u32 i = 0; i < extensions.count; ++i)
   {
      extension_names[i] = (const char*)extensions.data[i].data;

      printf("Using Vulkan device extension[%u]: %s\n", i, extension_names[i]);
   }

   ldev_info.queueCreateInfoCount = 1;
   ldev_info.pQueueCreateInfos = &queue_info;
   ldev_info.enabledExtensionCount = (u32)extensions.count;
   ldev_info.ppEnabledExtensionNames = extension_names;
   ldev_info.pNext = &features;

   VkDevice logical_device;
   vk_assert(vkCreateDevice(physical_device, &ldev_info, 0, &logical_device));

   return logical_device;
}

static VkQueue vk_graphics_queue_create(VkDevice logical_device, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_device));

   VkQueue graphics_queue = 0;
   u32 queue_index = 0;

   // TODO: Get the queue index
   vkGetDeviceQueue(logical_device, queue_family_index, queue_index, &graphics_queue);

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

static VkSwapchainKHR vk_swapchain_create(VkDevice logical_device, VkSurfaceKHR surface, vk_swapchain_surface* surface_info, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_format(surface_info->format));

   VkSwapchainKHR swapchain = 0;

   VkSwapchainCreateInfoKHR swapchain_info = {vk_info_khr(SWAPCHAIN)};
   swapchain_info.surface = surface;
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
   swapchain_info.oldSwapchain = surface_info->handle;

   vk_assert(vkCreateSwapchainKHR(logical_device, &swapchain_info, 0, &swapchain));

   return swapchain;
}

static vk_swapchain_surface vk_swapchain_surface_create(vk_context* context, u32 swapchain_width, u32 swapchain_height, u32 queue_family_index)
{
   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, context->surface, &surface_caps)))
      return (vk_swapchain_surface) {};

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

   vk_swapchain_surface swapchain_info = vk_window_swapchain_surface(context->physical_device, swapchain_extent.width, swapchain_extent.height, context->surface);

   swapchain_info.handle = vk_swapchain_create(context->logical_device, context->surface, &swapchain_info, queue_family_index);

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

static VkFramebuffer vk_framebuffer_create(VkDevice logical_device, VkRenderPass renderpass, vk_swapchain_surface* surface_info, VkImageView* attachments, u32 attachment_count)
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
   for(u32 i = 0; i < context->swapchain_surface.image_count; ++i)
   {
      vkDestroyImageView(context->logical_device, context->swapchain_images.image_views.data[i], 0);
      vkDestroyImageView(context->logical_device, context->swapchain_images.depth_views.data[i], 0);
   }
   for(u32 i = 0; i < context->framebuffers.count; ++i)
      vkDestroyFramebuffer(context->logical_device, context->framebuffers.data[i], 0);

   vkDestroySwapchainKHR(context->logical_device, context->swapchain_surface.handle, 0);
}

// TODO: Break into separate routines
static void vk_swapchain_update(vk_context* context)
{
   vk_assert(vkGetSwapchainImagesKHR(context->logical_device, context->swapchain_surface.handle, &context->swapchain_surface.image_count, context->swapchain_images.images.data));

   VkExtent3D depth_extent = {context->swapchain_surface.image_width, context->swapchain_surface.image_height, 1};

   for(u32 i = 0; i < context->swapchain_surface.image_count; ++i)
   {
      context->swapchain_images.depths.data[i] = vk_depth_image_create(context, VK_FORMAT_D32_SFLOAT, depth_extent);

      context->swapchain_images.image_views.data[i] = vk_image_view_create(context, context->swapchain_surface.format, context->swapchain_images.images.data[i], VK_IMAGE_ASPECT_COLOR_BIT);
      context->swapchain_images.depth_views.data[i] = vk_image_view_create(context, VK_FORMAT_D32_SFLOAT, context->swapchain_images.depths.data[i], VK_IMAGE_ASPECT_DEPTH_BIT);

      VkImageView attachments[2] = {context->swapchain_images.image_views.data[i], context->swapchain_images.depth_views.data[i]};

      VkFramebuffer fb = vk_framebuffer_create(context->logical_device, context->renderpass, &context->swapchain_surface, attachments, array_count(attachments));
      context->framebuffers.data[i] = fb;
   }
}

static void vk_resize(hw* hw, u32 width, u32 height)
{
   if(width == 0 || height == 0)
      return;

   u32 renderer_index = hw->renderer.renderer_index;
   assert(renderer_index < RENDERER_COUNT);

   vk_context* context = hw->renderer.backends[renderer_index];

   mvp_transform mvp = {};
   const f32 ar = (f32)width / height;

   mvp.n = 0.01f;
   mvp.f = 10000.0f;
   mvp.ar = ar;

   mvp.projection = mat4_perspective(ar, 75.0f, mvp.n, mvp.f);
   hw->renderer.mvp = mvp;

   vkDeviceWaitIdle(context->logical_device);

   vk_swapchain_destroy(context);
   context->swapchain_surface = vk_swapchain_surface_create(context, width, height, context->queue_family_index);
   vk_swapchain_update(context);

   printf("Viewport resized: [%u %u]\n", width, height);
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
   VkResult next_image_result = vkAcquireNextImageKHR(context->logical_device, context->swapchain_surface.handle, UINT64_MAX, context->image_ready_semaphore, VK_NULL_HANDLE, &image_index);

   if(next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(hw, context->swapchain_surface.image_width, context->swapchain_surface.image_height);

   if(next_image_result != VK_SUBOPTIMAL_KHR && next_image_result != VK_SUCCESS)
      return;

   vk_assert(vkResetCommandPool(context->logical_device, context->command_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   VkRenderPassBeginInfo renderpass_info = {vk_info_begin(RENDER_PASS)};
   renderpass_info.renderPass = context->renderpass;
   renderpass_info.framebuffer = context->framebuffers.data[image_index];
   renderpass_info.renderArea.extent = (VkExtent2D)
   {context->swapchain_surface.image_width, context->swapchain_surface.image_height};

   VkCommandBuffer command_buffer = context->command_buffer;

   vk_assert(vkBeginCommandBuffer(command_buffer, &buffer_begin_info));

   vkCmdResetQueryPool(context->command_buffer, context->query_pool, 0, context->query_pool_size);
   vkCmdWriteTimestamp(context->command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query_pool, 0);

   const f32 ar = (f32)context->swapchain_surface.image_width / context->swapchain_surface.image_height;

   mvp_transform mvp = hw->renderer.mvp;
   assert(mvp.n > 0.0f);
   assert(mvp.ar != 0.0f);

   // world space origin
   vec3 eye = state->camera.eye;
   vec3 dir = state->camera.dir;
   vec3_normalize(dir);

   mvp.view = mat4_view(eye, dir);

   mvp.model = mat4_identity();
   mvp.meshlet_offset = 0;

   const f32 c = 255.0f;
   VkClearValue clear[2] = {};
   clear[0].color = (VkClearColorValue){68.f / c, 10.f / c, 36.f / c, 1.0f};
   clear[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};

   renderpass_info.clearValueCount = 2;
   renderpass_info.pClearValues = clear;

   VkImage color_image = context->swapchain_images.images.data[image_index];
   VkImageMemoryBarrier color_image_begin_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_begin_barrier);

   VkImage depth_image = context->swapchain_images.depths.data[image_index];
   VkImageMemoryBarrier depth_image_begin_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_begin_barrier);


   vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

   VkViewport viewport = {};

   // y-is-up
   viewport.x = 0.0f;
   viewport.y = (f32)context->swapchain_surface.image_height;
   viewport.width = (f32)context->swapchain_surface.image_width;
   viewport.height = -(f32)context->swapchain_surface.image_height;

   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;

   VkRect2D scissor = {};
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   scissor.extent.width = (u32)context->swapchain_surface.image_width;
   scissor.extent.height = (u32)context->swapchain_surface.image_height;

   vkCmdSetViewport(command_buffer, 0, 1, &viewport);
   vkCmdSetScissor(command_buffer, 0, 1, &scissor);

   VkDescriptorBufferInfo vb_info = {};
   vb_info.buffer = context->bos.vb.handle;
   vb_info.offset = 0;
   vb_info.range = context->bos.vb.size;  // TODO: can be VK_WHOLE_SIZE

   // TODO: into narrow contract function
   assert(vb_info.buffer);
   assert(vb_info.range > 0);

   // TODO: Currently this is broken
   // TODO: Handle multi-meshes in the mesh shader
   if(state->rtx_enabled)
   {
      // bind bindless textures
      vkCmdBindDescriptorSets(
         command_buffer,
         VK_PIPELINE_BIND_POINT_GRAPHICS,
         context->rtx_pipeline_layout,
         1,
         1,
         &context->texture_descriptor.set,
         0,
         0
      );

      vkCmdPushConstants(command_buffer, context->rtx_pipeline_layout,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, 0,
                   sizeof(mvp), &mvp);

      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->rtx_pipeline);

      VkDescriptorBufferInfo mb_info = {};
      mb_info.buffer = context->bos.mb.handle;
      mb_info.offset = 0;
      mb_info.range = context->bos.mb.size;

   // TODO: into narrow contract function
      assert(mb_info.buffer);
      assert(mb_info.range > 0);

      // update the vertex and meshlet storage buffers
      VkWriteDescriptorSet storage_buffer[2] = {};
      storage_buffer[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      storage_buffer[0].dstBinding = 0;
      storage_buffer[0].dstSet = VK_NULL_HANDLE;
      storage_buffer[0].descriptorCount = 1;
      storage_buffer[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      storage_buffer[0].pBufferInfo = &vb_info;

      storage_buffer[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      storage_buffer[1].dstBinding = 1;
      storage_buffer[1].dstSet = VK_NULL_HANDLE;
      storage_buffer[1].descriptorCount = 1;
      storage_buffer[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      storage_buffer[1].pBufferInfo = &mb_info;

      vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->rtx_pipeline_layout, 0, array_count(storage_buffer), storage_buffer);

      u32 meshlet_limit = 0xffff;
      u32 draw_calls = context->meshlet_count / meshlet_limit;
      u32 base = 0;

      // 0xffff is the AMD limit - check this also on nvidia
      // draw meshlets in chunks
      for(u32 i = 0; i < draw_calls; ++i)
      {
         vkCmdPushConstants(command_buffer, context->rtx_pipeline_layout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT,
                            offsetof(mvp_transform, meshlet_offset), sizeof(mvp.meshlet_offset),
                            &base);

         vkCmdDrawMeshTasksEXT(command_buffer, meshlet_limit, 1, 1);
         base += meshlet_limit;
      }

      vkCmdPushConstants(command_buffer, context->rtx_pipeline_layout,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT,
                         offsetof(mvp_transform, meshlet_offset), sizeof(mvp.meshlet_offset),
                         &base);

      // draw rest of the meshlets
      vkCmdDrawMeshTasksEXT(command_buffer, context->meshlet_count % meshlet_limit, 1, 1);
   }
   else
   {
      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->graphics_pipeline);

      // bind bindless textures
      vkCmdBindDescriptorSets(
         command_buffer,
         VK_PIPELINE_BIND_POINT_GRAPHICS,
         context->pipeline_layout,
         1,
         1,
         &context->texture_descriptor.set,
         0,
         0
      );

      // update the vertex storage buffer
      VkWriteDescriptorSet storage_buffer = {};
      storage_buffer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      storage_buffer.dstBinding = 0;
      storage_buffer.dstSet = VK_NULL_HANDLE;
      storage_buffer.descriptorCount = 1;
      storage_buffer.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      storage_buffer.pBufferInfo = &vb_info;

      vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout, 0, 1, &storage_buffer);

      // TODO: into narrow contract function
      assert(context->bos.ib.handle);
      vkCmdBindIndexBuffer(command_buffer, context->bos.ib.handle, 0, VK_INDEX_TYPE_UINT32);

#if 1
      for(u32 i = 0; i < context->mesh_instances.count; ++i)
      {
         vk_mesh_instance mi = context->mesh_instances.data[i];
#if 0
         f32 s = mi.scale;
         vec4 r = mi.orientation;
         vec3 t = mi.pos;

         quaternion_to_matrix(&r, mvp.model.data);

         assert(s > .0f);

         // TODO: negative scales?
         mvp.model.data[0] *= s;
         mvp.model.data[5] *= s;
         mvp.model.data[10] *= s;

         mvp.model.data[12] = t.x;
         mvp.model.data[13] = t.y;
         mvp.model.data[14] = t.z;
#else
         mvp.model = mi.model;
#endif

         vkCmdPushConstants(command_buffer, context->pipeline_layout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                      sizeof(mvp), &mvp);

         vk_mesh_draw md = context->mesh_draws.data[mi.mesh_index];
         vkCmdDrawIndexed(command_buffer, (u32)md.index_count, 1, (u32)md.index_offset, (u32)md.vertex_offset, 0);
      }
#else
      // TODO: scratch arenas
      VkDrawIndexedIndirectCommand* draw_commands = malloc(context->mesh_instances.count * sizeof(VkDrawIndexedIndirectCommand));

      for(u32 i = 0; i < context->mesh_instances.count; ++i)
      {
         vk_mesh_instance mi = context->mesh_instances.data[i];
         vk_mesh_draw md = context->mesh_draws.data[mi.mesh_index];

         VkDrawIndexedIndirectCommand cmd =
         {
             .indexCount = (u32)md.index_count,
             .instanceCount = 1,               // one instance per mesh_instance
             .firstIndex = (u32)md.index_offset,
             .vertexOffset = (i32)md.vertex_offset,
             .firstInstance = i                // important: matches instance ID
         };

         draw_commands[i] = cmd;
      }

      //vk_buffer_upload(context->logical_device, context->graphics_queue, command_buffer, context->indirect_buffer, draw_commands, sizeof(VkDrawIndexedIndirectCommand) * context->mesh_instances.count);

      free(draw_commands);

      vkCmdDrawIndexedIndirect(command_buffer, context->indirect_buffer, 0, (u32)context->mesh_instances.count, sizeof(VkDrawIndexedIndirectCommand));
#endif
   }

   // draw axis
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->axis_pipeline);
   vkCmdDraw(command_buffer, 18, 1, 0, 0);

   vkCmdEndRenderPass(command_buffer);

   VkImageMemoryBarrier color_image_end_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_end_barrier);

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
   present_info.pSwapchains = &context->swapchain_surface.handle;

   present_info.pImageIndices = &image_index;

   present_info.waitSemaphoreCount = 1;
   present_info.pWaitSemaphores = &context->image_done_semaphore;

   VkResult present_result = vkQueuePresentKHR(context->graphics_queue, &present_info);

   if(present_result == VK_SUBOPTIMAL_KHR || present_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(hw, context->swapchain_surface.image_width, context->swapchain_surface.image_height);

   if(present_result != VK_SUCCESS)
      return;

   // wait until all queue ops are done
   // essentialy run gpu and cpu in sync (60 FPS usually)
   // TODO: This is bad way to do sync but who cares for now
   vk_assert(vkDeviceWaitIdle(context->logical_device));

   u64 query_results[2];
   vk_assert(vkGetQueryPoolResults(context->logical_device, context->query_pool, 0, array_count(query_results), sizeof(query_results), query_results, sizeof(query_results[0]), VK_QUERY_RESULT_64_BIT));

#if 1
   f64 gpu_begin = (f64)query_results[0] * context->time_period * 1e-6;
   f64 gpu_end = (f64)query_results[1] * context->time_period * 1e-6;

   static u32 begin = 0;
   static u32 timer = 0;
   u32 time = hw->timer.time();
   if(begin == 0)
      begin = time;
   u32 end = hw->timer.time();

   {
      // frame logs
      // TODO: this should really be in app.c
      if(hw->timer.time() - timer > 100)
      {
         if(hw->state.rtx_enabled)
            hw->window_title(hw, s8("cpu: %u ms; gpu: %.2f ms; #Meshlets: %u; Press 'R' to toggle RTX; RTX ON"), end - begin, gpu_end - gpu_begin, context->meshlet_count);
         else
            hw->window_title(hw, s8("cpu: %u ms; gpu: %.2f ms; #Meshlets: 0; Press 'R' to toggle RTX; RTX OFF"), end - begin, gpu_end - gpu_begin);

         timer = hw->timer.time();
      }
   }

   begin = end;
#endif
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

      vk_assert(vkCreateDescriptorSetLayout(logical_device, &info, 0, &set_layout));
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

      vk_assert(vkCreateDescriptorSetLayout(logical_device, &info, 0, &set_layout));
   }

   return set_layout;
}

// TODO: pass all the layouts
static VkPipelineLayout vk_pipeline_layout_create(VkDevice logical_device, VkDescriptorSetLayout* layouts, u32 layout_count, bool rtx_supported)
{
   VkPipelineLayout layout = 0;

   VkPipelineLayoutCreateInfo info = {vk_info(PIPELINE_LAYOUT)};

   VkPushConstantRange push_constants = {};
   if(rtx_supported)
      push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;
   else
      push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

   push_constants.offset = 0;
   push_constants.size = sizeof(mvp_transform);

   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = &push_constants;

   info.setLayoutCount = layout_count;
   info.pSetLayouts = layouts;

   vk_assert(vkCreatePipelineLayout(logical_device, &info, 0, &layout));

   return layout;
}

// TODO: Cleanup these pipelines
static VkPipeline vk_mesh_pipeline_create(VkDevice logical_device, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_device));
   assert(vk_valid_handle(shaders->ms));
   assert(vk_valid_handle(shaders->fs));
   assert(!vk_valid_handle(cache)); // TODO: enable

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
   depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // right handed NDC
   depth_stencil_info.minDepthBounds = 0.0f;
   depth_stencil_info.maxDepthBounds = 1.0f;
   pipeline_info.pDepthStencilState = &depth_stencil_info;

   VkPipelineColorBlendAttachmentState color_blend_attachment = {};
   color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   color_blend_attachment.blendEnable = true;
   // Src color: output color alpha
   color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   // Dst color: one minus source alpha (transparency)
   color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
   // Same for alpha channel blending
   color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
   color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
   color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

   VkPipelineColorBlendStateCreateInfo color_blend_info = {vk_info(PIPELINE_COLOR_BLEND_STATE)};
   color_blend_info.attachmentCount = 1;
   color_blend_info.pAttachments = &color_blend_attachment;
   color_blend_info.logicOpEnable = false;
   color_blend_info.blendConstants[0] = 0.f;
   color_blend_info.blendConstants[1] = 0.f;
   color_blend_info.blendConstants[2] = 0.f;
   color_blend_info.blendConstants[3] = 0.f;

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
   depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // right handed NDC
   depth_stencil_info.minDepthBounds = 0.0f;
   depth_stencil_info.maxDepthBounds = 1.0f;
   pipeline_info.pDepthStencilState = &depth_stencil_info;

   VkPipelineColorBlendAttachmentState color_blend_attachment = {};
   color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   color_blend_attachment.blendEnable = true;
   // Src color: output color alpha
   color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   // Dst color: one minus source alpha (transparency)
   color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
   // Same for alpha channel blending
   color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
   color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
   color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

   VkPipelineColorBlendStateCreateInfo color_blend_info = {vk_info(PIPELINE_COLOR_BLEND_STATE)};
   color_blend_info.attachmentCount = 1;
   color_blend_info.pAttachments = &color_blend_attachment;
   color_blend_info.logicOpEnable = false;
   color_blend_info.blendConstants[0] = 0.f;
   color_blend_info.blendConstants[1] = 0.f;
   color_blend_info.blendConstants[2] = 0.f;
   color_blend_info.blendConstants[3] = 0.f;

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
   depth_stencil_info.depthTestEnable = true;
   depth_stencil_info.depthWriteEnable = true;
   depth_stencil_info.depthBoundsTestEnable = true;
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

// TODO: think about the contracts here
void vk_initialize(hw* hw)
{
   assert(hw->renderer.window.handle);

   VkResult volk_result = volkInitialize();
   if(!vk_valid(volk_result))
      fault(!vk_valid(volk_result));

   vk_context* context = push(&hw->vk_storage, vk_context);

   // app callbacks
   hw->renderer.backends[VULKAN_RENDERER_INDEX] = context;
   hw->renderer.frame_present = vk_present;
   hw->renderer.frame_resize = vk_resize;
   hw->renderer.renderer_index = VULKAN_RENDERER_INDEX;

   context->storage = &hw->vk_storage;

   VkInstance instance = vk_instance_create(*context->storage);
   if(!instance)
      fault(!instance);

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

      messenger_info.pUserData = hw;

      VkDebugUtilsMessengerEXT messenger;

      VkResult debug_result = vk_create_debugutils_messenger_ext(instance, &messenger_info, 0, &messenger);
      if(!vk_valid(volk_result))
         fault(!vk_valid(volk_result));
   }
#endif

   // TODO: allocator
   VkAllocationCallbacks allocator = {};
   context->allocator = allocator;

   // TODO: pass the context only and its components since we are not allowing any other vulkan objects than the ones inside the context
   context->physical_device = vk_physical_device_select(hw, context, *context->storage, instance);
   context->surface = hw->renderer.window_surface_create(instance, hw->renderer.window.handle);
   context->queue_family_index = vk_logical_device_select_family_index(context->physical_device, context->surface, *context->storage);
   context->logical_device = vk_logical_device_create(hw, context->physical_device, *context->storage, context->queue_family_index, context->rtx_supported);
   context->image_ready_semaphore = vk_semaphore_create(context->logical_device);
   context->image_done_semaphore = vk_semaphore_create(context->logical_device);
   context->graphics_queue = vk_graphics_queue_create(context->logical_device, context->queue_family_index);
   context->query_pool_size = 128;
   context->query_pool = vk_query_pool_create(context->logical_device, context->query_pool_size);
   context->command_pool = vk_command_pool_create(context->logical_device, context->queue_family_index);
   context->command_buffer = vk_command_buffer_create(context->logical_device, context->command_pool);
   context->swapchain_surface = vk_swapchain_surface_create(context, hw->renderer.window.width, hw->renderer.window.height, context->queue_family_index);
   context->renderpass = vk_renderpass_create(context->logical_device, context->swapchain_surface.format, VK_FORMAT_D32_SFLOAT);

   // framebuffers
   context->framebuffers.arena = context->storage;
   array_resize(context->framebuffers, context->swapchain_surface.image_count);

   // images
   context->swapchain_images.images.arena = context->storage;
   array_resize(context->swapchain_images.images, context->swapchain_surface.image_count);

   context->swapchain_images.depths.arena = context->storage;
   array_resize(context->swapchain_images.depths, context->swapchain_surface.image_count);

   // views
   context->swapchain_images.image_views.arena = context->storage;
   array_resize(context->swapchain_images.image_views, context->swapchain_surface.image_count);

   context->swapchain_images.depth_views.arena = context->storage;
   array_resize(context->swapchain_images.depth_views, context->swapchain_surface.image_count);

   for(u32 i = 0; i < context->swapchain_surface.image_count; ++i)
   {
      array_add(context->framebuffers, VK_NULL_HANDLE);
      array_add(context->swapchain_images.images, VK_NULL_HANDLE);
      array_add(context->swapchain_images.depths, VK_NULL_HANDLE);

      array_add(context->swapchain_images.image_views, VK_NULL_HANDLE);
      array_add(context->swapchain_images.depth_views, VK_NULL_HANDLE);
   }

   hw->renderer.frame_resize(hw, hw->renderer.window.width, hw->renderer.window.height);

   u32 shader_count = 0;
   const char** shader_names = vk_shader_folder_read(context->storage, s8("bin\\assets\\shaders"));
   for(const char** p = shader_names; *p; ++p)
      shader_count++;

   context->shader_modules.max_count = shader_count;

   spv_lookup(context->logical_device, context->storage, &context->shader_modules, shader_names);

   context->bos = vk_buffer_objects_create(context, hw->state.gltf_file);

   VkDescriptorSetLayout non_rtx_set_layout = vk_pipeline_set_layout_create(context->logical_device, false);
   VkDescriptorSetLayout rtx_set_layout = vk_pipeline_set_layout_create(context->logical_device, true);

   vk_descriptor texture_descriptor = vk_texture_descriptor_create(context, *context->storage, 1<<16);

   VkDescriptorSetLayout set_layouts[2] = {non_rtx_set_layout, texture_descriptor.layout};

   VkPipelineLayout pipeline_layout = vk_pipeline_layout_create(context->logical_device, set_layouts, array_count(set_layouts), false);
   set_layouts[0] = rtx_set_layout; // create rtx layout next
   VkPipelineLayout rtx_pipeline_layout = vk_pipeline_layout_create(context->logical_device, set_layouts, array_count(set_layouts), true);

   VkPipelineCache cache = 0; // TODO: enable

   vk_shader_modules gm = spv_hash_lookup(&context->shader_modules, "graphics");
   context->graphics_pipeline = vk_graphics_pipeline_create(context->logical_device, context->renderpass, cache, pipeline_layout, &gm);

   vk_shader_modules mm = spv_hash_lookup(&context->shader_modules, "meshlet");
   context->rtx_pipeline = vk_mesh_pipeline_create(context->logical_device, context->renderpass, cache, rtx_pipeline_layout, &mm);

   vk_shader_modules am = spv_hash_lookup(&context->shader_modules, "axis");
   context->axis_pipeline = vk_axis_pipeline_create(context->logical_device, context->renderpass, cache, pipeline_layout, &am);

   context->pipeline_layout = pipeline_layout;
   context->rtx_pipeline_layout = rtx_pipeline_layout;
   context->texture_descriptor = texture_descriptor;

   vk_textures_log(context);

   spv_hash_iterate(&context->shader_modules);
}

void vk_uninitialize(hw* hw)
{
   vk_context* context = hw->renderer.backends[VULKAN_RENDERER_INDEX];
   vk_shader_modules mm = spv_hash_lookup(&context->shader_modules, "meshlet");
   vk_shader_modules gm = spv_hash_lookup(&context->shader_modules, "graphics");
   vk_shader_modules am = spv_hash_lookup(&context->shader_modules, "axis");

   assert(mm.ms && mm.fs);
   vkDestroyShaderModule(context->logical_device, mm.ms, 0);
   vkDestroyShaderModule(context->logical_device, mm.fs, 0);
   assert(gm.vs && gm.fs);
   vkDestroyShaderModule(context->logical_device, gm.vs, 0);
   vkDestroyShaderModule(context->logical_device, gm.fs, 0);
   assert(am.vs && am.fs);
   vkDestroyShaderModule(context->logical_device, am.vs, 0);
   vkDestroyShaderModule(context->logical_device, am.fs, 0);

   vkDestroyCommandPool(context->logical_device, context->command_pool, 0);
   vkDestroyQueryPool(context->logical_device, context->query_pool, 0);

   vkDestroySwapchainKHR(context->logical_device, context->swapchain_surface.handle, 0);
   vkDestroyPipeline(context->logical_device, context->axis_pipeline, 0);
   vkDestroyPipeline(context->logical_device, context->frustum_pipeline, 0);
   vkDestroyPipeline(context->logical_device, context->graphics_pipeline, 0);

   vkDestroyBuffer(context->logical_device, context->bos.ib.handle, 0);
   vkDestroyBuffer(context->logical_device, context->bos.vb.handle, 0);
   vkDestroyBuffer(context->logical_device, context->bos.mb.handle, 0);

   vkDestroyRenderPass(context->logical_device, context->renderpass, 0);
   vkDestroySemaphore(context->logical_device, context->image_done_semaphore, 0);
   vkDestroySemaphore(context->logical_device, context->image_ready_semaphore, 0);

   vkDestroySurfaceKHR(context->instance, context->surface, 0);

   // TODO this - must destroy all image buffers etc. before instance
   //vkDestroyDevice(context->logical_device, 0);
   //vkDestroyInstance(context->instance, 0);
}
