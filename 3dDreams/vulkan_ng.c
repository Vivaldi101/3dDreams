#include "arena.h"
#include "common.h"
#include "graphics.h"
#include "priority_queue.h"

#include "vulkan_ng.h"

#include <volk.c>
#include "win32_file_io.c" // TODO: pass these as function pointers from the platform
#include "vulkan_spirv_loader.c"

#pragma comment(lib,	"vulkan-1.lib")

/* define TINYOBJ_LOADER_C_IMPLEMENTATION for only *one* .c */
#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "../extern/tinyobjloader-c/tinyobj_loader_c.h"

typedef struct 
{
   arena scratch;
} tinyobj_user_ctx;

typedef struct 
{
   f32 vx, vy, vz;   // pos
   f32 nx, ny, nz;   // normal
   f32 tu, tv;       // texture
} tinyobj_vertex;

// Simple float hashing
static inline uint32_t float_to_bits(float f)
{
   uint32_t u;
   memcpy(&u, &f, sizeof(uint32_t));
   return u;
}

// TODO: Proper hash table for this
// FNV-1a hash combining
static uint32_t hash_vertex(const tinyobj_vertex* v, size modulo)
{
   uint32_t hash = 2166136261u;

#define HASH_F(f) do { \
        uint32_t bits = float_to_bits((f)); \
        hash ^= bits; \
        hash *= 16777619u; \
    } while(0)

   HASH_F(v->vx); HASH_F(v->vy); HASH_F(v->vz);
   HASH_F(v->nx); HASH_F(v->ny); HASH_F(v->nz);
   HASH_F(v->tu); HASH_F(v->tv);

#undef HASH_F

   return hash % modulo;
}

// Approximate float equality helper (in case of precision issues)
static inline int float_eq(float a, float b)
{
   return fabsf(a - b) < 1e-6f;
}

static int vertex_equal(const tinyobj_vertex* a, const tinyobj_vertex* b)
{
   return float_eq(a->vx, b->vx) && float_eq(a->vy, b->vy) && float_eq(a->vz, b->vz) &&
      float_eq(a->nx, b->nx) && float_eq(a->ny, b->ny) && float_eq(a->nz, b->nz) &&
      float_eq(a->tu, b->tu) && float_eq(a->tv, b->tv);
}

static u32 vertex_get(const tinyobj_vertex* vertex, const tinyobj_vertex* unique_verts, u32 unique_count)
{
   for(u32 i = 0; i < unique_count; ++i)
      if(vertex_equal(&unique_verts[i], vertex))
         return i;

   return (u32)-1;  // not found
}

static tinyobj_vertex* tinyobj_mesh_storage(arena* storage, u32 vertex_count)
{
   set_arena_type(tinyobj_vertex);
   arena_invariant(vertex_count, storage, arena_type);
   return new(storage, arena_type, vertex_count);
}

static void tinyobj_file_read(void *ctx, const char *filename, int is_mtl, const char *obj_filename, char **buf, size_t *len)
{
   char shader_path[MAX_PATH];

   tinyobj_user_ctx* user_data = (tinyobj_user_ctx*)ctx;

   file_result project_dir = vk_project_directory(&user_data->scratch);

   wsprintf(shader_path, project_dir.data, array_count(shader_path));
   wsprintf(shader_path, "%s\\assets\\objs\\%s", project_dir.data, obj_filename);

   file_result file_read = win32_file_read(&user_data->scratch, shader_path);

   *len = file_read.file_size;
   *buf = file_read.data;
}

enum { MAX_VULKAN_OBJECT_COUNT = 16, OBJECT_SHADER_COUNT = 2 };

//#define vk_break_on_validation

#define vk_valid_handle(v) ((v) != VK_NULL_HANDLE)
#define vk_valid_format(v) ((v) != VK_FORMAT_UNDEFINED)

#define vk_valid(v) ((v) == VK_SUCCESS)
#define vk_test_return(v) if(!vk_valid((v))) return false
#define vk_test_return_handle(v) if(!vk_valid((v))) return VK_NULL_HANDLE

#define vk_info(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO
#define vk_info_allocate(i) VK_STRUCTURE_TYPE_##i##_ALLOCATE_INFO
#define vk_info_khr(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO_KHR
#define vk_info_ext(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO_EXT

#define vk_info_begin(i) VK_STRUCTURE_TYPE_##i##_BEGIN_INFO
#define vk_info_end(i) VK_STRUCTURE_TYPE_##i##_END_INFO

#ifdef _DEBUG
#define vk_assert(v) \
        do { \
          VkResult _r = (v); \
          assert(vk_valid(_r)); \
        } while(0)
