#include "priority_queue.h"
#include "vulkan_ng.h"

#include "win32_file_io.c"

// TODO: global for now
static vk_allocator global_allocator;

#include "vulkan_spirv_loader.c"
#include "free_list.c"
#include "texture.c"
#include "hash.c"
#include "buffer.c"
#include "gltf.c"
#include "rt.c"

static void* VKAPI_PTR vk_allocation(void* user_data,
                                     size_t new_size,
                                     size_t alignment,
                                     VkSystemAllocationScope scope)
{
   (void)scope;
   (void)alignment;

   if(new_size == 0)
      return 0;

   vk_allocator* allocator = user_data;

   arena* a = allocator->a;
   list* l = &allocator->slots;

   // take from free-list
   if(l->free_list && l->free_list->next)
   {
      list_node* f = l->free_list;
      list_node* prev = 0;
      while(f && (f->data.slot_size != new_size))
      {
         prev = f;
         f = f->next;
      }
      if(f)
      {
         prev->next = f->next;
         f->next = 0;

         #if _DEBUG
         printf("ACQUIRING node from free-list: %p with %zu bytes\n", f, f->data.slot_size);
         #endif

         assert(!((uptr)f->data.memory & (alignment - 1)));
         return (byte*)f->data.memory + sizeof(size);
      }
   }

   // + sizeof(size) for header size for realloc
   list_node* n = list_node_push(a, l, new_size + sizeof(size));

   void* memory = n->data.memory;
   n->data.slot_size = new_size;

   assert(!((uptr)memory & (alignment - 1)));

   *(size*)(byte*)memory = new_size;

   #if _DEBUG
   printf("Vulkan alloc: %p with %zu bytes\n", memory, new_size);
   #endif

   return (byte*)memory + sizeof(size);
}

static void* VKAPI_PTR vk_reallocation(void* user_data,
                                       void* original,
                                       size_t new_size,
                                       size_t alignment,
                                       VkSystemAllocationScope scope)
{
   (void)scope;

   if(new_size == 0)
      return 0;

   if(!original)
      return vk_allocation(user_data, new_size, alignment, scope);

   size old_size = *((size*)original - 1);
   assert(old_size + new_size > new_size);
   void* result = vk_allocation(user_data, old_size + new_size, alignment, scope);

   memmove(result, original, old_size);

   #if _DEBUG
   printf("Vulkan re-alloc: %p with %zu bytes\n", result, new_size);
   #endif

   return result;
}

static void VKAPI_PTR vk_free(void* user_data, void* memory)
{
   if(!user_data || !memory)
      return;

   vk_allocator* allocator = user_data;
   list* l = &allocator->slots;

   list_node* n = (list_node*)((byte*)memory - (sizeof(list_node) + sizeof(size)));

   node_release(l, n);

   #if _DEBUG
   printf("RELEASING node to free-list: %p with %zu bytes\n", n, n->data.slot_size);
   #endif
}

static void VKAPI_PTR vk_internal_allocation(void* user_data,
                                             size_t size,
                                             VkInternalAllocationType allocation_type,
                                             VkSystemAllocationScope scope)
{
   (void)user_data;
   (void)size;
   (void)allocation_type;
   (void)scope;
}

static void VKAPI_PTR vk_internal_free(void* user_data,
                                       size_t size,
                                       VkInternalAllocationType allocation_type,
                                       VkSystemAllocationScope scope)
{
   (void)user_data;
   (void)size;
   (void)allocation_type;
   (void)scope;
}

static bool vk_gltf_read(vk_context* context, s8 filename)
{
   array(char) file_path = {context->app_storage};
   s8 prefix = s8("%s\\assets\\gltf\\%s");
   s8 exe_dir = vk_exe_directory(context->app_storage);

   file_path.count = exe_dir.len + prefix.len + filename.len - s8("%s%s").len;
   array_resize(file_path, file_path.count);
   wsprintf(file_path.data, s8_data(prefix), (const char*)exe_dir.data, filename.data);

   s8 gltf_path = {.data = (u8*)file_path.data, .len = file_path.count};

   assert(s8_equals(s8_slice(gltf_path, gltf_path.len - s8(".gltf").len, gltf_path.len), s8(".gltf")));
   return gltf_load(context, gltf_path);
}

