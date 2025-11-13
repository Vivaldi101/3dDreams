#include "priority_queue.h"
#include "vulkan_ng.h"

#include "win32_file_io.c"

#include "vulkan_spirv_loader.c"
#include "hash.c"
#include "texture.c"
#include "buffer.c"
#include "gltf.c"
#include "rt.c"

static void obj_file_read_callback(void *ctx, const char *filename, int is_mtl, const char *obj_filename, char **buf, size_t *len)
{
   (void)obj_filename;
   (void)is_mtl;

   obj_user_ctx* user_data = (obj_user_ctx*)ctx;
   arena file_read = {0};

   file_read = win32_file_read(&user_data->scratch, filename);

   *len = arena_left(&file_read);
   *buf = file_read.beg;
}

#if 0
static vk_buffer_objects vk_obj_read(vk_context* context, s8 filename)
{
   vk_buffer_objects result = {0};

   obj_user_ctx user_data = {0};

   tinyobj_shape_t* shapes = 0;
   tinyobj_material_t* materials = 0;
   tinyobj_attrib_t attrib = {0};

   size_t shape_count = 0;
   size_t material_count = 0;

   tinyobj_attrib_init(&attrib);

   array(char) obj_file_path = {context->storage};
   s8 prefix = s8("%s\\assets\\obj\\%s");
   s8 exe_dir = vk_project_directory(context->storage);

   obj_file_path.count = exe_dir.len + prefix.len + filename.len - s8("%s%s").len;
   array_resize(obj_file_path, obj_file_path.count);
   wsprintf(obj_file_path.data, s8_data(prefix), (const char*)exe_dir.data, filename.data);

   // TODO: array(char) to s8
   s8 obj_path = {.data = (u8*)obj_file_path.data, .len = obj_file_path.count};

   assert(strcmp(obj_path.data + obj_path.len - 4, ".obj") == 0);

   array(char) mtl_file_path = {context->storage};
   mtl_file_path.count = exe_dir.len + prefix.len + filename.len - s8("%s%s").len;
   // +2 for zero termination for the .obj string for c-string apis
   array_resize(mtl_file_path, mtl_file_path.count + 2);
   mtl_file_path.data++;
   wsprintf(mtl_file_path.data, s8_data(prefix), (const char*)exe_dir.data, filename.data);

   s8 mtl_path = {.data = (u8*)mtl_file_path.data, .len = mtl_file_path.count};
   mtl_path.data[mtl_path.len-3] = 'm';
   mtl_path.data[mtl_path.len-2] = 't';
   mtl_path.data[mtl_path.len-1] = 'l';

   assert(strcmp(mtl_path.data + mtl_path.len - 4, ".mtl") == 0);

   obj_path.data[obj_path.len] = 0;
   mtl_path.data[mtl_path.len] = 0;

   user_data.mtl_path = mtl_path;

   user_data.scratch = *context->storage;

   if(tinyobj_parse_obj(&attrib, &shapes, &shape_count, &materials, &material_count, s8_data(obj_path), obj_file_read_callback, &user_data, TINYOBJ_FLAG_TRIANGULATE) != TINYOBJ_SUCCESS)
      hw_message_box("Could not load .obj file");

   result = obj_load(context, *context->storage, &attrib);

   tinyobj_materials_free(materials, material_count);
   tinyobj_shapes_free(shapes, shape_count);
   tinyobj_attrib_free(&attrib);

   return result;
}
#endif

static bool vk_gltf_read(vk_context* context, s8 filename)
{
   array(char) file_path = {context->storage};
   s8 prefix = s8("%s\\assets\\gltf\\%s");
   s8 exe_dir = vk_exe_directory(context->storage);

   file_path.count = exe_dir.len + prefix.len + filename.len - s8("%s%s").len;
   array_resize(file_path, file_path.count);
   wsprintf(file_path.data, s8_data(prefix), (const char*)exe_dir.data, filename.data);

   s8 gltf_path = {.data = (u8*)file_path.data, .len = file_path.count};

   assert(s8_equals(s8_slice(gltf_path, gltf_path.len - s8(".gltf").len, gltf_path.len), s8(".gltf")));
   return gltf_load(context, gltf_path);
}

static vk_shader_module vk_shader_load(VkDevice logical_device, arena scratch, const char* shader_name)
{
   vk_shader_module result = {0};

   s8 exe_dir = vk_exe_directory(&scratch);

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

   VkShaderModule module = vk_shader_spv_module_load(logical_device, &scratch, exe_dir, s8(shader_name));

   switch(shader_stage)
   {
      case VK_SHADER_STAGE_MESH_BIT_EXT:
         result.handle = module;
         result.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
         break;
      case VK_SHADER_STAGE_VERTEX_BIT:
         result.handle = module;
         result.stage = VK_SHADER_STAGE_VERTEX_BIT;
         break;
      case VK_SHADER_STAGE_FRAGMENT_BIT:
         result.handle = module;
         result.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
         break;
      default: break;
   }

   return result;
}

