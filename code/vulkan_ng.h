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
#include "free_list.h"
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
          assert(vk_valid((_r))); \
        } while(0)
#else
#define vk_assert(v) (v)
#endif

align_struct vk_swapchain_surface
{
   u32 image_width;
   u32 image_height;
   u32 image_count;
   VkFormat format;
   VkSwapchainKHR handle;
} vk_swapchain_surface;

align_struct vk_image
{
   VkImage handle;
   VkImageView view;
   VkDeviceMemory memory;
} vk_image;

align_struct vk_swapchain_images
{
   array(vk_image) images;
   array(vk_image) depths;
} vk_swapchain_images;

align_struct vk_buffer
{
   VkBuffer handle;
   VkDeviceMemory memory;
   void* data; // host or local memory
   size size;
} vk_buffer;

align_struct vk_buffer_binding
{
   vk_buffer buffer;
   u32 binding;
   VkDescriptorType type;
   void* extras;
} vk_buffer_binding;

typedef struct meshlet meshlet;

typedef array(meshlet) array_meshlet;

align_struct vk_mesh_instance
{
   u32 mesh_index;  // which vk_mesh_draw this instance draws
   u32 albedo; 
   u32 normal;
   u32 ao; 
   u32 metal;
   u32 emissive;
   u32 index_offset;
   mat4 world;
} vk_mesh_instance;

align_struct vk_mesh_draw
{
   // TODO: u32 sizes?
   size index_offset;
   size index_count;
   size vertex_count;
   size vertex_offset;
} vk_mesh_draw;

align_struct vk_texture
{
   vk_image image;
} vk_texture;

align_struct vk_descriptor
{
   VkDescriptorPool descriptor_pool;
   VkDescriptorSet set;
   VkDescriptorSetLayout layout;
} vk_descriptor;

align_struct vk_pipeline
{
   VkPipeline pipeline;
   VkPipelineLayout layout; 
} vk_pipeline;

align_struct vk_buffer_hash_table
{
   struct vk_buffer* values;
   const char** keys;
   size max_count;
   size count;
} vk_buffer_hash_table;

align_struct vk_device
{
   VkPhysicalDevice physical;
   VkDevice logical;
   VkInstance instance;
   size queue_family_index;
} vk_device;

align_struct ctx_shader_destroy
{
   vk_device* devices;
} ctx_shader_destroy;

align_struct vk_geometry
{
   array(vk_mesh_draw) mesh_draws;
   array(vk_mesh_instance) mesh_instances;
} vk_geometry; 

align_struct vk_cmd
{
   VkCommandBuffer buffer;
   VkCommandPool pool;
} vk_cmd;

align_struct vk_rt_as
{
   VkAccelerationStructureKHR* blases;
   VkAccelerationStructureKHR tlas;
   size blas_count;   // TODO: array
} vk_rt_as;

align_struct vk_features
{
   bool mesh_shading_supported;
   bool raytracing_supported;
   f32 time_period;
} vk_features;

// TODO: change to hw_gpu_allocator and add to hw.h
align_struct vk_allocator
{
   VkAllocationCallbacks handle;
   list slots;
   arena* a;
} vk_allocator;

typedef array(VkFramebuffer) framebuffers_array;
align_struct vk_context
{
   framebuffers_array framebuffers;
   array(vk_texture) textures;

   array(meshlet) meshlets;

   array(size) meshlet_counts;
   array(size) meshlet_offsets;
   array(size) vertex_offsets;

   vk_rt_as rt_as;

   vk_geometry geometry;

   VkDescriptorSetLayout non_rtx_set_layout;
   VkDescriptorSetLayout rtx_set_layout;

   vk_device devices;

   VkSurfaceKHR surface;
   u32 query_pool_size;

   vk_descriptor texture_descriptor;

   VkSemaphore image_ready_semaphore;
   VkSemaphore image_done_semaphore;
   u32 image_index;

   VkQueue graphics_queue;
   VkQueryPool query_pool;

   vk_cmd cmd;

   VkRenderPass renderpass;

   // TODO: vk_pipeline
   VkPipeline rtx_pipeline;
   VkPipeline non_rtx_pipeline;
   VkPipeline axis_pipeline;
   VkPipeline frustum_pipeline;

   VkPipelineLayout non_rtx_pipeline_layout;
   VkPipelineLayout rtx_pipeline_layout;

   spv_hash_table shader_table;

   vk_buffer_hash_table buffer_table;

   vk_swapchain_surface swapchain;
   vk_swapchain_images images;

   arena* app_storage;
   arena* vulkan_storage;
   arena scratch;

#ifdef _DEBUG
   VkDebugUtilsMessengerEXT messenger;
#endif

   vk_features features;
} vk_context;

typedef struct hw hw;
bool vk_initialize(hw* hw);
void vk_uninitialize(hw* hw);

// TODO: these in buffer.h
static void vk_buffer_upload(vk_context* context, vk_buffer* to, const void* data);
static void vk_buffer_to_image_upload(vk_context* context, vk_buffer scratch, VkImage image, VkExtent3D image_extent, const void* data, VkDeviceSize size);
static void vk_buffer_destroy(vk_device* device, vk_buffer* buffer);
// TODO: pass the size for the buffer to be created instead of embedding it inside the buffer
// TODO: (vk_buffer* buffer, size buffer_size, vk_device* device, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags)
static bool vk_buffer_create_and_bind(vk_buffer* buffer, vk_device* device, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags);

#endif