#else
#define vk_assert(v) (v)
#endif

#pragma pack(push, 1)
typedef struct mvp_transform
{
    mat4 projection;
    mat4 view;
    mat4 model;
    f32 n;
    f32 f;
    f32 ar;
} mvp_transform;
#pragma pack(pop)

align_struct swapchain_surface_info
{
   u32 image_width;
   u32 image_height;
   u32 image_count;

   VkSurfaceKHR surface;
   VkSwapchainKHR swapchain;

   VkFormat format;
   VkImage images[MAX_VULKAN_OBJECT_COUNT];
   VkImage depths[MAX_VULKAN_OBJECT_COUNT];
   VkImageView image_views[MAX_VULKAN_OBJECT_COUNT];
   VkImageView depth_views[MAX_VULKAN_OBJECT_COUNT];
} swapchain_surface_info;

align_struct
{
   VkShaderModule vs;
   VkShaderModule fs;
} vk_shader_modules;

align_struct
{
   VkBuffer handle;
   VkDeviceMemory memory;
   void* data;
   size size;
} vk_buffer;

align_struct
{
   VkFramebuffer framebuffers[MAX_VULKAN_OBJECT_COUNT];

   VkPhysicalDevice physical_dev;
   VkDevice logical_dev;
   VkSurfaceKHR surface;
   VkAllocationCallbacks* allocator;
   VkSemaphore image_ready_semaphore;
   VkSemaphore image_done_semaphore;
   VkQueue graphics_queue;
   VkCommandPool command_pool;
   VkCommandBuffer command_buffer;
   VkRenderPass renderpass;

   // TODO: Pipelines into an array
   VkPipeline cube_pipeline;
   VkPipeline axes_pipeline;
   VkPipeline frustum_pipeline;
   VkPipelineLayout pipeline_layout;

   vk_buffer vb;
   vk_buffer ib;
   u32 index_count;

   swapchain_surface_info swapchain_info;

   arena* storage;
   u32 queue_family_index;
} vk_context;

static vk_buffer vk_buffer_create(VkDevice device, size size, VkPhysicalDeviceMemoryProperties memory_properties, VkBufferUsageFlags usage)
{
   vk_buffer buffer = {};

   VkBufferCreateInfo create_info = {vk_info(BUFFER)};
   create_info.size = size;
   create_info.usage = usage;

   if(!vk_valid(vkCreateBuffer(device, &create_info, 0, &buffer.handle)))
      return (vk_buffer){};

   VkMemoryRequirements memory_reqs;
   vkGetBufferMemoryRequirements(device, buffer.handle, &memory_reqs);

   VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   u32 memory_index = memory_properties.memoryTypeCount;
   u32 i = 0;

   while(i < memory_index)
   {
      if(((memory_reqs.memoryTypeBits & (1 << i)) && memory_properties.memoryTypes[i].propertyFlags == flags))
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

   if(!vk_valid(vkMapMemory(device, memory, 0, allocate_info.allocationSize, 0, &buffer.data)))
      return (vk_buffer){};

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

static VkFormat vk_swapchain_format(VkPhysicalDevice physical_dev, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_dev));
   assert(vk_valid_handle(surface));

   VkSurfaceFormatKHR formats[MAX_VULKAN_OBJECT_COUNT] = {};
   u32 format_count = array_count(formats);
   if(!vk_valid(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_dev, surface, &format_count, formats)))
      return VK_FORMAT_UNDEFINED;

   return formats[0].format;
}

static swapchain_surface_info vk_window_swapchain_surface_info(VkPhysicalDevice physical_dev, u32 width, u32 height, VkSurfaceKHR surface)
{
   assert(vk_valid_handle(physical_dev));
   assert(vk_valid_handle(surface));

   swapchain_surface_info result = {};

   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_dev, surface, &surface_caps)))
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
   result.format = vk_swapchain_format(physical_dev, surface);
   result.surface = surface;

   return result;
}

static VkPhysicalDevice vk_pdevice_select(VkInstance instance)
{
   assert(vk_valid_handle(instance));

   VkPhysicalDevice devs[MAX_VULKAN_OBJECT_COUNT] = {};
   u32 dev_count = array_count(devs);
   vk_test_return_handle(vkEnumeratePhysicalDevices(instance, &dev_count, devs));

   for(u32 i = 0; i < dev_count; ++i)
   {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devs[i], &props);

      if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
         return devs[i];
   }
   if(dev_count > 0)
      return devs[0];
   return 0;
}

static u32 vk_ldevice_select_index()
{
   // placeholder
   return 0;
}