static bool spirv_initialize(vk_context* context)
{
   u32 shader_count = 0;
   const char** shader_names = vk_shader_folder_read(context->storage, s8("bin\\assets\\shaders"));
   if(!shader_names)
      return false;

   for(const char** p = shader_names; *p; ++p)
      shader_count++;

   spv_hash_table* table = &context->shader_table;

   table->max_count = shader_count;

   // TODO: make function for hash tables
   table->keys = push(context->storage, const char*, table->max_count);
   table->values = push(context->storage, vk_shader_module, table->max_count);

   memset(table->values, 0, table->max_count * sizeof(vk_shader_module));

   for(size i = 0; i < table->max_count; ++i)
      table->keys[i] = 0;

   for(const char** p = shader_names; p && *p; ++p)
   {
      usize shader_len = strlen(*p);
      const char* shader_name = *p;

      // TODO: use s8 substring
      for(usize i = 0; i < shader_len; ++i)
      {
         // TODO: use s8 equals
         if(strncmp(shader_name + i, meshlet_module_name, strlen(meshlet_module_name)) == 0)
         {
            vk_shader_module mm = vk_shader_load(context->devices.logical, *context->storage, *p);
            if (mm.stage == VK_SHADER_STAGE_MESH_BIT_EXT)
               spv_hash_insert(table, meshlet_module_name"_ms", mm);
            else if (mm.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
               spv_hash_insert(table, meshlet_module_name"_fs", mm);
            break;
         }
         if(strncmp(shader_name + i, graphics_module_name, strlen(graphics_module_name)) == 0)
         {
            vk_shader_module gm = vk_shader_load(context->devices.logical, *context->storage, *p);
            if(gm.stage == VK_SHADER_STAGE_VERTEX_BIT)
               spv_hash_insert(table, graphics_module_name"_vs", gm);
            else if(gm.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
               spv_hash_insert(table, graphics_module_name"_fs", gm);
            break;
         }
         if(strncmp(shader_name + i, axis_module_name, strlen(axis_module_name)) == 0)
         {
            vk_shader_module am = vk_shader_load(context->devices.logical, *context->storage, *p);
            if(am.stage == VK_SHADER_STAGE_VERTEX_BIT)
               spv_hash_insert(table, axis_module_name"_vs", am);
            else if(am.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
               spv_hash_insert(table, axis_module_name"_fs", am);
            break;
         }
         if(strncmp(shader_name + i, frustum_module_name, strlen(frustum_module_name)) == 0)
         {
            vk_shader_module fm = vk_shader_load(context->devices.logical, *context->storage, *p);
            if(fm.stage == VK_SHADER_STAGE_VERTEX_BIT)
               spv_hash_insert(table, frustum_module_name"_vs", fm);
            else if(fm.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
               spv_hash_insert(table, frustum_module_name"_fs", fm);
            break;
         }
      }
   }

   return true;
}

static bool vk_assets_read(vk_context* context, s8 asset_file)
{
   bool success = false;
   if (s8_is_substr(asset_file, s8(".gltf")))
      success = vk_gltf_read(context, asset_file);
   else if (s8_is_substr(asset_file, s8(".obj")))
      ;
      //vk_obj_read(context, asset_file);
   else
      printf("Unsupported asset format: %s\n", s8_data(asset_file));

   return success;
}

static bool vk_create_debugutils_messenger_ext(VkDebugUtilsMessengerEXT* debug_messenger, VkInstance instance,
                                                   VkDebugUtilsMessengerCreateInfoEXT* create_info)
{
   PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
   if(!func)
      return false;

   if(!vk_valid(func(instance, create_info, 0, debug_messenger)))
      return false;

   return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_flags,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user_data)
{
   (void)severity_flags;
   (void)type;
   (void)user_data;
#if _DEBUG
   printf("VALIDATION LAYER MESSAGE: %s\n", data->pMessage);
#else
   (void)data;
#endif
#ifdef vk_break_on_validation
   assert((type & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0);
#endif

   return false;
}

static VkFormat vk_swapchain_format(arena scratch, VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_device));
   assert(vk_valid_handle(surface));

   array(VkSurfaceFormatKHR) formats = {&scratch};

   u32 format_count = 0;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, 0)))
      return VK_FORMAT_UNDEFINED;

   array_resize(formats, format_count);

   if(!vk_valid(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data)))
      return VK_FORMAT_UNDEFINED;

   formats.count = format_count;
   if(formats.count == 0)
      return VK_FORMAT_UNDEFINED;

   for(size i = 0; i < formats.count; ++i)
      if (formats.data[i].format == VK_FORMAT_R8G8B8A8_UNORM)
         return formats.data[i].format;

   return formats.data[0].format;
}

static vk_swapchain_surface vk_window_swapchain_surface_create(arena scratch, VkPhysicalDevice physical_device, u32 width, u32 height, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_device));
   assert(vk_valid_handle(surface));

   vk_swapchain_surface result = {0};

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
   result.format = vk_swapchain_format(scratch, physical_device, surface);

   return result;
}

static VkPhysicalDevice vk_physical_device_select(vk_context* context)
{
   u32 dev_count = 0;
   vk_assert(vkEnumeratePhysicalDevices(context->instance, &dev_count, 0));

   arena scratch = *context->storage;
   VkPhysicalDevice* devs = push(&scratch, VkPhysicalDevice, dev_count);
   vk_assert(vkEnumeratePhysicalDevices(context->instance, &dev_count, devs));

   u32 fallback_gpu = 0;
   for(u32 i = 0; i < dev_count; ++i)
   {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devs[i], &props);

      // match version
      if(props.apiVersion < VK_API_VERSION_1_3)
         continue;

      // timestamps
      if(!props.limits.timestampComputeAndGraphics)
         continue;

      // dedicated gpu
      if(!props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
         continue;

      fallback_gpu = i;
      context->time_period = props.limits.timestampPeriod;
      u32 extension_count = 0;
      vk_assert(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, 0));

      VkExtensionProperties* extensions = push(&scratch, VkExtensionProperties, extension_count);
      vk_assert(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, extensions));

      for(u32 j = 0; j < extension_count; ++j)
      {
         s8 e = s8(extensions[j].extensionName);
         printf("Available Vulkan device extension[%u]: %s\n", j, s8_data(e));

         if(s8_equals(e, s8(VK_EXT_MESH_SHADER_EXTENSION_NAME)))
            context->mesh_shading_supported = true;

         if(s8_equals(e, s8(VK_KHR_RAY_QUERY_EXTENSION_NAME)))
            context->raytracing_supported = true;

         if(context->raytracing_supported && context->mesh_shading_supported)
         {
            printf("Ray tracing supported\n");
            printf("Mesh shading supported\n");

            return devs[i];
         }
      }
   }

   return devs[fallback_gpu];
}