static vk_shader_module vk_shader_load(VkDevice logical_device, arena scratch, const char* shader_name_raw)
{
   vk_shader_module result = {0};

   VkShaderStageFlagBits shader_stage = 0;

   const s8 shader_name = s8(shader_name_raw);
   const s8 mesh_spv_name = s8("mesh.spv");
   const s8 vert_spv_name = s8("vert.spv");
   const s8 frag_spv_name = s8("frag.spv");

   if(s8_is_substr_count(shader_name, mesh_spv_name) != invalid_index)
      shader_stage = VK_SHADER_STAGE_MESH_BIT_EXT;
   else if(s8_is_substr_count(shader_name, vert_spv_name) != invalid_index)
      shader_stage = VK_SHADER_STAGE_VERTEX_BIT;
   else if(s8_is_substr_count(shader_name, frag_spv_name) != invalid_index)
      shader_stage = VK_SHADER_STAGE_FRAGMENT_BIT;

   VkShaderModule module = vk_shader_spv_module_load(logical_device, scratch, shader_name);

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
   const s8_array shaders = vk_shader_names_read(context->app_storage, s8("bin\\assets\\shaders"));

   if(shaders.count == 0)
   {
      printf("No shaders found\n");
      return false;
   }

   spv_hash_table* table = &context->shader_table;

   table->max_count = shaders.count;

   // TODO: make function for hash tables
   table->keys = push(context->app_storage, const char*, table->max_count);
   table->values = push(context->app_storage, vk_shader_module, table->max_count);

   pointer_clear(table->values, table->max_count * sizeof(vk_shader_module));

   for(size i = 0; i < shaders.count; ++i)
   {
      s8 shader = shaders.data[i];

      if(s8_is_substr_count(shader, s8(meshlet_module_name)) != -1)
      {
         vk_shader_module mm = vk_shader_load(context->devices.logical, context->scratch, s8_data(shader));
         if(mm.stage == VK_SHADER_STAGE_MESH_BIT_EXT)
            spv_hash_insert(table, meshlet_module_name"_ms", mm);
         else if(mm.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            spv_hash_insert(table, meshlet_module_name"_fs", mm);
      }
      else if(s8_is_substr_count(shader, s8(graphics_module_name)) != -1)
      {
         vk_shader_module gm = vk_shader_load(context->devices.logical, context->scratch, s8_data(shader));
         if(gm.stage == VK_SHADER_STAGE_VERTEX_BIT)
            spv_hash_insert(table, graphics_module_name"_vs", gm);
         else if(gm.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            spv_hash_insert(table, graphics_module_name"_fs", gm);
      }
      else if(s8_is_substr_count(shader, s8(axis_module_name)) != -1)
      {
         vk_shader_module am = vk_shader_load(context->devices.logical, context->scratch, s8_data(shader));
         if(am.stage == VK_SHADER_STAGE_VERTEX_BIT)
            spv_hash_insert(table, axis_module_name"_vs", am);
         else if(am.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            spv_hash_insert(table, axis_module_name"_fs", am);
      }
      else if(s8_is_substr_count(shader, s8(frustum_module_name)) != -1)
      {
         vk_shader_module fm = vk_shader_load(context->devices.logical, context->scratch, s8_data(shader));
         if(fm.stage == VK_SHADER_STAGE_VERTEX_BIT)
            spv_hash_insert(table, frustum_module_name"_vs", fm);
         else if(fm.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            spv_hash_insert(table, frustum_module_name"_fs", fm);
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

static hw_result vk_create_debugutils_messenger_ext(hw* hw, vk_device* devices)
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

   PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(devices->instance, "vkCreateDebugUtilsMessengerEXT");
   if(!func)
      return (hw_result){0};

   VkDebugUtilsMessengerEXT debug_messenger = 0;
   if(!vk_valid(func(devices->instance, &messenger_info, &global_allocator.handle, &debug_messenger)))
      return (hw_result){0};

   return (hw_result){debug_messenger};
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

static vk_swapchain_surface vk_window_swapchain_surface_create(arena scratch, const vk_device* devices, VkSurfaceKHR surface, u32 width, u32 height)
{
   vk_swapchain_surface result = {0};

   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices->physical, surface, &surface_caps)))
      return (vk_swapchain_surface){0};

   // triple buffering
   if(surface_caps.minImageCount < 2)
      return (vk_swapchain_surface){0};

   if(!implies(surface_caps.maxImageCount != 0, surface_caps.maxImageCount >= surface_caps.minImageCount + 1))
      return (vk_swapchain_surface){0};

   u32 image_count = surface_caps.minImageCount + 1;

   VkSwapchainKHR swapchain = 0;

   VkSurfaceCapabilitiesKHR caps;
   vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices->physical, surface, &caps);

   VkExtent2D extent = {0};

   if(caps.currentExtent.width != UINT32_MAX)
      extent = caps.currentExtent;
   else
   {
      extent.width = clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
      extent.height = clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
   }

   VkSwapchainCreateInfoKHR swapchain_info = {vk_info_khr(SWAPCHAIN)};
   swapchain_info.surface = surface;
   swapchain_info.minImageCount = image_count;
   swapchain_info.imageFormat = vk_swapchain_format(scratch, devices->physical, surface);
   swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
   swapchain_info.imageExtent = extent;
   swapchain_info.imageArrayLayers = 1;
   swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   swapchain_info.queueFamilyIndexCount = 1;
   swapchain_info.pQueueFamilyIndices = &(u32)devices->queue_family_index;
   swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
   swapchain_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   swapchain_info.clipped = true;
   swapchain_info.oldSwapchain = 0;

   // TODO: break this apart so that handle creation is separate from this outer routine
   if(!vk_valid(vkCreateSwapchainKHR(devices->logical, &swapchain_info, &global_allocator.handle, &swapchain)))
      return (vk_swapchain_surface){0};

   result.format = swapchain_info.imageFormat;
   result.handle = swapchain;
   result.image_count = image_count;
   result.image_width = extent.width;
   result.image_height = extent.height;

   return result;
}

// TODO: wrap into vk_device select
static hw_result vk_physical_device_select(arena s, vk_device* device, vk_features* features)
{
   hw_result result = {0};

   u32 dev_count = 0;
   if(!vk_valid(vkEnumeratePhysicalDevices(device->instance, &dev_count, 0)))
      return result;

   VkPhysicalDevice* devs = push(&s, VkPhysicalDevice, dev_count);
   if(!vk_valid(vkEnumeratePhysicalDevices(device->instance, &dev_count, devs)))
      return result;

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

      features->time_period = props.limits.timestampPeriod;

      // pick the first one as the fallback
      if(fallback_gpu == 0)
         fallback_gpu = i;

      u32 extension_count = 0;
      if(!vk_valid(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, 0)))
         return result;

      VkExtensionProperties* extensions = push(&s, VkExtensionProperties, extension_count);
      if(!vk_valid(vkEnumerateDeviceExtensionProperties(devs[i], 0, &extension_count, extensions)))
         return result;

      for(u32 j = 0; j < extension_count; ++j)
      {
         s8 e = s8(extensions[j].extensionName);
         printf("Available Vulkan device extension[%u]: %s\n", j, s8_data(e));

         if(s8_equals(e, s8(VK_EXT_MESH_SHADER_EXTENSION_NAME)))
            features->mesh_shading_supported = true;

         if(s8_equals(e, s8(VK_KHR_RAY_QUERY_EXTENSION_NAME)))
            features->raytracing_supported = true;

         if(features->raytracing_supported && features->mesh_shading_supported)
         {
            printf("Ray tracing supported\n");
            printf("Mesh shading supported\n");

            return (hw_result){.h = devs[i]};
         }
      }
   }

   return (hw_result){.h = devs[fallback_gpu]};
}

static hw_result vk_logical_device_select_family_index(arena scratch, vk_device* devices, VkSurfaceKHR surface)
{
   u32 queue_family_count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(devices->physical, &queue_family_count, 0);

   VkQueueFamilyProperties* queue_families = push(&scratch, VkQueueFamilyProperties, queue_family_count);
   vkGetPhysicalDeviceQueueFamilyProperties(devices->physical, &queue_family_count, queue_families);

   for(u32 i = 0; i < queue_family_count; i++)
   {
      VkBool32 present_support = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(devices->physical, i, surface, &present_support);

      // graphics and presentation within same queue for simplicity
      if((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support)
         return (hw_result){.i = i};
   }

   return (hw_result){.i = invalid_index};
}

static hw_result vk_logical_device_create(arena scratch, vk_device* devices, vk_features* features)
{
   array(s8) extensions = {&scratch};

   array_push(extensions) = s8(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
   array_push(extensions) = s8(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
   array_push(extensions) = s8(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);

   if(features->mesh_shading_supported)
   {
      array_push(extensions) = s8(VK_EXT_MESH_SHADER_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
   }
   if(features->raytracing_supported)
   {
      array_push(extensions) = s8(VK_KHR_RAY_QUERY_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
      array_push(extensions) = s8(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
   }

   VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

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

   features2.pNext = &features12;
   features12.pNext = &features_multiview;
   features_multiview.pNext = &features_frag_shading;

   if(features->mesh_shading_supported)
   {
      features_mesh_shader.pNext = features2.pNext;
      features2.pNext = &features_mesh_shader;

      features_mesh_shader.meshShader = true;
      features_mesh_shader.taskShader = true;
      features_mesh_shader.multiviewMeshShader = true;
   }

   if(features->raytracing_supported)
   {
      ray_query.pNext = &acceleration_features;
      acceleration_features.pNext = features2.pNext;
      features2.pNext = &ray_query;

      ray_query.rayQuery = true;
      acceleration_features.accelerationStructure = true;
   }

   vkGetPhysicalDeviceFeatures2(devices->physical, &features2);

   features2.features.depthBounds = true;
   features2.features.wideLines = true;
   features2.features.fillModeNonSolid = true;
   features2.features.sampleRateShading = true;

   const char** extension_names = push(&scratch, char*, extensions.count);

   for(u32 i = 0; i < extensions.count; ++i)
   {
      extension_names[i] = (const char*)extensions.data[i].data;

      printf("Using Vulkan device extension[%u]: %s\n", i, extension_names[i]);
   }

   enum { queue_count = 1 };
   f32 priorities[queue_count] = {1.0f};
   VkDeviceQueueCreateInfo queue_info[queue_count] = {vk_info(DEVICE_QUEUE)};

   queue_info[0].queueFamilyIndex = (u32)devices->queue_family_index;
   queue_info[0].queueCount = queue_count;
   queue_info[0].pQueuePriorities = priorities;

   VkDeviceCreateInfo ldev_info = {vk_info(DEVICE)};

   ldev_info.queueCreateInfoCount = 1;
   ldev_info.pQueueCreateInfos = queue_info;
   ldev_info.enabledExtensionCount = (u32)extensions.count;
   ldev_info.ppEnabledExtensionNames = extension_names;
   ldev_info.pNext = &features2;

   VkDevice logical_device;
   if(!vk_valid(vkCreateDevice(devices->physical, &ldev_info, &global_allocator.handle, &logical_device)))
      return (hw_result){0};

   return (hw_result){logical_device};
}

static VkQueue vk_graphics_queue_get(vk_device* devices)
{
   VkQueue graphics_queue = 0;
   u32 queue_index = 0;

   vkGetDeviceQueue(devices->logical, (u32)devices->queue_family_index, queue_index, &graphics_queue);

   return graphics_queue;
}

static hw_result vk_semaphore_create(vk_device* devices)
{
   VkSemaphore sema = 0;

   VkSemaphoreCreateInfo sema_info = {vk_info(SEMAPHORE)};
   if(!vk_valid(vkCreateSemaphore(devices->logical, &sema_info, &global_allocator.handle, &sema)))
      return (hw_result){0};

   return (hw_result){sema};
}

static vk_swapchain_surface vk_swapchain_surface_create(arena scratch, const vk_device* devices, VkSurfaceKHR surface, u32 width, u32 height)
{
   return vk_window_swapchain_surface_create(scratch, devices, surface, width, height);
}

static hw_result vk_command_buffer_create(vk_cmd* cmd, vk_device* devices)
{
   VkCommandBufferAllocateInfo buffer_allocate_info = {vk_info_allocate(COMMAND_BUFFER)};

   buffer_allocate_info.commandBufferCount = 1;
   buffer_allocate_info.commandPool = cmd->pool;
   buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

   VkCommandBuffer buffer = 0;
   if(!vk_valid(vkAllocateCommandBuffers(devices->logical, &buffer_allocate_info, &buffer)))
      return (hw_result){0};

   return (hw_result){buffer};
}

static hw_result vk_command_pool_create(vk_device* devices)
{
   VkCommandPoolCreateInfo pool_info = {vk_info(COMMAND_POOL)};
   pool_info.queueFamilyIndex = (u32)devices->queue_family_index;

   VkCommandPool pool = 0;
   if(!vk_valid(vkCreateCommandPool(devices->logical, &pool_info, &global_allocator.handle, &pool)))
      return (hw_result){0};

   return (hw_result){pool};
}


static hw_result vk_renderpass_create(vk_device* devices, vk_swapchain_surface* swapchain)
{
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

   attachments[color_attachment_index].format = swapchain->format;
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

   if(!vk_valid(vkCreateRenderPass(devices->logical, &renderpass_info, &global_allocator.handle, &renderpass)))
      return (hw_result){0};

   return (hw_result){renderpass};
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

   vk_assert(vkCreateFramebuffer(logical_device, &framebuffer_info, &global_allocator.handle, &framebuffer));

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

static void vk_swapchain_destroy(vk_device* devices, vk_swapchain_images* images, framebuffers_array* framebuffers, vk_swapchain_surface* swapchain)
{
   for(u32 i = 0; i < framebuffers->count; ++i)
      vkDestroyFramebuffer(devices->logical, framebuffers->data[i], &global_allocator.handle);

   for (u32 i = 0; i < images->depths.count; ++i)
   {
      vkDestroyImageView(devices->logical, images->depths.data[i].view, &global_allocator.handle);
      vkDestroyImage(devices->logical, images->depths.data[i].handle, &global_allocator.handle);
      vkFreeMemory(devices->logical, images->depths.data[i].memory, &global_allocator.handle);
   }

   for (u32 i = 0; i < images->images.count; ++i)
      vkDestroyImageView(devices->logical, images->images.data[i].view, &global_allocator.handle);

   vkDestroySwapchainKHR(devices->logical, swapchain->handle, &global_allocator.handle);
}

// TODO: wide contract
static void vk_swapchain_update(arena scratch, vk_device* devices, vk_swapchain_images* images, vk_swapchain_surface* swapchain, framebuffers_array* framebuffers, VkRenderPass renderpass)
{
   VkImage* raw_images = push(&scratch, VkImage, swapchain->image_count);

   vk_assert(vkGetSwapchainImagesKHR(devices->logical, swapchain->handle, &swapchain->image_count, raw_images));

   VkExtent3D depth_extent = {swapchain->image_width, swapchain->image_height, 1};

   for(u32 i = 0; i < swapchain->image_count; ++i)
   {
      images->images.data[i].handle = raw_images[i];

      vk_depth_image_create(&images->depths.data[i], devices, VK_FORMAT_D32_SFLOAT, depth_extent); // TODO: Return false here if fail

      images->images.data[i].view = vk_image_view_create(devices, swapchain->format, images->images.data[i].handle, VK_IMAGE_ASPECT_COLOR_BIT);
      images->depths.data[i].view = vk_image_view_create(devices, VK_FORMAT_D32_SFLOAT, images->depths.data[i].handle, VK_IMAGE_ASPECT_DEPTH_BIT);

      VkImageView attachments[2] = {images->images.data[i].view, images->depths.data[i].view};

      VkFramebuffer fb = vk_framebuffer_create(devices->logical, renderpass, swapchain, attachments, array_count(attachments));
      framebuffers->data[i] = fb;
   }
}

static void gpu_log(hw* hw)
{
   u32 renderer_index = hw->renderer.renderer_index;
   assert(renderer_index < RENDERER_COUNT);

   vk_context* context = hw->renderer.backends[renderer_index];

   u64 query_results[2] = {0};
   vkGetQueryPoolResults(context->devices.logical,
                         context->query_pool,
                         0,
                         array_count(query_results),
                         sizeof(query_results),
                         query_results,
                         sizeof(query_results[0]),
                         VK_QUERY_RESULT_64_BIT);

   const f64 gpu_begin = (f64)(query_results[0]) * context->features.time_period;
   const f64 gpu_end = (f64)(query_results[1]) * context->features.time_period;

   const f64 ms = 1e3;
   const f64 us = 1e6;
   const f64 gpu_delta = max(gpu_end - gpu_begin, 0.f);

   if(hw->state.is_mesh_shading)
      hw->window_title_set(hw,
                       s8("cpu: %.2f ms; gpu: %.2f ms; #Meshlets: %u; Hold 'a' to show world axis; Press 'f' to toggle fullscreen; Press 'r' to reset camera; Press 'm' to toggle RTX; RTX ON"),
                       hw->state.frame_delta_in_seconds * ms, gpu_delta / us, context->meshlets.count);
   else
      hw->window_title_set(hw,
                       s8("cpu: %.2f ms; gpu: %.2f ms; #Meshlets: 0; Hold 'a' to show world axis; Press 'f' to toggle fullscreen; Press 'r' to reset camera; Press 'm' to toggle RTX; RTX OFF"),
                       hw->state.frame_delta_in_seconds * ms, gpu_delta / us);
}

static void vk_resize_swapchain(hw_renderer* renderer, u32 width, u32 height)
{
   if(width == 0 || height == 0)
      return;

   u32 renderer_index = renderer->renderer_index;
   assert(renderer_index < RENDERER_COUNT);

   vk_context* context = renderer->backends[renderer_index];
   vk_device* devices = &context->devices;

   // wait for device to be done before recreating swapchain
   vk_assert(vkDeviceWaitIdle(devices->logical));

   vk_swapchain_images* images = &context->images;
   framebuffers_array* framebuffers = &context->framebuffers;
   vk_swapchain_surface* swapchain = &context->swapchain;

   vk_swapchain_destroy(&context->devices, images, framebuffers, swapchain);

   arena s = context->scratch;
   VkSurfaceKHR surface = context->surface;

   VkRenderPass renderpass = context->renderpass;

   context->swapchain = vk_swapchain_surface_create(s, devices, surface, width, height);
   vk_swapchain_update(s, devices, images, swapchain, framebuffers, renderpass);

   mvp_transform mvp = {0};
   const f32 ar = (f32)swapchain->image_width / swapchain->image_height;

   mvp.n = 0.01f;
   mvp.f = 10000.0f;
   mvp.ar = ar;

   mvp.projection = mat4_perspective(ar, 75.0f, mvp.n, mvp.f);
   renderer->mvp = mvp;

   printf("Window size: [%ux%u]\n", width, height);
}

static hw_result vk_query_pool_create(vk_device* devices, size query_pool_size)
{
   VkQueryPoolCreateInfo info = {vk_info(QUERY_POOL)};
   info.queryType = VK_QUERY_TYPE_TIMESTAMP;
   info.queryCount = (u32)query_pool_size;

   VkQueryPool pool = 0;
   if(!vk_valid(vkCreateQueryPool(devices->logical, &info, &global_allocator.handle, &pool)))
      return (hw_result){0};

   return (hw_result){pool};
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
   present_info.pSwapchains = &context->swapchain.handle;
   present_info.pImageIndices = &context->image_index;
   present_info.waitSemaphoreCount = 1;
   present_info.pWaitSemaphores = &context->image_done_semaphore;

   VkResult present_result = vkQueuePresentKHR(context->graphics_queue, &present_info);

   if(present_result == VK_SUBOPTIMAL_KHR || present_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize_swapchain(&hw->renderer, context->swapchain.image_width, context->swapchain.image_height);

   // wait until all queue ops are done
   // essentialy run gpu and cpu in sync (driver decides this)
   // TODO: This is bad way to do sync but who cares for now
   // TODO: Use fences really
   vk_assert(vkDeviceWaitIdle(context->devices.logical));
}

static void vk_render(hw* hw, vk_context* context, app_state* state)
{
   u32 image_index = 0;
   VkResult next_image_result = vkAcquireNextImageKHR(context->devices.logical, context->swapchain.handle, UINT64_MAX, context->image_ready_semaphore, VK_NULL_HANDLE, &image_index);

   context->image_index = image_index;

   if(next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
   {
      // TODO: recycle semaphores with free-list
      vkDestroySemaphore(context->devices.logical, context->image_ready_semaphore, &global_allocator.handle);
      context->image_ready_semaphore = vk_semaphore_create(&context->devices).h;

      vk_resize_swapchain(&hw->renderer, hw->renderer.window.width, hw->renderer.window.height);

      return; // drop this frame
   }

   if(next_image_result == VK_SUBOPTIMAL_KHR)
      vk_resize_swapchain(&hw->renderer, hw->renderer.window.width, hw->renderer.window.height);

   vk_assert(vkResetCommandPool(context->devices.logical, context->cmd.pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   VkRenderPassBeginInfo renderpass_info = {vk_info_begin(RENDER_PASS)};
   renderpass_info.renderPass = context->renderpass;
   renderpass_info.framebuffer = context->framebuffers.data[image_index];
   renderpass_info.renderArea.extent = (VkExtent2D)
   {context->swapchain.image_width, context->swapchain.image_height};

   VkCommandBuffer command_buffer = context->cmd.buffer;

   vk_assert(vkBeginCommandBuffer(command_buffer, &buffer_begin_info));

   vkCmdResetQueryPool(context->cmd.buffer, context->query_pool, 0, context->query_pool_size);
   vkCmdWriteTimestamp(context->cmd.buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query_pool, 0);

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

   VkImage color_image = context->images.images.data[image_index].handle;
   VkImageMemoryBarrier color_image_begin_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_begin_barrier);

   VkImage depth_image = context->images.depths.data[image_index].handle;
   VkImageMemoryBarrier depth_image_begin_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_begin_barrier);


   vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

   VkViewport viewport = {0};

   // y-is-up
   viewport.x = 0.0f;
   viewport.y = (f32)context->swapchain.image_height;
   viewport.width = (f32)context->swapchain.image_width;
   viewport.height = -(f32)context->swapchain.image_height;

   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;

   VkRect2D scissor = {0};
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   scissor.extent.width = (u32)context->swapchain.image_width;
   scissor.extent.height = (u32)context->swapchain.image_height;

   vkCmdSetViewport(command_buffer, 0, 1, &viewport);
   vkCmdSetScissor(command_buffer, 0, 1, &scissor);
   vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

   if(state->is_mesh_shading)
   {
      VkPipeline pipeline = context->rtx_pipeline;
      VkPipelineLayout pipeline_layout = context->rtx_pipeline_layout;

      cmd_bind_descriptor_set(command_buffer, pipeline_layout, &context->texture_descriptor.set, 1, 1);
      cmd_bind_pipeline(command_buffer, pipeline);

      arena s = context->scratch;
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
         array_push(bindings) = (vk_buffer_binding){buffer, 3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, &context->rt_as.tlas};
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

      arena s = context->scratch;
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

   vkCmdWriteTimestamp(context->cmd.buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query_pool, 1);

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

      if(!vk_valid(vkCreateDescriptorSetLayout(device, &info, &global_allocator.handle, set_layout)))
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

      if(!vk_valid(vkCreateDescriptorSetLayout(device, &info, &global_allocator.handle, set_layout)))
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

   if(!vk_valid(vkCreatePipelineLayout(logical_device, &info, &global_allocator.handle, layout)))
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
   arena scratch = context->scratch;

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

   if(!vk_valid(vkCreateGraphicsPipelines(context->devices.logical, cache, 1, &pipeline_info, &global_allocator.handle, pipeline)))
      return false;

   return true;
}

static bool vk_graphics_pipeline_create(VkPipeline* pipeline, vk_context* context, VkPipelineCache cache, const vk_shader_module* shader_modules, size shader_module_count)
{
   arena scratch = context->scratch;

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

   if(!vk_valid(vkCreateGraphicsPipelines(context->devices.logical, cache, 1, &pipeline_info, &global_allocator.handle, pipeline)))
      return false;

   return true;
}

static bool vk_axis_pipeline_create(VkPipeline* pipeline, vk_context* context, VkPipelineCache cache, const vk_shader_module* shader_modules, size shader_module_count)
{
   arena scratch = context->scratch;

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

   if(!vk_valid(vkCreateGraphicsPipelines(context->devices.logical, cache, 1, &pipeline_info, &global_allocator.handle, pipeline)))
      return false;

   return true;
}

static hw_result vk_instance_create(arena scratch)
{
   u32 ext_count = 0;
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, 0)))
      return (hw_result){0};

   VkExtensionProperties* extensions = push(&scratch, VkExtensionProperties, ext_count);
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, extensions)))
      return (hw_result){0};

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
   VkInstance instance = 0;
   if(!vk_valid(vkCreateInstance(&instance_info, &global_allocator.handle, &instance)))
      return (hw_result){0};

   return (hw_result){instance};
}

static bool vk_buffers_create(vk_context* context)
{
   vk_buffer indirect_buffer = {0};
   vk_buffer indirect_rtx_buffer = {0};
   vk_buffer mesh_draw_buffer = {0};
   vk_buffer rt_buffer = {0};

   arena s = context->scratch;

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

static bool vk_pipelines_create(vk_features* features, vk_context* context)
{
   VkDescriptorSetLayout non_rtx_set_layout = 0;
   VkDescriptorSetLayout rtx_set_layout = 0;

   if(!vk_pipeline_set_layout_create(&non_rtx_set_layout, context->devices.logical, false))
      return false;

   if(features->mesh_shading_supported && features->raytracing_supported)
      if(!vk_pipeline_set_layout_create(&rtx_set_layout, context->devices.logical, true))
         return false;

   VkPipelineLayout non_rtx_pipeline_layout = 0;
   VkPipelineLayout rtx_pipeline_layout = 0;

   // set 0 for vertex SSBO, set 1 for bindless textures
   arena scratch = context->scratch;
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
   if(!vk_valid(volkInitialize()))
      return false;

   vk_context* context = push(hw->app_storage, vk_context);
   vk_device* devices = &context->devices;
   vk_cmd* cmd = &context->cmd;
   vk_features* features = &context->features;
   VkSurfaceKHR* surface = &context->surface;
   vk_swapchain_surface* swapchain = &context->swapchain;

   // app callbacks
   hw->renderer.backends[VULKAN_RENDERER_INDEX] = context;
   hw->renderer.frame_render = vk_render;
   hw->renderer.frame_present = vk_present;
   hw->renderer.frame_resize = vk_resize_swapchain;
   hw->renderer.gpu_log = gpu_log;
   hw->renderer.renderer_index = VULKAN_RENDERER_INDEX;

   context->app_storage = hw->app_storage;
   context->vulkan_storage = hw->vulkan_storage;
   context->scratch = hw->scratch;

   arena* a = context->app_storage;
   arena s = context->scratch;

   global_allocator.a = context->vulkan_storage;
   global_allocator.handle.pUserData = &global_allocator;
   global_allocator.handle.pfnAllocation = vk_allocation;
   global_allocator.handle.pfnReallocation = vk_reallocation;
   global_allocator.handle.pfnFree = vk_free;
   global_allocator.handle.pfnInternalFree = vk_internal_free;
   global_allocator.handle.pfnInternalAllocation = vk_internal_allocation;

   if(!(context->devices.instance = vk_instance_create(s).h))
   {
      printf("Could not create instance\n");
      return false;
   }

   volkLoadInstance(devices->instance);

   if(!(context->devices.physical = vk_physical_device_select(s, devices, features).h))
   {
      printf("Could not select physical device\n");
      return false;
   }
   #ifdef _DEBUG
   if(!(context->messenger = vk_create_debugutils_messenger_ext(hw, devices).h))
   {
      printf("Could not create debug messenger\n");
      return false;
   }
   #endif
   if(!(context->surface = hw->renderer.window_surface_create(&global_allocator, context->devices.instance, hw->renderer.window.handle).h))
   {
      printf("Could not create the window surface\n");
      return false;
   }
   if((context->devices.queue_family_index = vk_logical_device_select_family_index(s, devices, *surface).i) == invalid_index)
   {
      printf("Could not select queue family index for surface\n");
      return false;
   }
   if(!(context->devices.logical = vk_logical_device_create(s, devices, features).h))
   {
      printf("Could not create logical device\n");
      return false;
   }
   if(!(context->image_ready_semaphore = vk_semaphore_create(devices).h))
   {
      printf("Could not create image ready semaphore\n");
      return false;
   }
   if(!(context->image_done_semaphore = vk_semaphore_create(devices).h))
   {
      printf("Could not create image done semaphore\n");
      return false;
   }

   const u32 query_pool_size = 128;
   if(!(context->query_pool = vk_query_pool_create(devices, query_pool_size).h))
   {
      printf("Could not create query pool\n");
      return false;
   }
   context->query_pool_size = query_pool_size;

   if(!(context->cmd.pool = vk_command_pool_create(devices).h))
   {
      printf("Could not create command pool\n");
      return false;
   }
   if(!(context->cmd.buffer = vk_command_buffer_create(cmd, devices).h))
   {
      printf("Could not create command buffer\n");
      return false;
   }
   // TODO: break this apart so that handle creation is separate
   context->swapchain = vk_swapchain_surface_create(s, devices, *surface, hw->renderer.window.width, hw->renderer.window.height);

   if(!(context->renderpass = vk_renderpass_create(devices, swapchain).h))
   {
      printf("Could not create renderpass\n");
      return false;
   }
   context->graphics_queue = vk_graphics_queue_get(devices);

   // framebuffers
   context->framebuffers.arena = a;
   array_resize(context->framebuffers, context->swapchain.image_count);

   // images
   context->images.images.arena = a;
   array_resize(context->images.images, context->swapchain.image_count);

   // depths
   context->images.depths.arena = a;
   array_resize(context->images.depths, context->swapchain.image_count);

   for(u32 i = 0; i < context->swapchain.image_count; ++i)
   {
      array_add(context->framebuffers, VK_NULL_HANDLE);
      array_add(context->images.images, (vk_image) { 0 });
      array_add(context->images.depths, (vk_image) { 0 });
   }

   hw->renderer.frame_resize(&hw->renderer, hw->renderer.window.width, hw->renderer.window.height);

   const size buffer_table_size = 1 << 8;
   context->buffer_table = buffer_hash_create(buffer_table_size, a);
   buffer_hash_clear(&context->buffer_table);

   if(!spirv_initialize(context))
   {
      printf("Could not compile and load all the required shader modules\n");
      return false;
   }

   if(!vk_assets_read(context, hw->state.asset_file))
   {
      printf("Could not read all the assets\n");
      return false;
   }

   if(features->raytracing_supported)
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
   if(!vk_pipelines_create(features, context))
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
   vk_device* devices = &context->devices;
   vk_buffer_hash_table* buffer_table = &context->buffer_table;

   spv_hash_function(&context->shader_table, vk_shader_module_destroy, &(ctx_shader_destroy){&context->devices});

   // TODO: iterate buffer objects here
   vk_buffer vb = *buffer_hash_lookup(buffer_table, vb_buffer_name);
   vk_buffer ib = *buffer_hash_lookup(buffer_table, ib_buffer_name);
   vk_buffer mb = *buffer_hash_lookup(buffer_table, mb_buffer_name);

   vk_buffer indirect = *buffer_hash_lookup(buffer_table, indirect_buffer_name);
   vk_buffer indirect_rtx = *buffer_hash_lookup(buffer_table, indirect_rtx_buffer_name);
   vk_buffer transform = *buffer_hash_lookup(buffer_table, mesh_draw_buffer_name);

   vk_buffer tlas = *buffer_hash_lookup(buffer_table, tlas_buffer_name);
   vk_buffer blas = *buffer_hash_lookup(buffer_table, blas_buffer_name);
   vk_buffer rt = *buffer_hash_lookup(buffer_table, rt_buffer_name);

   vkDeviceWaitIdle(context->devices.logical);

   vkDestroyDescriptorSetLayout(context->devices.logical, context->non_rtx_set_layout, &global_allocator.handle);
   vkDestroyDescriptorSetLayout(context->devices.logical, context->rtx_set_layout, &global_allocator.handle);

   vkDestroyDescriptorSetLayout(context->devices.logical, context->texture_descriptor.layout, &global_allocator.handle);
   vkDestroyDescriptorPool(context->devices.logical, context->texture_descriptor.descriptor_pool, &global_allocator.handle);

   vkDestroyPipeline(context->devices.logical, context->axis_pipeline, &global_allocator.handle);
   vkDestroyPipeline(context->devices.logical, context->frustum_pipeline, &global_allocator.handle);
   vkDestroyPipeline(context->devices.logical, context->non_rtx_pipeline, &global_allocator.handle);
   vkDestroyPipeline(context->devices.logical, context->rtx_pipeline, &global_allocator.handle);

   vkDestroyPipelineLayout(context->devices.logical, context->non_rtx_pipeline_layout, &global_allocator.handle);
   vkDestroyPipelineLayout(context->devices.logical, context->rtx_pipeline_layout, &global_allocator.handle);

   vkDestroyCommandPool(context->devices.logical, context->cmd.pool, &global_allocator.handle);
   vkDestroyQueryPool(context->devices.logical, context->query_pool, &global_allocator.handle);

   vk_buffer_destroy(&context->devices, &ib);
   vk_buffer_destroy(&context->devices, &vb);
   vk_buffer_destroy(&context->devices, &mb);

   vk_buffer_destroy(&context->devices, &indirect);
   vk_buffer_destroy(&context->devices, &indirect_rtx);
   vk_buffer_destroy(&context->devices, &transform);

   vk_buffer_destroy(&context->devices, &tlas);
   vk_buffer_destroy(&context->devices, &blas);
   vk_buffer_destroy(&context->devices, &rt);

   for(size i = 0; i < context->rt_as.blas_count; i++)
      vkDestroyAccelerationStructureKHR(context->devices.logical, context->rt_as.blases[i], &global_allocator.handle);

   vkDestroyAccelerationStructureKHR(context->devices.logical, context->rt_as.tlas, &global_allocator.handle);

   vkDestroyRenderPass(context->devices.logical, context->renderpass, &global_allocator.handle);
   vkDestroySemaphore(context->devices.logical, context->image_done_semaphore, &global_allocator.handle);
   vkDestroySemaphore(context->devices.logical, context->image_ready_semaphore, &global_allocator.handle);

   for(u32 i = 0; i < context->framebuffers.count; ++i)
      vkDestroyFramebuffer(context->devices.logical, context->framebuffers.data[i], &global_allocator.handle);

   for(u32 i = 0; i < context->textures.count; ++i)
   {
      vkDestroyImageView(context->devices.logical, context->textures.data[i].image.view, &global_allocator.handle);
      vkDestroyImage(context->devices.logical, context->textures.data[i].image.handle, &global_allocator.handle);
      vkFreeMemory(context->devices.logical, context->textures.data[i].image.memory, &global_allocator.handle);
   }

   for (u32 i = 0; i < context->images.depths.count; ++i)
   {
      vkDestroyImageView(context->devices.logical, context->images.depths.data[i].view, &global_allocator.handle);
      vkDestroyImage(context->devices.logical, context->images.depths.data[i].handle, &global_allocator.handle);
      vkFreeMemory(context->devices.logical, context->images.depths.data[i].memory, &global_allocator.handle);
   }

   for (u32 i = 0; i < context->images.images.count; ++i)
      vkDestroyImageView(context->devices.logical, context->images.images.data[i].view, &global_allocator.handle);

   //TODO: call vk_swapchain_destroy(context);
   vkDestroySwapchainKHR(context->devices.logical, context->swapchain.handle, &global_allocator.handle);
   vkDestroySurfaceKHR(devices->instance, context->surface, &global_allocator.handle);

   vkDestroyDevice(context->devices.logical, &global_allocator.handle);
#if _DEBUG
   vkDestroyDebugUtilsMessengerEXT(devices->instance, context->messenger, &global_allocator.handle);
#endif

   vkDestroyInstance(devices->instance, &global_allocator.handle);
}