static VkDevice vk_ldevice_create(VkPhysicalDevice physical_dev, u32 queue_family_index)
{
   assert(vk_valid_handle(physical_dev));
   f32 queue_prio = 1.0f;

   VkDeviceQueueCreateInfo queue_info = {vk_info(DEVICE_QUEUE)};
   queue_info.queueFamilyIndex = queue_family_index; // TODO: query the right queue family
   queue_info.queueCount = 1;
   queue_info.pQueuePriorities = &queue_prio;

   VkDeviceCreateInfo ldev_info = {vk_info(DEVICE)};
   const char* dev_ext_names[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

   VkPhysicalDeviceFeatures enabled_features = {};
   enabled_features.depthBounds = VK_TRUE;
   enabled_features.wideLines = VK_TRUE;
   enabled_features.fillModeNonSolid = VK_TRUE;

   ldev_info.queueCreateInfoCount = 1;
   ldev_info.pQueueCreateInfos = &queue_info;
   ldev_info.enabledExtensionCount = array_count(dev_ext_names);
   ldev_info.ppEnabledExtensionNames = dev_ext_names;
   ldev_info.pEnabledFeatures = &enabled_features;

   VkDevice logical_dev;
   vk_test_return_handle(vkCreateDevice(physical_dev, &ldev_info, 0, &logical_dev));

   return logical_dev;
}

static VkQueue vk_graphics_queue_create(VkDevice logical_dev, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_dev));

   VkQueue graphics_queue = 0;

   // TODO: Get the queue index
   vkGetDeviceQueue(logical_dev, queue_family_index, 0, &graphics_queue);

   return graphics_queue;
}

static VkSemaphore vk_semaphore_create(VkDevice logical_dev)
{
   assert(vk_valid_handle(logical_dev));

   VkSemaphore sema = 0;

   VkSemaphoreCreateInfo sema_info = {vk_info(SEMAPHORE)};
   vk_test_return_handle(vkCreateSemaphore(logical_dev, &sema_info, 0, &sema));

   return sema;
}

static VkSwapchainKHR vk_swapchain_create(VkDevice logical_dev, swapchain_surface_info* surface_info, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_dev));
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
   swapchain_info.clipped = VK_TRUE;
   swapchain_info.oldSwapchain = surface_info->swapchain;

   vk_test_return_handle(vkCreateSwapchainKHR(logical_dev, &swapchain_info, 0, &swapchain));

   return swapchain;
}

static swapchain_surface_info vk_swapchain_info_create(vk_context* context, u32 swapchain_width, u32 swapchain_height, u32 queue_family_index)
{
   VkSurfaceCapabilitiesKHR surface_caps;
   if(!vk_valid(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_dev, context->surface, &surface_caps)))
      return (swapchain_surface_info) {};

   swapchain_surface_info swapchain_info = vk_window_swapchain_surface_info(context->physical_dev, swapchain_width, swapchain_height, context->surface);

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

   swapchain_info.swapchain = vk_swapchain_create(context->logical_dev, &swapchain_info, queue_family_index);

   return swapchain_info;
}

static VkCommandBuffer vk_command_buffer_create(VkDevice logical_dev, VkCommandPool pool)
{
   assert(vk_valid_handle(logical_dev));
   assert(vk_valid_handle(pool));

   VkCommandBuffer buffer = 0;
   VkCommandBufferAllocateInfo buffer_allocate_info = {vk_info_allocate(COMMAND_BUFFER)};

   buffer_allocate_info.commandBufferCount = 1;
   buffer_allocate_info.commandPool = pool;
   buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

   vk_assert(vkAllocateCommandBuffers(logical_dev, &buffer_allocate_info, &buffer));

   return buffer;
}

static VkCommandPool vk_command_pool_create(VkDevice logical_dev, u32 queue_family_index)
{
   assert(vk_valid_handle(logical_dev));

   VkCommandPool pool = 0;

   VkCommandPoolCreateInfo pool_info = {vk_info(COMMAND_POOL)};
   pool_info.queueFamilyIndex = queue_family_index;

   vk_test_return_handle(vkCreateCommandPool(logical_dev, &pool_info, 0, &pool));

   return pool;
}