static u32 vk_logical_device_select_family_index(vk_context* context, arena scratch)
{
   u32 queue_family_count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(context->devices.physical, &queue_family_count, 0);

   VkQueueFamilyProperties* queue_families = push(&scratch, VkQueueFamilyProperties, queue_family_count);
   vkGetPhysicalDeviceQueueFamilyProperties(context->devices.physical, &queue_family_count, queue_families);

   for(u32 i = 0; i < queue_family_count; i++)
   {
      VkBool32 present_support = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(context->devices.physical, i, context->surface, &present_support);

      // graphics and presentation within same queue for simplicity
      if((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support)
         return i;
   }

   // try to use the first as a last fallback
   return 0;
}

static VkDevice vk_logical_device_create(vk_context* context, arena scratch)
{
   array(s8) extensions = {&scratch};

   array_push(extensions) = s8(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
   array_push(extensions) = s8(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
   array_push(extensions) = s8(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);

   if(context->mesh_shading_supported)
   {
      array_push(extensions) = s8(VK_EXT_MESH_SHADER_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
   }
   if(context->raytracing_supported)
   {
      array_push(extensions) = s8(VK_KHR_RAY_QUERY_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
   }

   VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

   VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};

   VkPhysicalDeviceMultiviewFeatures features_multiview = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};

   VkPhysicalDeviceFragmentShadingRateFeaturesKHR features_frag_shading = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};

   VkPhysicalDeviceMeshShaderFeaturesEXT features_mesh_shader = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

   VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};

   VkPhysicalDeviceRayQueryFeaturesKHR ray_query = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

   features12.storageBuffer8BitAccess = true;
   features12.uniformAndStorageBuffer8BitAccess = true;
   features12.storagePushConstant8 = true;
   features12.shaderSampledImageArrayNonUniformIndexing = true;
   features12.descriptorBindingSampledImageUpdateAfterBind = true;
   features12.descriptorBindingUpdateUnusedWhilePending = true;
   features12.descriptorBindingPartiallyBound = true;
   features12.bufferDeviceAddress = true;

   features_multiview.multiview = true;

   features_frag_shading.primitiveFragmentShadingRate = true;

   features.pNext = &features12;
   features12.pNext = &features_multiview;
   features_multiview.pNext = &features_frag_shading;

   if(context->mesh_shading_supported)
   {
      features_mesh_shader.pNext = features.pNext;
      features.pNext = &features_mesh_shader;

      features_mesh_shader.meshShader = true;
      features_mesh_shader.taskShader = true;
      features_mesh_shader.multiviewMeshShader = true;
   }

   if(context->raytracing_supported)
   {
      ray_query.pNext = &acceleration_features;
      acceleration_features.pNext = features.pNext;
      features.pNext = &ray_query;

      ray_query.rayQuery = true;
      acceleration_features.accelerationStructure = true;
   }

   vkGetPhysicalDeviceFeatures2(context->devices.physical, &features);

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

   enum { queue_count = 1 };
   f32 priorities[queue_count] = {1.0f};
   VkDeviceQueueCreateInfo queue_info[queue_count] = {vk_info(DEVICE_QUEUE)};

   queue_info[0].queueFamilyIndex = context->queue_family_index; // TODO: query the right queue family
   queue_info[0].queueCount = queue_count;
   queue_info[0].pQueuePriorities = priorities;

   VkDeviceCreateInfo ldev_info = {vk_info(DEVICE)};

   ldev_info.queueCreateInfoCount = 1;
   ldev_info.pQueueCreateInfos = queue_info;
   ldev_info.enabledExtensionCount = (u32)extensions.count;
   ldev_info.ppEnabledExtensionNames = extension_names;
   ldev_info.pNext = &features;

   VkDevice logical_device;
   vk_assert(vkCreateDevice(context->devices.physical, &ldev_info, 0, &logical_device));

   return logical_device;
}

static VkQueue vk_graphics_queue_create(vk_context* context)
{
   assert(vk_valid_handle(context->devices.logical));

   VkQueue graphics_queue = 0;
   u32 queue_index = 0;

   // TODO: Get the queue index
   vkGetDeviceQueue(context->devices.logical, context->queue_family_index, queue_index, &graphics_queue);

   return graphics_queue;
}

static VkSemaphore vk_semaphore_create(vk_context* context)
{
   assert(vk_valid_handle(context->devices.logical));

   VkSemaphore sema = 0;

   VkSemaphoreCreateInfo sema_info = {vk_info(SEMAPHORE)};
   vk_assert(vkCreateSemaphore(context->devices.logical, &sema_info, 0, &sema));

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

static vk_swapchain_surface vk_swapchain_surface_create(vk_context* context, u32 swapchain_width, u32 swapchain_height)
{
   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->devices.physical, context->surface, &surface_caps)))
      return (vk_swapchain_surface) {0};

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

   vk_swapchain_surface swapchain_info = vk_window_swapchain_surface_create(*context->storage, context->devices.physical, swapchain_extent.width, swapchain_extent.height, context->surface);

   swapchain_info.handle = vk_swapchain_create(context->devices.logical, context->surface, &swapchain_info, context->queue_family_index);

   return swapchain_info;
}

static VkCommandBuffer vk_command_buffer_create(vk_context* context)
{
   assert(vk_valid_handle(context->devices.logical));
   assert(vk_valid_handle(context->command_pool));

   VkCommandBuffer buffer = 0;
   VkCommandBufferAllocateInfo buffer_allocate_info = {vk_info_allocate(COMMAND_BUFFER)};

   buffer_allocate_info.commandBufferCount = 1;
   buffer_allocate_info.commandPool = context->command_pool;
   buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

   vk_assert(vkAllocateCommandBuffers(context->devices.logical, &buffer_allocate_info, &buffer));

   return buffer;
}

static VkCommandPool vk_command_pool_create(vk_context* context)
{
   assert(vk_valid_handle(context->devices.logical));

   VkCommandPool pool = 0;

   VkCommandPoolCreateInfo pool_info = {vk_info(COMMAND_POOL)};
   pool_info.queueFamilyIndex = context->queue_family_index;

   vk_assert(vkCreateCommandPool(context->devices.logical, &pool_info, 0, &pool));

   return pool;
}


