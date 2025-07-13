#if !defined(_VULKAN_NG_H)
#define _VULKAN_NG_H

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#pragma comment(lib,	"vulkan-1.lib")
#elif
// other plats for vulkan
#endif

#include <volk.h>

#include "common.h"
#include "arena.h"

#include "../assets/shaders/mesh.h"

// TODO: make vulkan_ng a shared lib
bool vk_uninitialize(struct hw* hw);
bool vk_initialize(struct hw* hw);

#define vk_valid_handle(v) ((v) != VK_NULL_HANDLE)
#define vk_valid_format(v) ((v) != VK_FORMAT_UNDEFINED)

#define vk_valid(v) ((v) == VK_SUCCESS)

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

// TODO: Remove these and use arena arrays
enum { MAX_VULKAN_OBJECT_COUNT = 16, OBJECT_SHADER_COUNT = 2 };   // For mesh shading - ms and fs, for regular pipeline - vs and fs

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
   VkBuffer handle;
   VkDeviceMemory memory;
   void* data; // host or local memory
   usize size;
} vk_buffer;

align_struct
{
   vk_buffer buffer;
   u32 count;
} vk_meshlet;

typedef struct meshlet meshlet;
align_struct geometry
{
   array(meshlet) meshlets;
} geometry;

align_struct
{
   size index_offset;
   size index_count;
   size vertex_offset;
} mesh_draw;

align_struct
{
   // TODO: array(VkFramebuffer)
   VkFramebuffer framebuffers[MAX_VULKAN_OBJECT_COUNT];
   array(mesh_draw) mesh_draws;

   VkInstance instance;
   VkPhysicalDevice physical_device;
   VkDevice logical_device;
   VkSurfaceKHR surface;
   VkAllocationCallbacks* allocator;
   VkSemaphore image_ready_semaphore;
   VkSemaphore image_done_semaphore;
   VkQueue graphics_queue;
   VkCommandPool command_pool;
   VkQueryPool query_pool;
   u32 query_pool_size;
   VkCommandBuffer command_buffer;
   VkRenderPass renderpass;

   VkPipeline rtx_pipeline;
   VkPipeline graphics_pipeline;
   VkPipeline axis_pipeline;
   VkPipeline frustum_pipeline;

   VkPipelineLayout pipeline_layout;
   VkPipelineLayout rtx_pipeline_layout;

   vk_buffer vb;        // vertex buffer
   vk_buffer ib;        // index buffer
   vk_buffer mb;        // mesh buffer

   u32 max_meshlet_count;

   // TODO: array(meshlet)
   u32 meshlet_count;
   u32 index_count;

   swapchain_surface_info swapchain_info;

   arena* storage;

   u32 queue_family_index;
   f32 time_period;

   size geometry_buffer_size;

   bool rtx_supported;
} vk_context;

static void vk_buffer_upload(VkDevice device, VkQueue queue, VkCommandBuffer cmd_buffer, VkCommandPool cmd_pool, vk_buffer buffer, vk_buffer scratch, const void* data, VkDeviceSize size);

#endif