static VkImage vk_depth_image_create(VkDevice logical_dev, VkPhysicalDevice physical_dev, VkFormat format, VkExtent3D extent)
{
   VkImage result = 0;

   // Image creation info structure
   VkImageCreateInfo image_info = {};
   image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.imageType = VK_IMAGE_TYPE_2D;  // 2D depth image
   image_info.extent = extent;              // Width, height, depth
   image_info.mipLevels = 1;
   image_info.arrayLayers = 1;
   image_info.format = format;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Initial layout
   image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;  // Depth usage
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;  // No multisampling
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.queueFamilyIndexCount = 0;
   image_info.pQueueFamilyIndices = 0;

   // Create the depth image
   if(vkCreateImage(logical_dev, &image_info, 0, &result) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   // Allocate memory for the image
   VkMemoryRequirements memory_requirements;
   vkGetImageMemoryRequirements(logical_dev, result, &memory_requirements);

   // Allocate memory (find suitable memory type)
   VkMemoryAllocateInfo alloc_info = {};
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.allocationSize = memory_requirements.size;

   // Find a memory type that supports depth-stencil attachments
   VkPhysicalDeviceMemoryProperties memory_properties;
   vkGetPhysicalDeviceMemoryProperties(physical_dev, &memory_properties);

   uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
   for(uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
      if((memory_requirements.memoryTypeBits & (1 << i)) &&
          (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
         memory_type_index = i;
         break;
      }
   }
   if(memory_type_index == VK_MAX_MEMORY_TYPES)
      return VK_NULL_HANDLE;

   alloc_info.memoryTypeIndex = memory_type_index;

   VkDeviceMemory memory;
   if(vkAllocateMemory(logical_dev, &alloc_info, 0, &memory) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   // Bind the memory to the image
   if(vkBindImageMemory(logical_dev, result, memory, 0) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return result;
}


static VkRenderPass vk_renderpass_create(VkDevice logical_dev, VkFormat color_format, VkFormat depth_format)
{
   assert(vk_valid_handle(logical_dev));
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

   // TODO: pass the depth format too
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

   vk_test_return_handle(vkCreateRenderPass(logical_dev, &renderpass_info, 0, &renderpass));

   return renderpass;
}

static VkFramebuffer vk_framebuffer_create(VkDevice logical_dev, VkRenderPass renderpass, swapchain_surface_info* surface_info, VkImageView* attachments, u32 attachment_count)
{
   VkFramebuffer framebuffer = 0;

   VkFramebufferCreateInfo framebuffer_info = {vk_info(FRAMEBUFFER)};
   framebuffer_info.renderPass = renderpass;
   framebuffer_info.attachmentCount = attachment_count;
   framebuffer_info.pAttachments = attachments;
   framebuffer_info.width = surface_info->image_width;
   framebuffer_info.height = surface_info->image_height;
   framebuffer_info.layers = 1;

   vk_test_return_handle(vkCreateFramebuffer(logical_dev, &framebuffer_info, 0, &framebuffer));

   return framebuffer;
}

static VkImageView vk_image_view_create(VkDevice logical_dev, VkFormat format, VkImage image, VkImageAspectFlags aspect_mask)
{
   assert(vk_valid_handle(logical_dev));
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

   vk_test_return_handle(vkCreateImageView(logical_dev, &view_info, 0, &image_view));

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
   vkDeviceWaitIdle(context->logical_dev);
   for(u32 i = 0; i < context->swapchain_info.image_count; ++i)
   {
      vkDestroyFramebuffer(context->logical_dev, context->framebuffers[i], 0);
      vkDestroyImageView(context->logical_dev, context->swapchain_info.image_views[i], 0);
      vkDestroyImageView(context->logical_dev, context->swapchain_info.depth_views[i], 0);
   }

   vkDestroySwapchainKHR(context->logical_dev, context->swapchain_info.swapchain, 0);
}

static bool vk_swapchain_update(vk_context* context)
{
   vk_test_return(vkGetSwapchainImagesKHR(context->logical_dev, context->swapchain_info.swapchain, &context->swapchain_info.image_count, context->swapchain_info.images));

   VkExtent3D depth_extent = {context->swapchain_info.image_width, context->swapchain_info.image_height, 1};

   for(u32 i = 0; i < context->swapchain_info.image_count; ++i)
   {
      context->swapchain_info.depths[i] = vk_depth_image_create(context->logical_dev, context->physical_dev, VK_FORMAT_D32_SFLOAT, depth_extent);

      context->swapchain_info.image_views[i] = vk_image_view_create(context->logical_dev, context->swapchain_info.format, context->swapchain_info.images[i], VK_IMAGE_ASPECT_COLOR_BIT);
      context->swapchain_info.depth_views[i] = vk_image_view_create(context->logical_dev, VK_FORMAT_D32_SFLOAT, context->swapchain_info.depths[i], VK_IMAGE_ASPECT_DEPTH_BIT);

      VkImageView attachments[2] = {context->swapchain_info.image_views[i], context->swapchain_info.depth_views[i]};

      context->framebuffers[i] = vk_framebuffer_create(context->logical_dev, context->renderpass, &context->swapchain_info, attachments, array_count(attachments));
   }

   return true;
}

void vk_resize(void* renderer, u32 width, u32 height)
{
   if(width == 0 || height == 0)
      return;

   vk_context* context = (vk_context*)renderer;

   vkDeviceWaitIdle(context->logical_dev);
   vk_swapchain_destroy(context);
   context->swapchain_info = vk_swapchain_info_create(context, width, height, context->queue_family_index);
   vk_swapchain_update(context);
}

void vk_present(vk_context* context)
{
   u32 image_index = 0;
   VkResult next_image_result = vkAcquireNextImageKHR(context->logical_dev, context->swapchain_info.swapchain, UINT64_MAX, context->image_ready_semaphore, VK_NULL_HANDLE, &image_index);

   if(next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(context, context->swapchain_info.image_width, context->swapchain_info.image_height);

   if(next_image_result != VK_SUBOPTIMAL_KHR && next_image_result != VK_SUCCESS)
      return;

   vk_assert(vkResetCommandPool(context->logical_dev, context->command_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   VkRenderPassBeginInfo renderpass_info = {vk_info_begin(RENDER_PASS)};
   renderpass_info.renderPass = context->renderpass;
   renderpass_info.framebuffer = context->framebuffers[image_index];
   renderpass_info.renderArea.extent = (VkExtent2D)
   {context->swapchain_info.image_width, context->swapchain_info.image_height};

   VkCommandBuffer command_buffer = context->command_buffer;
   {
      vk_assert(vkBeginCommandBuffer(command_buffer, &buffer_begin_info));

      const f32 ar = (f32)context->swapchain_info.image_width / context->swapchain_info.image_height;

      f32 delta = 1.5f;
      static f32 rot = 0.0f;
      static f32 originz = -10.0f;
      static f32 cameraz = 0.0f;

      if(originz > 1.0f)
         originz = -10.0f;

      cameraz = sinf(rot/30)*4;

      rot += delta;
      originz += delta/4;

      mvp_transform mvp = {};

      mvp.n = 0.01f;
      mvp.f = 1000.0f;
      mvp.ar = ar;

      float radius = 5.0f;
      float theta = DEG2RAD(rot);

      vec3 eye = {
          radius * cosf(theta/2.0f),
          radius * 0.5f * cosf(theta*2.0f),
          //1.0f,
          radius * sinf(theta/2.0f),
      };

      vec3 center = {0.0f, 0.0f, 0.0f};
      vec3 dir = vec3_sub(&eye, &center);

      mvp.projection = mat4_perspective(ar, 90.0f, mvp.n, mvp.f);
      //mvp.view = mat4_view((vec3){0.0f, 2.0f, 4.0f}, (vec3){0.0f, 0.0f, -1.0f});
      mvp.view = mat4_view(eye, dir);
      mat4 translate = mat4_translate((vec3){0.0f, 0.0f, 0.0f});

      mvp.model = mat4_identity();
      //mvp.model = mat4_mul(mat4_mul(mat4_rotation_z(rot/2.0f), mat4_rotation_x(rot/4.0f)), mvp.model);
      mvp.model = mat4_mul(translate, mvp.model);

      const f32 c = 255.0f;
      VkClearValue clear[2] = {};
      clear[0].color = (VkClearColorValue){48 / c, 10 / c, 36 / c, 1.0f};
      clear[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};

      renderpass_info.clearValueCount = 2;
      renderpass_info.pClearValues = clear;

      VkImage color_image = context->swapchain_info.images[image_index];
      VkImageMemoryBarrier color_image_begin_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT ,0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_begin_barrier);

      //VkImage depth_image = context->swapchain_info.depths[image_index];
      //VkImageMemoryBarrier depth_image_begin_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      //vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                           //VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_begin_barrier);


      vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

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

      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->cube_pipeline);

      const VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(command_buffer, 0, 1, &context->vb.handle, &offset);
      vkCmdBindIndexBuffer(command_buffer, context->ib.handle, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(command_buffer, context->index_count, 1, 0, 0, 0);

      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->axes_pipeline);

      vkCmdDraw(command_buffer, 18, 1, 0, 0);

      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->frustum_pipeline);

      vkCmdDraw(command_buffer, 12, 1, 0, 0);

      vkCmdEndRenderPass(command_buffer);

      VkImageMemoryBarrier color_image_end_barrier = vk_pipeline_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                           VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &color_image_end_barrier);

      //VkImageMemoryBarrier depth_image_end_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
      //vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           //VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_end_barrier);

      vk_assert(vkEndCommandBuffer(command_buffer));
   }

   VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
   submit_info.waitSemaphoreCount = 1;
   submit_info.pWaitSemaphores = &context->image_ready_semaphore;

   submit_info.pWaitDstStageMask = &(VkPipelineStageFlags) { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

   // add more command buffers here
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
      vk_resize(context, context->swapchain_info.image_width, context->swapchain_info.image_height);

   if(present_result != VK_SUCCESS)
      return;

   // wait until all queue ops are done
   // TODO: This is bad way to do sync but who cares for now
   vk_assert(vkDeviceWaitIdle(context->logical_dev));
}

static vk_shader_modules vk_shaders_load(VkDevice logical_dev, arena scratch, const char* shader_name)
{
   assert(vk_valid_handle(logical_dev));

   vk_shader_modules shader_modules = {};
   VkShaderStageFlagBits shader_type_bits[OBJECT_SHADER_COUNT] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};

   file_result project_dir = vk_project_directory(&scratch);

   for(u32 i = 0; i < OBJECT_SHADER_COUNT; ++i)
   {
      file_result shader_file = vk_shader_spv_read(&scratch, project_dir.data, shader_name, shader_type_bits[i]);
      if(shader_file.file_size == 0)
         return (vk_shader_modules) {};

      VkShaderModuleCreateInfo module_info = {};
      module_info.sType = vk_info(SHADER_MODULE);
      module_info.pCode = (u32*)shader_file.data;
      module_info.codeSize = shader_file.file_size;

      if(!vk_valid(vkCreateShaderModule(logical_dev,
         &module_info,
         0,
         shader_type_bits[i] == VK_SHADER_STAGE_VERTEX_BIT ? &shader_modules.vs : &shader_modules.fs)))
         return (vk_shader_modules) {};
   }

   return shader_modules;
}

static VkPipelineLayout vk_pipeline_layout_create(VkDevice logical_dev)
{
   VkPipelineLayout layout = 0;

   VkPipelineLayoutCreateInfo info = {vk_info(PIPELINE_LAYOUT)};

   VkPushConstantRange push_constants = {};
   push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
   push_constants.offset = 0;
   push_constants.size = sizeof(mvp_transform);

   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = &push_constants;

   vk_test_return(vkCreatePipelineLayout(logical_dev, &info, 0, &layout));

   return layout;
}

// TODO: Cleanup these pipelines
static VkPipeline vk_cube_pipeline_create(VkDevice logical_dev, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_dev));
   assert(vk_valid_handle(shaders->fs));
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

   VkVertexInputBindingDescription stream = {};
   stream.binding = 0;
   stream.stride = sizeof(tinyobj_vertex);
   stream.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

   VkVertexInputAttributeDescription attributes[3] = {};   // pos, normal, uv
   attributes[0].location = 0;
   attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
   attributes[0].offset = 0;

   attributes[1].location = 1;
   attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
   attributes[1].offset = 12;

   attributes[2].location = 2;
   attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
   attributes[2].offset = 24;

   vertex_input_info.vertexBindingDescriptionCount = 1;
   vertex_input_info.pVertexBindingDescriptions = &stream;
   vertex_input_info.vertexAttributeDescriptionCount = array_count(attributes);
   vertex_input_info.pVertexAttributeDescriptions = attributes;

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
   depth_stencil_info.depthTestEnable = VK_TRUE;
   depth_stencil_info.depthWriteEnable = VK_TRUE;
   depth_stencil_info.depthBoundsTestEnable = VK_TRUE;
   depth_stencil_info.stencilTestEnable = VK_TRUE;
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

   vk_test_return(vkCreateGraphicsPipelines(logical_dev, cache, 1, &pipeline_info, 0, &pipeline));

   return pipeline;
}

static VkPipeline vk_frustum_pipeline_create(VkDevice logical_dev, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_dev));
   assert(vk_valid_handle(shaders->fs));
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
   assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
   pipeline_info.pInputAssemblyState = &assembly_info;

   VkPipelineViewportStateCreateInfo viewport_info = {vk_info(PIPELINE_VIEWPORT_STATE)};
   viewport_info.scissorCount = 1;
   viewport_info.viewportCount = 1;
   pipeline_info.pViewportState = &viewport_info;

   VkPipelineRasterizationStateCreateInfo raster_info = {vk_info(PIPELINE_RASTERIZATION_STATE)};
   raster_info.lineWidth = 1.0f;
   raster_info.cullMode = VK_CULL_MODE_NONE;
   raster_info.polygonMode = VK_POLYGON_MODE_FILL;
   raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   pipeline_info.pRasterizationState = &raster_info;

   VkPipelineMultisampleStateCreateInfo sample_info = {vk_info(PIPELINE_MULTISAMPLE_STATE)};
   sample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   pipeline_info.pMultisampleState = &sample_info;

   VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {vk_info(PIPELINE_DEPTH_STENCIL_STATE)};
   depth_stencil_info.depthTestEnable = VK_TRUE;
   depth_stencil_info.depthWriteEnable = VK_TRUE;
   depth_stencil_info.depthBoundsTestEnable = VK_TRUE;
   depth_stencil_info.stencilTestEnable = VK_TRUE;
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

   vk_test_return(vkCreateGraphicsPipelines(logical_dev, cache, 1, &pipeline_info, 0, &pipeline));

   return pipeline;

}

