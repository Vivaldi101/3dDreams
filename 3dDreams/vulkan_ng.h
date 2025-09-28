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
#include "vulkan_shader_module.h"

#include "../assets/shaders/mesh.h"

#define vk_valid_handle(v) ((v) != VK_NULL_HANDLE)
#define vk_valid_format(v) ((v) != VK_FORMAT_UNDEFINED)

#define vk_valid(v) ((v) == VK_SUCCESS)

#define vk_error(s) printf("Vulkan error:" #s)

// most vk used types
#define vk_info(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO
#define vk_info_allocate(i) VK_STRUCTURE_TYPE_##i##_ALLOCATE_INFO
#define vk_info_khr(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO_KHR
#define vk_info_ext(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO_EXT

#define vk_info_begin(i) VK_STRUCTURE_TYPE_##i##_BEGIN_INFO
#define vk_info_end(i) VK_STRUCTURE_TYPE_##i##_END_INFO

// currently we just assert for lot of the commands - we should expand the contract to return null handle on failure and just use this to assert invariants on vulkan state?
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
typedef enum file_format { FILE_FORMAT_OBJ = 0, FILE_FORMAT_GLTF = 1 } file_format;

align_struct
{
   u32 image_width;
   u32 image_height;
   u32 image_count;
   VkFormat format;
   VkSwapchainKHR handle;
} vk_swapchain_surface;

align_struct
{
   array(VkImage) images;
   array(VkImage) depths;
   array(VkImageView) image_views;
   array(VkImageView) depth_views;
} vk_swapchain_images;

align_struct
{
   VkBuffer handle;
   VkDeviceMemory memory;
   void* data; // host or local memory
   size size;
} vk_buffer;

align_struct
{
   vk_buffer vb;        // vertex buffer
   vk_buffer ib;        // index buffer
   vk_buffer mb;        // mesh buffer
   vk_buffer indirect;        // indirect rendering
   vk_buffer indirect_rtx;    // indirect rendering
   vk_buffer world_transform; // world transform
} vk_buffer_objects;

align_struct
{
   vk_buffer buffer;
   u32 binding;
} vk_buffer_binding;

align_struct
{
   vk_buffer buffer;
   u32 count;
} vk_meshlet;

align_struct
{
   VkImage handle;
   VkImageView view;
   VkDeviceMemory memory;
} vk_image;

typedef struct meshlet meshlet;
align_struct
{
   array(meshlet) meshlets;
} vk_meshlet_buffer;

align_struct
{
   u32 mesh_index;  // which mesh this instance draws
   u32 albedo; 
   u32 normal;
   u32 ao; 
   u32 metal;
   u32 emissive;
   mat4 world;
} vk_mesh_instance;

align_struct
{
   // TODO: u32 sizes?
   size index_offset;
   size index_count;
   size vertex_offset;
} vk_mesh_draw;

align_struct
{
   array(char) path;
   vk_image image;
} vk_texture;

align_struct
{
   VkDescriptorSet set;
   VkDescriptorSetLayout layout;
} vk_descriptor;

align_struct
{
   VkPipeline pipeline;
   VkPipelineLayout layout; 
} vk_pipeline;

align_struct
{
   array(VkFramebuffer) framebuffers;
   array(vk_mesh_draw) mesh_draws;
   array(vk_mesh_instance) mesh_instances;
   array(vk_texture) textures;

   VkInstance instance;
   // devices into separate struct
   VkPhysicalDevice physical_device;
   VkDevice logical_device;
   VkSurfaceKHR surface;
   u32 query_pool_size;

   VkAllocationCallbacks allocator;

   vk_descriptor texture_descriptor;

   VkSemaphore image_ready_semaphore;
   VkSemaphore image_done_semaphore;

   VkQueue graphics_queue;
   VkCommandPool command_pool;
   VkQueryPool query_pool;
   VkCommandBuffer command_buffer;
   VkRenderPass renderpass;

   VkPipeline rtx_pipeline;
   VkPipeline non_rtx_pipeline;
   VkPipeline axis_pipeline;
   VkPipeline frustum_pipeline;

   VkPipelineLayout non_rtx_pipeline_layout;
   VkPipelineLayout rtx_pipeline_layout;

   spv_hash_table shader_modules;

   vk_buffer_objects bos; // buffer objects

   u32 meshlet_count;

   vk_swapchain_surface swapchain_surface;
   vk_swapchain_images swapchain_images;

   arena* storage;

   u32 queue_family_index;
   f32 time_period;

   bool rtx_supported;
} vk_context;

void vk_initialize(hw* hw);
void vk_uninitialize(hw* hw);

typedef struct vertex vertex;
// TODO: these in buffer.h
static void vk_buffer_upload(vk_context* context, vk_buffer buffer, vk_buffer scratch, const void* data, VkDeviceSize dev_size);
static void vk_buffer_to_image_upload(vk_context* context, vk_buffer scratch, VkImage image, VkExtent3D image_extent, const void* data, VkDeviceSize size);
static void vk_buffer_destroy(VkDevice device, vk_buffer* buffer);
static vk_buffer vk_buffer_create(vk_context* context, size size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags);

#endif