static VkRenderPass vk_renderpass_create(vk_context* context)
{
   assert(vk_valid_handle(context->devices.logical));
   assert(vk_valid_format(context->swapchain_surface.format));

   VkRenderPass renderpass = 0;

   const u32 color_attachment_index = 0;
   const u32 depth_attachment_index = 1;

   VkAttachmentReference color_attachment_ref = {color_attachment_index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; 
   VkAttachmentReference depth_attachment_ref = {depth_attachment_index, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; 

   VkSubpassDescription subpass = {0};
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_attachment_ref;

   subpass.pDepthStencilAttachment = &depth_attachment_ref;

   VkAttachmentDescription attachments[2] = {0};

   attachments[color_attachment_index].format = context->swapchain_surface.format;
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

   vk_assert(vkCreateRenderPass(context->devices.logical, &renderpass_info, 0, &renderpass));

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
   vkDeviceWaitIdle(context->devices.logical);

   for(u32 i = 0; i < context->framebuffers.count; ++i)
      vkDestroyFramebuffer(context->devices.logical, context->framebuffers.data[i], 0);

   for (u32 i = 0; i < context->swapchain_images.depths.count; ++i)
   {
      vkDestroyImageView(context->devices.logical, context->swapchain_images.depths.data[i].view, 0);
      vkDestroyImage(context->devices.logical, context->swapchain_images.depths.data[i].handle, 0);
      vkFreeMemory(context->devices.logical, context->swapchain_images.depths.data[i].memory, 0);
   }

   for (u32 i = 0; i < context->swapchain_images.images.count; ++i)
      vkDestroyImageView(context->devices.logical, context->swapchain_images.images.data[i].view, 0);

   vkDestroySwapchainKHR(context->devices.logical, context->swapchain_surface.handle, 0);
}

// TODO: break into separate routines
// TODO: wide contract
static void vk_swapchain_update(vk_context* context)
{
   arena scratch = *context->storage;

   VkImage* swapchain_images = push(&scratch, VkImage, context->swapchain_surface.image_count);

   vk_assert(vkGetSwapchainImagesKHR(context->devices.logical, context->swapchain_surface.handle, &context->swapchain_surface.image_count, swapchain_images));

   VkExtent3D depth_extent = {context->swapchain_surface.image_width, context->swapchain_surface.image_height, 1};

   for(u32 i = 0; i < context->swapchain_surface.image_count; ++i)
   {
      context->swapchain_images.images.data[i].handle = swapchain_images[i];

      vk_depth_image_create(&context->swapchain_images.depths.data[i], context, VK_FORMAT_D32_SFLOAT, depth_extent); // TDOO: Return false here if fail

      context->swapchain_images.images.data[i].view = vk_image_view_create(context, context->swapchain_surface.format, context->swapchain_images.images.data[i].handle, VK_IMAGE_ASPECT_COLOR_BIT);
      context->swapchain_images.depths.data[i].view = vk_image_view_create(context, VK_FORMAT_D32_SFLOAT, context->swapchain_images.depths.data[i].handle, VK_IMAGE_ASPECT_DEPTH_BIT);

      VkImageView attachments[2] = {context->swapchain_images.images.data[i].view, context->swapchain_images.depths.data[i].view};

      VkFramebuffer fb = vk_framebuffer_create(context->devices.logical, context->renderpass, &context->swapchain_surface, attachments, array_count(attachments));
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

   mvp_transform mvp = {0};
   const f32 ar = (f32)width / height;

   mvp.n = 0.01f;
   mvp.f = 10000.0f;
   mvp.ar = ar;

   mvp.projection = mat4_perspective(ar, 75.0f, mvp.n, mvp.f);
   hw->renderer.mvp = mvp;

   vkDeviceWaitIdle(context->devices.logical);

   vk_swapchain_destroy(context);
   context->swapchain_surface = vk_swapchain_surface_create(context, width, height);
   vk_swapchain_update(context);

   printf("Viewport resized: [%u %u]\n", width, height);
}

static VkQueryPool vk_query_pool_create(vk_context* context)
{
   VkQueryPool result = 0;

   context->query_pool_size = 128;

   VkQueryPoolCreateInfo info = {vk_info(QUERY_POOL)};
   info.queryType = VK_QUERY_TYPE_TIMESTAMP;
   info.queryCount = context->query_pool_size;

   vk_assert(vkCreateQueryPool(context->devices.logical, &info, 0, &result));

   return result;
}

// TODO: these into cmd.c
static void cmd_push_all_constants(VkCommandBuffer command_buffer, VkPipelineLayout layout, mvp_transform* mvp)
{
   vkCmdPushConstants(command_buffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*mvp), mvp);
}

static void cmd_push_rtx_constants(VkCommandBuffer command_buffer, VkPipelineLayout layout, mvp_transform* mvp, u32 offset)
{
   vkCmdPushConstants(command_buffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, offset, sizeof(*mvp), mvp);
}

static void cmd_push_all_rtx_constants(VkCommandBuffer command_buffer, VkPipelineLayout layout, mvp_transform* mvp)
{
   cmd_push_rtx_constants(command_buffer, layout, mvp, 0);
}

static VkDescriptorBufferInfo cmd_buffer_descriptor_create(vk_buffer* buffer)
{
   assert(buffer->handle && buffer->size > 0);

   VkDescriptorBufferInfo result = {0};
   result.buffer = buffer->handle;
   result.range = buffer->size;
   result.offset = 0;

   return result;
}

static VkWriteDescriptorSet cmd_write_descriptor_create(u32 binding, VkDescriptorType type, VkDescriptorBufferInfo* buffer_info, void* extras)
{
   VkWriteDescriptorSet result = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

   VkWriteDescriptorSetAccelerationStructureKHR acceleration_write_descriptor =
   {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};

   if(type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
   {
      acceleration_write_descriptor.pAccelerationStructures = extras;
      acceleration_write_descriptor.accelerationStructureCount = 1;
      result.pNext = &acceleration_write_descriptor;
   }

   result.dstBinding = binding;
   result.dstSet = VK_NULL_HANDLE;
   result.descriptorCount = 1;
   result.descriptorType = type;
   result.pBufferInfo = buffer_info;

   return result;
}

static void cmd_push_storage_buffer(VkCommandBuffer command_buffer, arena scratch, VkPipelineLayout layout, vk_buffer_binding* bindings, u32 binding_count, u32 set_number)
{
   VkWriteDescriptorSet* write_sets = push(&scratch, VkWriteDescriptorSet, binding_count);
   VkDescriptorBufferInfo* infos = push(&scratch, VkDescriptorBufferInfo, binding_count);

   VkWriteDescriptorSetAccelerationStructureKHR acceleration_write_descriptor =
   {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};

   for(u32 i = 0; i < binding_count; ++i)
   {
      infos[i] = cmd_buffer_descriptor_create(&bindings[i].buffer);

      VkWriteDescriptorSet set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

      if(bindings[i].type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
      {
         acceleration_write_descriptor.pAccelerationStructures = bindings[i].extras;
         acceleration_write_descriptor.accelerationStructureCount = 1;
         set.pNext = &acceleration_write_descriptor;
      }

      set.dstBinding = bindings[i].binding;
      set.dstSet = VK_NULL_HANDLE;
      set.descriptorCount = 1;
      set.descriptorType = bindings[i].type;
      set.pBufferInfo = &infos[i];

      write_sets[i] = set;
   }

   vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, set_number, binding_count, write_sets);
}

static void cmd_bind_index_buffer(VkCommandBuffer command_buffer, VkBuffer buffer, VkDeviceSize offset)
{
   vkCmdBindIndexBuffer(command_buffer, buffer, offset, VK_INDEX_TYPE_UINT32);
}

static void cmd_bind_pipeline(VkCommandBuffer command_buffer, VkPipeline pipeline)
{
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

static void cmd_bind_descriptor_set(VkCommandBuffer command_buffer, VkPipelineLayout layout, VkDescriptorSet* sets, u32 set, u32 set_count)
{
   vkCmdBindDescriptorSets(command_buffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      layout,
      set,
      set_count,
      sets,
      0,
      0
   );
}

static void vk_present(hw* hw, vk_context* context)
{
   VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
   present_info.swapchainCount = 1;
   present_info.pSwapchains = &context->swapchain_surface.handle;
   present_info.pImageIndices = &context->image_index;
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
   vk_assert(vkDeviceWaitIdle(context->devices.logical));
}

static void vk_render(hw* hw, vk_context* context, app_state* state)
{
   u32 image_index = 0;
   VkResult next_image_result = vkAcquireNextImageKHR(context->devices.logical, context->swapchain_surface.handle, UINT64_MAX, context->image_ready_semaphore, VK_NULL_HANDLE, &image_index);

   context->image_index = image_index;

   if(next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(hw, context->swapchain_surface.image_width, context->swapchain_surface.image_height);

   if(next_image_result != VK_SUBOPTIMAL_KHR && next_image_result != VK_SUCCESS)
      return;

   vk_assert(vkResetCommandPool(context->devices.logical, context->command_pool, 0));

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

   mvp_transform mvp = hw->renderer.mvp;
   assert(mvp.n > 0.0f);
   assert(mvp.ar != 0.0f);

   // world space origin
   vec3 eye = state->camera.eye;
   vec3 dir = state->camera.dir;
   vec3_normalize(dir);

   mvp.view = mat4_view(eye, dir);

   //mvp.world = mat4_identity();

   VkClearValue clear[2] = {0};
   //clear[0].color = (VkClearColorValue){68.f / c, 10.f / c, 36.f / c, 1.0f};
   //clear[0].color = (VkClearColorValue){1.f, 1.f, 1.f};
   clear[0].color = (VkClearColorValue){0.19f, 0.19f, 0.19f};
   clear[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};

   renderpass_info.clearValueCount = 2;
   renderpass_info.pClearValues = clear;

   VkImage color_image = context->swapchain_images.images.data[image_index].handle;
   VkImageMemoryBarrier color_image_begin_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_begin_barrier);

   VkImage depth_image = context->swapchain_images.depths.data[image_index].handle;
   VkImageMemoryBarrier depth_image_begin_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_begin_barrier);


   vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

   VkViewport viewport = {0};

   // y-is-up
   viewport.x = 0.0f;
   viewport.y = (f32)context->swapchain_surface.image_height;
   viewport.width = (f32)context->swapchain_surface.image_width;
   viewport.height = -(f32)context->swapchain_surface.image_height;

   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;

   VkRect2D scissor = {0};
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   scissor.extent.width = (u32)context->swapchain_surface.image_width;
   scissor.extent.height = (u32)context->swapchain_surface.image_height;

   vkCmdSetViewport(command_buffer, 0, 1, &viewport);
   vkCmdSetScissor(command_buffer, 0, 1, &scissor);
   vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

   if(state->is_mesh_shading)
   {
      VkPipeline pipeline = context->rtx_pipeline;
      VkPipelineLayout pipeline_layout = context->rtx_pipeline_layout;

      cmd_bind_descriptor_set(command_buffer, pipeline_layout, &context->texture_descriptor.set, 1, 1);
      cmd_bind_pipeline(command_buffer, pipeline);

      arena s = *context->storage;
      array(vk_buffer_binding) bindings = {&s};

      if(buffer_hash_lookup(&context->buffer_table, vb_buffer_name))
      {
         vk_buffer buffer = *buffer_hash_lookup(&context->buffer_table, vb_buffer_name);
         array_push(bindings) = (vk_buffer_binding){buffer, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
      }

      if(buffer_hash_lookup(&context->buffer_table, mb_buffer_name))
      {
         vk_buffer buffer = *buffer_hash_lookup(&context->buffer_table, mb_buffer_name);
         array_push(bindings) = (vk_buffer_binding){buffer, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
      }

      if(buffer_hash_lookup(&context->buffer_table, mesh_draw_buffer_name))
      {
         vk_buffer buffer = *buffer_hash_lookup(&context->buffer_table, mesh_draw_buffer_name);
         array_push(bindings) = (vk_buffer_binding){buffer, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
      }

      if(buffer_hash_lookup(&context->buffer_table, rt_buffer_name))
      {
         vk_buffer buffer = *buffer_hash_lookup(&context->buffer_table, rt_buffer_name);
         array_push(bindings) = (vk_buffer_binding){buffer, 3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, &context->tlas};
      }

      cmd_push_storage_buffer(command_buffer, s, pipeline_layout, bindings.data, (u32)bindings.count, 0);
      cmd_push_all_rtx_constants(command_buffer, pipeline_layout, &mvp);

      if(buffer_hash_lookup(&context->buffer_table, indirect_rtx_buffer_name))
         vkCmdDrawMeshTasksIndirectEXT(command_buffer,
                                       buffer_hash_lookup(&context->buffer_table, indirect_rtx_buffer_name)->handle,
                                       0, (u32)context->geometry.mesh_draws.count,
                                       sizeof(VkDrawMeshTasksIndirectCommandEXT));
   }
   else
   {
      VkPipeline pipeline = context->non_rtx_pipeline;
      VkPipelineLayout pipeline_layout = context->non_rtx_pipeline_layout;

      cmd_bind_descriptor_set(command_buffer, pipeline_layout, &context->texture_descriptor.set, 1, 1);
      cmd_bind_pipeline(command_buffer, pipeline);
      if(buffer_hash_lookup(&context->buffer_table, ib_buffer_name))
         cmd_bind_index_buffer(command_buffer, buffer_hash_lookup(&context->buffer_table, ib_buffer_name)->handle, 0);

      arena s = *context->storage;
      array(vk_buffer_binding) bindings = {&s};

      if(buffer_hash_lookup(&context->buffer_table, vb_buffer_name))
      {
         vk_buffer buffer = *buffer_hash_lookup(&context->buffer_table, vb_buffer_name);
         array_push(bindings) = (vk_buffer_binding){buffer, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
      }

      if(buffer_hash_lookup(&context->buffer_table, mesh_draw_buffer_name))
      {
         vk_buffer buffer = *buffer_hash_lookup(&context->buffer_table, mesh_draw_buffer_name);
         array_push(bindings) = (vk_buffer_binding){buffer, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
      }

      cmd_push_storage_buffer(command_buffer, s, pipeline_layout, bindings.data, (u32)bindings.count, 0);
      cmd_push_all_constants(command_buffer, pipeline_layout, &mvp);

      if(buffer_hash_lookup(&context->buffer_table, indirect_buffer_name))
         vkCmdDrawIndexedIndirect(command_buffer,
                                  buffer_hash_lookup(&context->buffer_table, indirect_buffer_name)->handle,
                                  0, (u32)context->geometry.mesh_draws.count,
                                  sizeof(VkDrawIndexedIndirectCommand));

      vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);

      mvp.draw_ground_plane = 1;

      cmd_push_all_constants(command_buffer, pipeline_layout, &mvp);

      // draw ground plane
      vkCmdDraw(command_buffer, 4, 1, 0, 0);

      vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
   }

   // draw frustum
   //vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->frustum_pipeline);
   //vkCmdDraw(command_buffer, 12, 1, 0, 0);

   if(state->draw_axis)
   {
      // draw axis
      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->axis_pipeline);
      vkCmdDraw(command_buffer, 18, 1, 0, 0);
   }

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

   u64 query_results[2];
   vk_assert(vkGetQueryPoolResults(context->devices.logical, context->query_pool, 0, array_count(query_results), sizeof(query_results), query_results, sizeof(query_results[0]), VK_QUERY_RESULT_64_BIT));

#if 1
   f64 gpu_begin = (f64)query_results[0] * context->time_period * 1e-6;
   f64 gpu_end = (f64)query_results[1] * context->time_period * 1e-6;

   {
      // frame logs
      // TODO: this should really be in app.c
      if(1)
      {
         if(hw->state.is_mesh_shading)
            hw->window_title(hw, s8("gpu: %.2f ms; #Meshlets: %u; Press 'm' to toggle RTX; RTX ON"), gpu_end - gpu_begin, context->meshlets.count);
         else
            hw->window_title(hw, s8("gpu: %.2f ms; #Meshlets: 0; Press 'm' to toggle RTX; RTX OFF"), gpu_end - gpu_begin);
      }
   }
#endif
}

// TODO: pass amount of bindings to create here
// TODO: dont pass any booleans?
static bool vk_pipeline_set_layout_create(VkDescriptorSetLayout* set_layout, VkDevice device, bool is_rtx)
{
   // TODO: cleanup this nonsense
   if(is_rtx)
   {
      VkDescriptorSetLayoutBinding bindings[4] = {0};
      bindings[0].binding = 0;
      bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[0].descriptorCount = 1;
      bindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

      bindings[1].binding = 1;
      bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[1].descriptorCount = 1;
      bindings[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

      bindings[2].binding = 2;
      bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[2].descriptorCount = 1;
      bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;

      bindings[3].binding = 3;
      bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      bindings[3].descriptorCount = 1;
      bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

      VkDescriptorSetLayoutCreateInfo info = {vk_info(DESCRIPTOR_SET_LAYOUT)};

      info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
      info.bindingCount = array_count(bindings);
      info.pBindings = bindings;

      if(!vk_valid(vkCreateDescriptorSetLayout(device, &info, 0, set_layout)))
         return false;
   }
   else
   {
      VkDescriptorSetLayoutBinding bindings[2] = {0};
      bindings[0].binding = 0;
      bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[0].descriptorCount = 1;
      bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

      bindings[1].binding = 1;
      bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[1].descriptorCount = 1;
      bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

      VkDescriptorSetLayoutCreateInfo info = {vk_info(DESCRIPTOR_SET_LAYOUT)};

      info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
      info.bindingCount = array_count(bindings);
      info.pBindings = bindings;

      if(!vk_valid(vkCreateDescriptorSetLayout(device, &info, 0, set_layout)))
         return false;
   }

   return true;
}

static bool vk_pipeline_layout_create(VkPipelineLayout* layout, VkDevice logical_device, VkDescriptorSetLayout* set_layouts, size set_layout_count, bool mesh_shading_supported)
{
   VkPipelineLayoutCreateInfo info = {vk_info(PIPELINE_LAYOUT)};

   VkPushConstantRange push_constants = {0};
   if(mesh_shading_supported)
      push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;
   else
      push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

   push_constants.offset = 0;
   push_constants.size = sizeof(mvp_transform);

   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = &push_constants;

   info.setLayoutCount = (u32)set_layout_count;
   info.pSetLayouts = set_layouts;

   if(!vk_valid(vkCreatePipelineLayout(logical_device, &info, 0, layout)))
      return false;

   return true;
}

static VkPipelineShaderStageCreateInfo vk_shader_stage_create_info(vk_shader_module shader_module)
{
   assert(shader_module.handle);
   assert(shader_module.stage);

   VkPipelineShaderStageCreateInfo result =
   {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = shader_module.stage,
      .module = shader_module.handle,
      .pName = "main"
   };

   return result;
}

static bool vk_mesh_pipeline_create(VkPipeline* pipeline, vk_context* context, VkPipelineCache cache, const vk_shader_module* shader_modules, size shader_module_count)
{
   arena scratch = *context->storage;

   array(VkPipelineShaderStageCreateInfo) stages = {&scratch};

   for (size i = 0; i < shader_module_count; ++i)
      array_push(stages) = vk_shader_stage_create_info(shader_modules[i]);

   VkGraphicsPipelineCreateInfo pipeline_info = {vk_info(GRAPHICS_PIPELINE)};
   pipeline_info.stageCount = (u32)stages.count;
   pipeline_info.pStages = stages.data;

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

   VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
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

   pipeline_info.renderPass = context->renderpass;
   pipeline_info.layout = context->rtx_pipeline_layout;

   if(!vk_valid(vkCreateGraphicsPipelines(context->devices.logical, cache, 1, &pipeline_info, 0, pipeline)))
      return false;

   return true;
}

static bool vk_graphics_pipeline_create(VkPipeline* pipeline, vk_context* context, VkPipelineCache cache, const vk_shader_module* shader_modules, size shader_module_count)
{
   arena scratch = *context->storage;

   array(VkPipelineShaderStageCreateInfo) stages = {&scratch};

   for (size i = 0; i < shader_module_count; ++i)
      array_push(stages) = vk_shader_stage_create_info(shader_modules[i]);

   VkGraphicsPipelineCreateInfo pipeline_info = {vk_info(GRAPHICS_PIPELINE)};
   pipeline_info.stageCount = (u32)stages.count;
   pipeline_info.pStages = stages.data;

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

   VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
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
   dynamic_info.pDynamicStates = (VkDynamicState[4]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};
   dynamic_info.dynamicStateCount = 4;
   pipeline_info.pDynamicState = &dynamic_info;

   pipeline_info.renderPass = context->renderpass;
   pipeline_info.layout = context->non_rtx_pipeline_layout;

   if(!vk_valid(vkCreateGraphicsPipelines(context->devices.logical, cache, 1, &pipeline_info, 0, pipeline)))
      return false;

   return true;
}

static bool vk_axis_pipeline_create(VkPipeline* pipeline, vk_context* context, VkPipelineCache cache, const vk_shader_module* shader_modules, size shader_module_count)
{
   arena scratch = *context->storage;

   array(VkPipelineShaderStageCreateInfo) stages = {&scratch};

   for (size i = 0; i < shader_module_count; ++i)
      array_push(stages) = vk_shader_stage_create_info(shader_modules[i]);

   VkGraphicsPipelineCreateInfo pipeline_info = {vk_info(GRAPHICS_PIPELINE)};
   pipeline_info.stageCount = (u32)stages.count;
   pipeline_info.pStages = stages.data;

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

   VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
   color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   VkPipelineColorBlendStateCreateInfo color_blend_info = {vk_info(PIPELINE_COLOR_BLEND_STATE)};
   color_blend_info.attachmentCount = 1;
   color_blend_info.pAttachments = &color_blend_attachment;
   pipeline_info.pColorBlendState = &color_blend_info;

   VkPipelineDynamicStateCreateInfo dynamic_info = {vk_info(PIPELINE_DYNAMIC_STATE)};
   dynamic_info.pDynamicStates = (VkDynamicState[3]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   dynamic_info.dynamicStateCount = 2;

   pipeline_info.pDynamicState = &dynamic_info;
   pipeline_info.renderPass = context->renderpass;
   pipeline_info.layout = context->non_rtx_pipeline_layout;

   if(!vk_valid(vkCreateGraphicsPipelines(context->devices.logical, cache, 1, &pipeline_info, 0, pipeline)))
      return false;

   return true;
}

bool vk_instance_create(VkInstance* instance, arena scratch)
{
   u32 ext_count = 0;
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, 0)))
      return false;

   VkExtensionProperties* extensions = push(&scratch, VkExtensionProperties, ext_count);
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, extensions)))
      return false;

   const char** ext_names = push(&scratch, const char*, ext_count);

   for(size_t i = 0; i < ext_count; ++i)
   {
      ext_names[i] = extensions[i].extensionName;
      printf("Instance extension: [%zu]: %s\n", i, ext_names[i]);
   }

   VkInstanceCreateInfo instance_info = {vk_info(INSTANCE)};
   instance_info.pApplicationInfo = &(VkApplicationInfo) { .apiVersion = VK_API_VERSION_1_3 };

   instance_info.enabledExtensionCount = ext_count;
   instance_info.ppEnabledExtensionNames = ext_names;

#ifdef _DEBUG
   {
      const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
      instance_info.enabledLayerCount = array_count(validation_layers);
      instance_info.ppEnabledLayerNames = validation_layers;
   }
#endif
   if(!vk_valid(vkCreateInstance(&instance_info, 0, instance)))
      return false;

   return true;
}

static bool vk_buffers_create(vk_context* context)
{
   vk_buffer indirect_buffer = {0};
   vk_buffer indirect_rtx_buffer = {0};
   vk_buffer mesh_draw_buffer = {0};
   vk_buffer rt_buffer = {0};

   arena s = *context->storage;

   // TODO: pass devices and geometry
   if(!buffer_indirect_create(&indirect_buffer, context, s, false))
      return false;

   buffer_hash_insert(&context->buffer_table, indirect_buffer_name, indirect_buffer);

   if(!buffer_indirect_create(&indirect_rtx_buffer, context, s, true))
      return false;

   buffer_hash_insert(&context->buffer_table, indirect_rtx_buffer_name, indirect_rtx_buffer);

   if(!buffer_draws_create(&mesh_draw_buffer, context, s))
      return false;

   buffer_hash_insert(&context->buffer_table, mesh_draw_buffer_name, mesh_draw_buffer);

   if(!buffer_rt_create(&rt_buffer, context))
      return false;

   buffer_hash_insert(&context->buffer_table, rt_buffer_name, rt_buffer);

   return true;
}

static bool vk_pipelines_create(vk_context* context)
{
   VkDescriptorSetLayout non_rtx_set_layout = 0;
   VkDescriptorSetLayout rtx_set_layout = 0;

   if(!vk_pipeline_set_layout_create(&non_rtx_set_layout, context->devices.logical, false))
      return false;

   if(context->mesh_shading_supported && context->raytracing_supported)
      if(!vk_pipeline_set_layout_create(&rtx_set_layout, context->devices.logical, true))
         return false;

   VkPipelineLayout non_rtx_pipeline_layout = 0;
   VkPipelineLayout rtx_pipeline_layout = 0;

   // set 0 for vertex SSBO, set 1 for bindless textures
   arena scratch = *context->storage;
   array(VkDescriptorSetLayout) set_layouts = {&scratch};
   array_push(set_layouts) = non_rtx_set_layout;

	if(context->textures.count > 0)
		array_push(set_layouts) = context->texture_descriptor.layout;

   if(!vk_pipeline_layout_create(&non_rtx_pipeline_layout, context->devices.logical, set_layouts.data, set_layouts.count, false))
      return false;

   array(VkDescriptorSetLayout) rtx_set_layouts = {&scratch};
   array_push(rtx_set_layouts) = rtx_set_layout;

	if(context->textures.count > 0)
		array_push(rtx_set_layouts) = context->texture_descriptor.layout;

   if(!vk_pipeline_layout_create(&rtx_pipeline_layout, context->devices.logical, rtx_set_layouts.data, rtx_set_layouts.count, true))
      return false;

   context->non_rtx_set_layout = non_rtx_set_layout;
   context->rtx_set_layout = rtx_set_layout;

   context->non_rtx_pipeline_layout = non_rtx_pipeline_layout;
   context->rtx_pipeline_layout = rtx_pipeline_layout;

   VkPipelineCache cache = 0; // TODO: enable

   VkPipeline graphics = 0;
   VkPipeline axis = 0;
   VkPipeline frustum = 0;
   VkPipeline mesh = 0;

   enum {shader_module_count = 2};
   vk_shader_module shader_modules[shader_module_count] = {0};

   vk_shader_module gm_vs = spv_hash_lookup(&context->shader_table, graphics_module_name"_vs");
   vk_shader_module gm_fs = spv_hash_lookup(&context->shader_table, graphics_module_name"_fs");

   shader_modules[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   shader_modules[0].handle = gm_vs.handle;

   shader_modules[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   shader_modules[1].handle = gm_fs.handle;

   if (!vk_graphics_pipeline_create(&graphics, context, cache, shader_modules, shader_module_count))
      return false;
   context->non_rtx_pipeline = graphics;

   vk_shader_module mm_ms = spv_hash_lookup(&context->shader_table, meshlet_module_name"_ms");
   vk_shader_module mm_fs = spv_hash_lookup(&context->shader_table, meshlet_module_name"_fs");

   shader_modules[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
   shader_modules[0].handle = mm_ms.handle;

   shader_modules[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   shader_modules[1].handle = mm_fs.handle;

   if(!vk_mesh_pipeline_create(&mesh, context, cache, shader_modules, shader_module_count))
      return false;
   context->rtx_pipeline = mesh;

   vk_shader_module am_vs = spv_hash_lookup(&context->shader_table, axis_module_name"_vs");
   vk_shader_module am_fs = spv_hash_lookup(&context->shader_table, axis_module_name"_fs");

   shader_modules[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   shader_modules[0].handle = am_vs.handle;

   shader_modules[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   shader_modules[1].handle = am_fs.handle;

   if(!vk_axis_pipeline_create(&axis, context, cache, shader_modules, shader_module_count))
      return false;
   context->axis_pipeline = axis;

   vk_shader_module fm_vs = spv_hash_lookup(&context->shader_table, frustum_module_name"_vs");
   vk_shader_module fm_fs = spv_hash_lookup(&context->shader_table, frustum_module_name"_fs");

   shader_modules[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   shader_modules[0].handle = fm_vs.handle;

   shader_modules[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   shader_modules[1].handle = fm_fs.handle;

   if (!vk_graphics_pipeline_create(&frustum, context, cache, shader_modules, shader_module_count))
      return false;
   context->frustum_pipeline = frustum;

   return true;
}

bool vk_initialize(hw* hw)
{
   if (!vk_valid(volkInitialize()))
      return false;

   vk_context* context = push(&hw->vk_storage, vk_context);

   // app callbacks
   hw->renderer.backends[VULKAN_RENDERER_INDEX] = context;
   hw->renderer.frame_render = vk_render;
   hw->renderer.frame_present = vk_present;
   hw->renderer.frame_resize = vk_resize;
   hw->renderer.renderer_index = VULKAN_RENDERER_INDEX;

   context->storage = &hw->vk_storage;
   context->scratch = hw->scratch;

   VkInstance instance = 0;
   if(!vk_instance_create(&instance, *context->storage))
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

      messenger_info.pUserData = hw;

      VkDebugUtilsMessengerEXT messenger = 0;
      if(!vk_create_debugutils_messenger_ext(&messenger, instance, &messenger_info))
      {
         printf("Could not create debug messenger");
         return false;
      }

      context->messenger = messenger;
   }
#endif

   // TODO: allocator
   VkAllocationCallbacks allocator = {0};
   context->allocator = allocator;

   // TODO: wide contracts for all these since vk_initialize is wide
   // TODO: should pass the scratch arenas instead of the context
   context->devices.physical = vk_physical_device_select(context);
   context->surface = hw->renderer.window_surface_create(context->instance, hw->renderer.window.handle);
   context->queue_family_index = vk_logical_device_select_family_index(context, *context->storage);
   context->devices.logical = vk_logical_device_create(context, *context->storage);
   context->image_ready_semaphore = vk_semaphore_create(context);
   context->image_done_semaphore = vk_semaphore_create(context);
   context->graphics_queue = vk_graphics_queue_create(context);
   context->query_pool = vk_query_pool_create(context);
   context->command_pool = vk_command_pool_create(context);
   context->command_buffer = vk_command_buffer_create(context);
   context->swapchain_surface = vk_swapchain_surface_create(context, hw->renderer.window.width, hw->renderer.window.height);
   context->renderpass = vk_renderpass_create(context);

   // framebuffers
   context->framebuffers.arena = context->storage;
   array_resize(context->framebuffers, context->swapchain_surface.image_count);

   // images
   context->swapchain_images.images.arena = context->storage;
   array_resize(context->swapchain_images.images, context->swapchain_surface.image_count);

   // depths
   context->swapchain_images.depths.arena = context->storage;
   array_resize(context->swapchain_images.depths, context->swapchain_surface.image_count);

   for(u32 i = 0; i < context->swapchain_surface.image_count; ++i)
   {
      array_add(context->framebuffers, VK_NULL_HANDLE);
      array_add(context->swapchain_images.images, (vk_image){0});
      array_add(context->swapchain_images.depths, (vk_image){0});
   }

   hw->renderer.frame_resize(hw, hw->renderer.window.width, hw->renderer.window.height);

   context->buffer_table = buffer_hash_create(100, context->storage);
   buffer_hash_clear(&context->buffer_table);

   if(!spirv_initialize(context))
   {
      printf("Could not compile and load all the shader modules\n");
      return false;
   }

   if(!vk_assets_read(context, hw->state.asset_file))
   {
      printf("Could not read all the assets\n");
      return false;
   }

   if(context->raytracing_supported)
      if(!rt_acceleration_structures_create(context))
      {
         printf("Could not create acceleration structures for ray tracing\n");
         return false;
      }

   if(!vk_buffers_create(context))
   {
      printf("Could not create all the buffer objects\n");
      return false;
   }

   if(!texture_descriptor_create(context, 1 << 16))
   {
      printf("Could not create bindless textures\n");
      return false;
   }

   // TODO: remove vk_* prefix
   if(!vk_pipelines_create(context))
   {
      printf("Could not create all the pipelines\n");
      return false;
   }

   spv_hash_function(&context->shader_table, spv_hash_log_module_name, 0);

   return true;
}

static void vk_shader_module_destroy(void* ctx, vk_shader_module_name shader_module)
{
   ctx_shader_destroy* p = (ctx_shader_destroy*)ctx;

   vkDestroyShaderModule(p->devices->logical, shader_module.module.handle, 0);
}

void vk_uninitialize(hw* hw)
{
   // TODO: Semcompress this entire function
   vk_context* context = hw->renderer.backends[VULKAN_RENDERER_INDEX];

   spv_hash_function(&context->shader_table, vk_shader_module_destroy, &(ctx_shader_destroy){&context->devices});

   // TODO: iterate buffer objects here
   vk_buffer vb = *buffer_hash_lookup(&context->buffer_table, vb_buffer_name);
   vk_buffer ib = *buffer_hash_lookup(&context->buffer_table, ib_buffer_name);
   vk_buffer mb = *buffer_hash_lookup(&context->buffer_table, mb_buffer_name);

   vk_buffer indirect = *buffer_hash_lookup(&context->buffer_table, indirect_buffer_name);
   vk_buffer indirect_rtx = *buffer_hash_lookup(&context->buffer_table, indirect_rtx_buffer_name);
   vk_buffer transform = *buffer_hash_lookup(&context->buffer_table, mesh_draw_buffer_name);

   vkDeviceWaitIdle(context->devices.logical);

   vkDestroyDescriptorSetLayout(context->devices.logical, context->non_rtx_set_layout, 0);
   vkDestroyDescriptorSetLayout(context->devices.logical, context->rtx_set_layout, 0);

   vkDestroyDescriptorSetLayout(context->devices.logical, context->texture_descriptor.layout, 0);
   vkDestroyDescriptorPool(context->devices.logical, context->texture_descriptor.descriptor_pool, 0);

   vkDestroyPipeline(context->devices.logical, context->axis_pipeline, 0);
   vkDestroyPipeline(context->devices.logical, context->frustum_pipeline, 0);
   vkDestroyPipeline(context->devices.logical, context->non_rtx_pipeline, 0);
   vkDestroyPipeline(context->devices.logical, context->rtx_pipeline, 0);

   vkDestroyPipelineLayout(context->devices.logical, context->non_rtx_pipeline_layout, 0);
   vkDestroyPipelineLayout(context->devices.logical, context->rtx_pipeline_layout, 0);

   vkDestroyCommandPool(context->devices.logical, context->command_pool, 0);
   vkDestroyQueryPool(context->devices.logical, context->query_pool, 0);

   vk_buffer_destroy(&context->devices, &ib);
   vk_buffer_destroy(&context->devices, &vb);
   vk_buffer_destroy(&context->devices, &mb);

   vk_buffer_destroy(&context->devices, &indirect);
   vk_buffer_destroy(&context->devices, &indirect_rtx);
   vk_buffer_destroy(&context->devices, &transform);

   vkDestroyRenderPass(context->devices.logical, context->renderpass, 0);
   vkDestroySemaphore(context->devices.logical, context->image_done_semaphore, 0);
   vkDestroySemaphore(context->devices.logical, context->image_ready_semaphore, 0);

   for(u32 i = 0; i < context->framebuffers.count; ++i)
      vkDestroyFramebuffer(context->devices.logical, context->framebuffers.data[i], 0);

   for(u32 i = 0; i < context->textures.count; ++i)
   {
      vkDestroyImageView(context->devices.logical, context->textures.data[i].image.view, 0);
      vkDestroyImage(context->devices.logical, context->textures.data[i].image.handle, 0);
      vkFreeMemory(context->devices.logical, context->textures.data[i].image.memory, 0);
   }

   for (u32 i = 0; i < context->swapchain_images.depths.count; ++i)
   {
      vkDestroyImageView(context->devices.logical, context->swapchain_images.depths.data[i].view, 0);
      vkDestroyImage(context->devices.logical, context->swapchain_images.depths.data[i].handle, 0);
      vkFreeMemory(context->devices.logical, context->swapchain_images.depths.data[i].memory, 0);
   }

   for (u32 i = 0; i < context->swapchain_images.images.count; ++i)
      vkDestroyImageView(context->devices.logical, context->swapchain_images.images.data[i].view, 0);

   vkDestroySwapchainKHR(context->devices.logical, context->swapchain_surface.handle, 0);
   vkDestroySurfaceKHR(context->instance, context->surface, 0);

   vkDestroyDevice(context->devices.logical, 0);
#if _DEBUG
   vkDestroyDebugUtilsMessengerEXT(context->instance, context->messenger, 0);
#endif

   vkDestroyInstance(context->instance, 0);
}