static VkPipeline vk_axis_pipeline_create(VkDevice logical_dev, VkRenderPass renderpass, VkPipelineCache cache, VkPipelineLayout layout, const vk_shader_modules* shaders)
{
   assert(vk_valid_handle(logical_dev));
   assert(vk_valid_handle(shaders->fs));
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
   depth_stencil_info.depthBoundsTestEnable = VK_TRUE;
   depth_stencil_info.depthTestEnable = VK_TRUE;
   depth_stencil_info.depthWriteEnable = VK_TRUE;
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

   vk_test_return(vkCreateGraphicsPipelines(logical_dev, cache, 1, &pipeline_info, 0, &pipeline));

   return pipeline;
}

const char** vk_extension_names_storage(arena* storage, size ext_count)
{
   set_arena_type(const char*);
   arena_invariant(ext_count, storage, arena_type);

   return new(storage, arena_type, ext_count);
}

VkInstance vk_instance_create(arena scratch)
{
   set_arena_type(VkExtensionProperties);

   VkInstance instance = 0;

   u32 ext_count = 0;
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, 0)))
      return 0;

   if(scratch_left(scratch, arena_type) < ext_count)
      return 0;

   arena_type* extensions = new(&scratch, arena_type, ext_count);
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, extensions)))
      return 0;

   if(scratch_left(scratch, const char*) < ext_count)
      return 0;

   const char** ext_names = vk_extension_names_storage(&scratch, ext_count);

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
   vk_test_return(vkCreateInstance(&instance_info, 0, &instance));

   return instance;
}

bool vk_initialize(hw* hw)
{
   if(!hw->renderer.window.handle)
      return false;

   if(!vk_valid(volkInitialize()))
      return false;

   set_arena_type(vk_context);
   size contexts_left = scratch_left(hw->vk_storage, arena_type);

   if(contexts_left < 1)
      return false;

   scratch_invariant(1, hw->vk_storage, arena_type);

   arena_type* context = new(&hw->vk_storage, arena_type);

   context->storage = &hw->vk_storage;

   arena scratch = hw->vk_scratch;

   VkInstance instance = vk_instance_create(scratch);

   if(!instance)
      return 0;

   volkLoadInstance(instance);

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

   context->queue_family_index = vk_ldevice_select_index();
   context->physical_dev = vk_pdevice_select(instance);
   context->logical_dev = vk_ldevice_create(context->physical_dev, context->queue_family_index);
   context->surface = hw->renderer.window_surface_create(instance, hw->renderer.window.handle);
   context->image_ready_semaphore = vk_semaphore_create(context->logical_dev);
   context->image_done_semaphore = vk_semaphore_create(context->logical_dev);
   context->graphics_queue = vk_graphics_queue_create(context->logical_dev, context->queue_family_index);
   context->command_pool = vk_command_pool_create(context->logical_dev, context->queue_family_index);
   context->command_buffer = vk_command_buffer_create(context->logical_dev, context->command_pool);
   context->swapchain_info = vk_swapchain_info_create(context, hw->renderer.window.width, hw->renderer.window.height, context->queue_family_index);
   context->renderpass = vk_renderpass_create(context->logical_dev, context->swapchain_info.format, VK_FORMAT_D32_SFLOAT);

   VkExtent3D depth_extent = {context->swapchain_info.image_width, context->swapchain_info.image_height, 1};
   for(u32 i = 0; i < context->swapchain_info.image_count; ++i)
      context->swapchain_info.depths[i] = vk_depth_image_create(context->logical_dev, context->physical_dev, VK_FORMAT_D32_SFLOAT, depth_extent);

   if(!vk_swapchain_update(context))
      return false;

   const char* shader_names[] = {"cube", "axis", "frustum"};
   vk_shader_modules shaders[array_count(shader_names)];

   for(u32 i = 0; i < array_count(shader_names); ++i)
      shaders[i] = vk_shaders_load(context->logical_dev, scratch, shader_names[i]);

   VkPipelineCache cache = 0; // TODO: enable
   VkPipelineLayout layout = vk_pipeline_layout_create(context->logical_dev);
   // TODO: Cleanup this
   context->cube_pipeline = vk_cube_pipeline_create(context->logical_dev, context->renderpass, cache, layout, &shaders[0]);
   context->axes_pipeline = vk_axis_pipeline_create(context->logical_dev, context->renderpass, cache, layout, &shaders[1]);
   context->frustum_pipeline = vk_frustum_pipeline_create(context->logical_dev, context->renderpass, cache, layout, &shaders[2]);
   context->pipeline_layout = layout;

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->physical_dev, &memory_props);

   size buffer_size = MB(10);
   vk_buffer index_buffer = vk_buffer_create(context->logical_dev, buffer_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
   vk_buffer vertex_buffer = vk_buffer_create(context->logical_dev, buffer_size, memory_props, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

   // valid gpu buffers
   if(index_buffer.handle == VK_NULL_HANDLE || vertex_buffer.handle == VK_NULL_HANDLE)
      return false;

   context->vb = vertex_buffer;
   context->ib = index_buffer;

 // tinyobj 
   {
      const char* filename = "cube.obj";
      //const char* filename = "cornell_box.obj";

      tinyobj_shape_t* shapes = 0;
      tinyobj_material_t* materials = 0;
      tinyobj_attrib_t attrib = {};

      size_t shape_count = 0;
      size_t material_count = 0;

      tinyobj_attrib_init(&attrib);

      tinyobj_user_ctx user_data = {};
      // user data scratch arena
      user_data.scratch = scratch;

      if(!tinyobj_parse_obj(&attrib, &shapes, &shape_count, &materials, &material_count, filename, tinyobj_file_read, &user_data, TINYOBJ_FLAG_TRIANGULATE) == TINYOBJ_SUCCESS)
         return false;

      usize index_count = attrib.num_faces;

      arena index_scratch = scratch;
      scratch_shrink(index_scratch, index_count, tinyobj_vertex);

      tinyobj_vertex* verts = new(&index_scratch, tinyobj_vertex, index_count);
      u32 unique_vertex_count = 0;

      for(usize i = 0; i < index_count; ++i)
      {
         tinyobj_vertex v = {};
         int vi = attrib.faces[i].v_idx;
         int vti = attrib.faces[i].vt_idx;
         int vni = attrib.faces[i].vn_idx;

         if(vi >= 0)
         {
            v.vx = attrib.vertices[vi * 3 + 0];
            v.vy = attrib.vertices[vi * 3 + 1];
            v.vz = attrib.vertices[vi * 3 + 2];
         }

         if(vni >= 0)
         {
            v.nx = attrib.normals[vni * 3 + 0];
            v.ny = attrib.normals[vni * 3 + 1];
            v.nz = attrib.normals[vni * 3 + 2];
         }

         if(vti >= 0)
         {
            v.tu = attrib.texcoords[vti * 2 + 0];
            v.tv = attrib.texcoords[vti * 2 + 1];
         }

         int index = vertex_get(&v, verts, unique_vertex_count);
         if(index == -1)
         {
            index = unique_vertex_count;
            verts[index] = v;
            unique_vertex_count++;
         }

         ((u32*)context->ib.data)[i] = index;
      }

      // load vb
      memcpy(context->vb.data, verts, unique_vertex_count*sizeof(tinyobj_vertex));

#if 0
      index_scratch = scratch;
      scratch_shrink(index_scratch, index_count, u32);

      // TOOD: Enable once the index buffer shit works
      //u32* indices = new(&index_scratch, u32, index_count);

      u32 face_index = 0;
      for(u32 i = 0; indices + i != (u32*)index_scratch.end; ++i)
      {
         indices[i] = i;
      }

      // load ib
      //memcpy(context->ib.data, indices, index_count*sizeof(u32));
#endif
      context->index_count = (u32)index_count;
   }

   // app callbacks
   hw->renderer.backends[vk_renderer_index] = context;
   hw->renderer.frame_present = vk_present;
   hw->renderer.frame_resize = vk_resize;
   hw->renderer.renderer_index = vk_renderer_index;

   return true;
}

bool vk_uninitialize(hw* hw)
{
   // TODO:
#if 0
   vk_context* context = hw->renderer.backends[vk_renderer_index];

   vkDestroySwapchainKHR(context->logical_dev, context->swapchain, 0);
   vkDestroySurfaceKHR(context->instance, context->surface, 0);
   vkDestroyDevice(context->logical_dev, 0);
   vkDestroyInstance(context->instance, 0);
#endif

   return true;
}
