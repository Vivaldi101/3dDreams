#include "arena.h"
#include "common.h"
#include "graphics.h"

#include <volk.c>

#include "vulkan_ng.h"

#include "hash.c"
#include "win32_file_io.c"
#include "vulkan_spirv_loader.c"

#pragma comment(lib,	"vulkan-1.lib")

#define RTX 1

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "../extern/tinyobjloader-c/tinyobj_loader_c.h"

typedef struct 
{
   arena scratch;
} obj_user_ctx;

typedef struct 
{
   f32 vx, vy, vz;   // pos
   u8 nx, ny, nz;   // normal
   f32 tu, tv;       // texture
} obj_vertex;

typedef struct 
{
   u32 vertex_index_buffer[64];  // vertex indices into the main vertex buffer
   u8 primitive_indices[126];    // 42 triangles (primitives) into the above buffer
   u8 triangle_count;
   u8 vertex_count;
} meshlet;

typedef struct 
{
   size vertex_count;
   u32* index_buffer;         // vertex indices
   size index_count;
   meshlet* meshlet_buffer;   // TODO: arenas here
   u32 meshlet_count;
} mesh;

static void meshlet_add_new_vertex_index(u32 index, u8* meshlet_vertices, meshlet* ml)
{
   if(meshlet_vertices[index] == 0xff)
   {
      meshlet_vertices[index] = ml->vertex_count;

      assert(meshlet_vertices[index] < array_count(ml->vertex_index_buffer));
      // store index into the main vertex buffer
      ml->vertex_index_buffer[meshlet_vertices[index]] = index;
      ml->vertex_count++;
   }
}

static bool meshlet_build(arena* meshlet_storage, arena meshlet_scratch, mesh* m)
{
   meshlet ml = {};

   size vertex_count = m->vertex_count;

   u8* meshlet_vertices = new(&meshlet_scratch, u8, vertex_count).beg;
   if(!meshlet_vertices)
      return false;

   // 0xff means the vertex index is not in use yet
   memset(meshlet_vertices, 0xff, vertex_count);

   usize max_index_count = array_count(ml.primitive_indices);
   usize max_vertex_count = array_count(ml.vertex_index_buffer);
   usize max_triangle_count = max_index_count/3;

   size index_count = m->index_count;

   for(size i = 0; i < index_count; i += 3)
   {
      // original per primitive (triangle indices)
      u32 i0 = m->index_buffer[i + 0];
      u32 i1 = m->index_buffer[i + 1];
      u32 i2 = m->index_buffer[i + 2];

      // are the mesh vertex indices not used yet
      bool mi0 = meshlet_vertices[i0] == 0xff;
      bool mi1 = meshlet_vertices[i1] == 0xff;
      bool mi2 = meshlet_vertices[i2] == 0xff;

      // flush meshlet if vertexes or primitives overflow
      if((ml.vertex_count + (mi0 + mi1 + mi2) > max_vertex_count) || 
         (ml.triangle_count + 1 > max_triangle_count))
      {
         meshlet* pml = new(meshlet_storage, meshlet, 1).beg;
         if(!pml) 
            return false;
         *pml = ml;

         // clear the vertex indices used for this meshlet so that they can be used for the next one
         for(u32 j = 0; j < ml.vertex_count; ++j)
         {
            assert(ml.vertex_index_buffer[j] < vertex_count);
            meshlet_vertices[ml.vertex_index_buffer[j]] = 0xff;
         }

         m->meshlet_count++;

         // begin another meshlet
         struct_clear(ml);
      }

      meshlet_add_new_vertex_index(i0, meshlet_vertices, &ml);
      meshlet_add_new_vertex_index(i1, meshlet_vertices, &ml);
      meshlet_add_new_vertex_index(i2, meshlet_vertices, &ml);

      assert(ml.triangle_count*3 + 2 < array_count(ml.primitive_indices));

      assert(meshlet_vertices[i0] >= 0);
      assert(meshlet_vertices[i1] >= 0);
      assert(meshlet_vertices[i2] >= 0);

      assert(meshlet_vertices[i0] <= ml.vertex_count);
      assert(meshlet_vertices[i1] <= ml.vertex_count);
      assert(meshlet_vertices[i2] <= ml.vertex_count);

      ml.primitive_indices[ml.triangle_count * 3 + 0] = meshlet_vertices[i0];
      ml.primitive_indices[ml.triangle_count * 3 + 1] = meshlet_vertices[i1];
      ml.primitive_indices[ml.triangle_count * 3 + 2] = meshlet_vertices[i2];

      ml.triangle_count++;   // primitive index triplet done

      // within max bounds
      assert(ml.vertex_count <= max_vertex_count);
      assert(ml.triangle_count <= max_triangle_count);
   }

   // add any left over meshlets
   if(ml.vertex_count > 0)
   {
      meshlet* pml = new(meshlet_storage, meshlet, 1).beg;
      if(!pml)
         return false;

      *pml = ml;
      m->meshlet_count++;
   }

   return true;
}

enum { MAX_VULKAN_OBJECT_COUNT = 16, OBJECT_SHADER_COUNT = 2 };   // For mesh shading - ms and fs, for regular pipeline - vs and fs

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
   VkBuffer handle;
   VkDeviceMemory memory;
   void* data;
   size size;
} vk_buffer;

align_struct
{
   vk_buffer buffer;
   u32 count;
} vk_meshlet;

align_struct
{
   VkFramebuffer framebuffers[MAX_VULKAN_OBJECT_COUNT];

   VkPhysicalDevice physical_device;
   VkDevice logical_device;
   VkSurfaceKHR surface;
   VkAllocationCallbacks* allocator;
   VkSemaphore image_ready_semaphore;
   VkSemaphore image_done_semaphore;
   VkQueue graphics_queue;
   VkCommandPool command_pool;
   VkCommandBuffer command_buffer;
   VkRenderPass renderpass;

   // TODO: Pipelines into an array
   VkPipeline graphics_pipeline;
   VkPipeline axis_pipeline;
   VkPipeline frustum_pipeline;
   VkPipelineLayout pipeline_layout;

   vk_buffer vb;        // vertex buffer
   vk_buffer ib;        // index buffer
   vk_buffer mb;        // mesh buffer - todo wrap this in the meshlet structure

   u32 max_meshlet_count;
   u32 meshlet_count;
   u32 index_count;

   swapchain_surface_info swapchain_info;

   arena* storage;
   u32 queue_family_index;
} vk_context;

static void obj_file_read(void *ctx, const char *filename, int is_mtl, const char *obj_filename, char **buf, size_t *len)
{
   char shader_path[MAX_PATH];

   obj_user_ctx* user_data = (obj_user_ctx*)ctx;

   arena project_dir = vk_project_directory(&user_data->scratch);

   if(is_stub(project_dir))
   {
      *len = 0; *buf = 0;
      return;
   }

   wsprintf(shader_path, project_dir.beg, array_count(shader_path));
   wsprintf(shader_path, "%s\\assets\\objs\\%s", project_dir.beg, filename);

   arena file_read = win32_file_read(&user_data->scratch, shader_path);

   if(is_stub(file_read))
   {
      *len = 0; *buf = 0;
      return;
   }

   *len = scratch_size(file_read);
   *buf = file_read.beg;
}

// TOOD: move into own file
static bool obj_load(vk_context* context, arena scratch)
{
      index_hash_table obj_table = {};

      mesh obj_mesh = {};
      const char* filename = "buddha.obj";
      //const char* filename = "hairball.obj";
      //const char* filename = "dragon.obj";
      //const char* filename = "exterior.obj";
      //const char* filename = "erato.obj";
      //const char* filename = "sponza.obj";
      //const char* filename = "san-miguel.obj";

      tinyobj_shape_t* shapes = 0;
      tinyobj_material_t* materials = 0;
      tinyobj_attrib_t attrib = {};

      size_t shape_count = 0;
      size_t material_count = 0;

      tinyobj_attrib_init(&attrib);

      obj_user_ctx user_data = {};
      user_data.scratch = scratch;

      scratch_clear(user_data.scratch);
#if 0
      if(tinyobj_parse_mtl_file(&materials, &material_count, mtl_filename, obj_filename, obj_file_read, &user_data) != TINYOBJ_SUCCESS)
      {
         hw_message("Could not load .mtl file");
         return false;
      }
#endif

      scratch_clear(user_data.scratch);
      if(tinyobj_parse_obj(&attrib, &shapes, &shape_count, &materials, &material_count, filename, obj_file_read, &user_data, TINYOBJ_FLAG_TRIANGULATE) != TINYOBJ_SUCCESS)
      {
         hw_message("Could not load .obj file");
         return false;
      }

      // only triangles allowed
      assert(attrib.num_face_num_verts * 3 == attrib.num_faces);

      const usize index_count = attrib.num_faces;

      obj_table.max_count = index_count;

      scratch_clear(scratch);

      arena keys = new(&scratch, hash_key, obj_table.max_count);
      arena values = new(&scratch, hash_value, obj_table.max_count);

      if(is_stub(keys) || is_stub(values))
         return 0;

      obj_table.keys = keys.beg;
      obj_table.values = values.beg;

      memset(obj_table.keys, -1, sizeof(hash_key)*obj_table.max_count);

      u32 vertex_index = 0;

      for(usize f = 0; f < index_count; f += 3)
      {
         const tinyobj_vertex_index_t* vidx = attrib.faces + f;

         for(usize i = 0; i < 3; ++i)
         {
            i32 vi = vidx[i].v_idx;
            i32 vti = vidx[i].vt_idx;
            i32 vni = vidx[i].vn_idx;

            hash_key index = (hash_key){.vi = vi, .vni = vni, .vti = vti};
            hash_value lookup = hash_lookup(&obj_table, index);

            if(lookup == ~0u)
            {
               obj_vertex v = {};
               if(vi >= 0)
               {
                  v.vx = attrib.vertices[vi * 3 + 0];
                  v.vy = attrib.vertices[vi * 3 + 1];
                  v.vz = attrib.vertices[vi * 3 + 2];
               }

               if(vni >= 0)
               {
                  f32 nx = attrib.normals[vni * 3 + 0];
                  f32 ny = attrib.normals[vni * 3 + 1];
                  f32 nz = attrib.normals[vni * 3 + 2];
                  v.nx = (u8)(nx * 127.f + 127.f);
                  v.ny = (u8)(ny * 127.f + 127.f);
                  v.nz = (u8)(nz * 127.f + 127.f);
               }

               if(vti >= 0)
               {
                  v.tu = attrib.texcoords[vti * 2 + 0];
                  v.tv = attrib.texcoords[vti * 2 + 1];
               }

               hash_insert(&obj_table, index, vertex_index);
               ((u32*)context->ib.data)[f + i] = vertex_index;
               ((obj_vertex*)context->vb.data)[vertex_index++] = v;
            }
            else
               ((u32*)context->ib.data)[f + i] = lookup;
         }
      }

      context->index_count = (u32)index_count;

#if RTX 
      obj_mesh.index_buffer = context->ib.data;
      obj_mesh.index_count = context->index_count;
      obj_mesh.vertex_count = obj_table.count;  // unique vertex count
      obj_mesh.meshlet_buffer = context->storage->beg;

      scratch_clear(scratch);

      if(!meshlet_build(context->storage, scratch, &obj_mesh))
         return false;

      context->meshlet_count = obj_mesh.meshlet_count;
      memcpy(context->mb.data, obj_mesh.meshlet_buffer, context->meshlet_count * sizeof(meshlet));
#endif

      tinyobj_materials_free(materials, material_count);
      tinyobj_shapes_free(shapes, shape_count);
      tinyobj_attrib_free(&attrib);

      return true;
}

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

static VkPhysicalDevice vk_pdevice_select(VkInstance instance)
{
   assert(vk_valid_handle(instance));

   VkPhysicalDevice devs[MAX_VULKAN_OBJECT_COUNT] = {};
   u32 dev_count = array_count(devs);
   vk_assert(vkEnumeratePhysicalDevices(instance, &dev_count, devs));

   for(u32 i = 0; i < dev_count; ++i)
   {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devs[i], &props);

      if(props.apiVersion < VK_VERSION_1_1)
         continue;

      if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
         return devs[i];
   }

   if(dev_count > 0)
      return devs[0];
   return 0;
}

static u32 vk_ldevice_select_family_index()
{
   // TODO: placeholder
   return 0;
}

static u32 vk_mesh_shader_max_tasks(VkPhysicalDevice physical_device)
{
   VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_props = {};
   mesh_shader_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;

   VkPhysicalDeviceProperties2 device_props2 = {};
   device_props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
   device_props2.pNext = &mesh_shader_props;

   vkGetPhysicalDeviceProperties2(physical_device, &device_props2);

   u32 max_mesh_tasks = mesh_shader_props.maxTaskWorkGroupCount[0];

   return max_mesh_tasks;
}

static VkDevice vk_ldevice_create(VkPhysicalDevice physical_device, u32 queue_family_index)
{
   assert(vk_valid_handle(physical_device));
   f32 queue_prio = 1.0f;

   VkDeviceQueueCreateInfo queue_info = {vk_info(DEVICE_QUEUE)};
   queue_info.queueFamilyIndex = queue_family_index; // TODO: query the right queue family
   queue_info.queueCount = 1;
   queue_info.pQueuePriorities = &queue_prio;

   VkDeviceCreateInfo ldev_info = {vk_info(DEVICE)};
   const char* dev_ext_names[] = 
   {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
      VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
      VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
#if RTX
      VK_EXT_MESH_SHADER_EXTENSION_NAME,
      VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
#endif
   };

   VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

#if RTX
   VkPhysicalDeviceVulkan12Features vk12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
   vk12.storageBuffer8BitAccess = true;
   vk12.uniformAndStorageBuffer8BitAccess = true;
   vk12.storagePushConstant8 = true;

   VkPhysicalDeviceFragmentShadingRateFeaturesKHR frag_shading_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
   frag_shading_features.primitiveFragmentShadingRate = VK_TRUE;

   VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
   mesh_shader_features.meshShader = true;
   mesh_shader_features.taskShader = true;
   mesh_shader_features.multiviewMeshShader = true;
   mesh_shader_features.pNext = &frag_shading_features;

   VkPhysicalDeviceMultiviewFeatures multiview = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
   multiview.multiview = true;
   multiview.pNext = &mesh_shader_features;

   vk12.pNext = &multiview;
   features2.pNext = &vk12;
#endif

   vkGetPhysicalDeviceFeatures2(physical_device, &features2);

   features2.features.depthBounds = true;
   features2.features.wideLines = true;
   features2.features.fillModeNonSolid = true;
   features2.features.sampleRateShading = true;

   ldev_info.queueCreateInfoCount = 1;
   ldev_info.pQueueCreateInfos = &queue_info;
   ldev_info.enabledExtensionCount = array_count(dev_ext_names);
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

static void vk_resize(void* renderer, u32 width, u32 height)
{
   if(width == 0 || height == 0)
      return;

   vk_context* context = (vk_context*)renderer;

   vkDeviceWaitIdle(context->logical_device);
   vk_swapchain_destroy(context);
   context->swapchain_info = vk_swapchain_info_create(context, width, height, context->queue_family_index);
   vk_swapchain_update(context);
}

static VkQueryPool vk_query_pool(VkDevice device, size pool_size)
{
   VkQueryPool result = 0;

   VkQueryPoolCreateInfo info = {vk_info(QUERY_POOL)};

   vk_assert(vkCreateQueryPool(device, &info, 0, &result));

   return result;
}

static void vk_present(hw* hw, vk_context* context)
{
   u32 image_index = 0;
   VkResult next_image_result = vkAcquireNextImageKHR(context->logical_device, context->swapchain_info.swapchain, UINT64_MAX, context->image_ready_semaphore, VK_NULL_HANDLE, &image_index);

   if(next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
      vk_resize(context, context->swapchain_info.image_width, context->swapchain_info.image_height);

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
   {
      vk_assert(vkBeginCommandBuffer(command_buffer, &buffer_begin_info));

      const f32 ar = (f32)context->swapchain_info.image_width / context->swapchain_info.image_height;

      f32 delta = 0.75f;
      static f32 rot = 0.0f;
      static f32 originz = -10.0f;
      static f32 cameraz = 0.0f;
      static f32 t = 0.0f;                 // current time

      if(originz > 1.0f)
         originz = -10.0f;

      cameraz = sinf(rot/30)*4;

      rot += delta;
      originz += delta/4;

      mvp_transform mvp = {};

      mvp.n = 0.01f;
      mvp.f = 1000.0f;
      mvp.ar = ar;

      f32 radius = 1.5f;
      f32 theta = DEG2RAD(rot);
      f32 height = 0.0f;

#if 0
      f32 A = PI / 2.0f;            // amplitude: half of pi (90 degrees swing)
      f32 omega = 3.0f;           // angular speed (radians per second)
      f32 theta = A * sinf(omega * t) + A;  // oscillating between 0 and PI
      f32 cos_theta = cosf(theta);
      f32 sin_theta = sinf(theta);
      t += 0.001f;
#endif

      vec3 eye = 
      {
          radius * cosf(theta),
          height,
          radius * sinf(theta)
      };

      vec3 origin = {0.0f, 0.0f, 0.0f};
      vec3 dir = vec3_sub(&eye, &origin);

      mvp.projection = mat4_perspective(ar, 65.0f, mvp.n, mvp.f);
      //mvp.view = mat4_view((vec3){0.0f, 2.0f, 4.0f}, (vec3){0.0f, 0.0f, -1.0f});
      mvp.view = mat4_view(eye, dir);
      //mat4 translate = mat4_translate((vec3){-50.0f, 0.0f, -20.0f});
      mat4 translate = mat4_translate((vec3){0.0f, 0.0f, 0.0f});

      mvp.model = mat4_identity();
      //mvp.model = mat4_scale(mvp.model, 0.15f);
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

      // TODO: Enable depth image barriers
      //VkImage depth_image = context->swapchain_info.depths[image_index];
      //VkImageMemoryBarrier depth_image_begin_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      //vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                           //VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_begin_barrier);


      vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

#if RTX
      vkCmdPushConstants(command_buffer, context->pipeline_layout,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, 0,
                   sizeof(mvp), &mvp);
#else

      vkCmdPushConstants(command_buffer, context->pipeline_layout,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                   sizeof(mvp), &mvp);
#endif

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

#if RTX
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

      vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout, 0, array_count(descriptors), descriptors);
      // TODO: testing 
      static u32 meshlet_count = 0;
      meshlet_count += 50;
      // max meshlet count
      meshlet_count %= 0xffff;
      vkCmdDrawMeshTasksEXT(command_buffer, 0xffff, 1, 1);
#else

      VkWriteDescriptorSet descriptors[1] = {};
      descriptors[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptors[0].dstBinding = 0;
      descriptors[0].descriptorCount = 1;
      descriptors[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptors[0].pBufferInfo = &vb_info;

      vkCmdPushDescriptorSetKHR(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout, 0, array_count(descriptors), descriptors);

      vkCmdBindIndexBuffer(command_buffer, context->ib.handle, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(command_buffer, context->index_count, 1, 0, 0, 0);
#endif

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

      //VkImageMemoryBarrier depth_image_end_barrier = vk_pipeline_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
      //vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           //VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depth_image_end_barrier);

      vk_assert(vkEndCommandBuffer(command_buffer));
   }

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
      vk_resize(context, context->swapchain_info.image_width, context->swapchain_info.image_height);

   if(present_result != VK_SUCCESS)
      return;

   static u32 begin = 0;
   static u32 timer = 0;
   u32 time = hw->timer.time();
   if(begin == 0)
      begin = time;
   u32 end = hw->timer.time();

   if(hw->timer.time() - timer > 1000)
   {
#if RTX
      hw->log(hw, s8("cpu: %u ms; #Meshlets: %d"), end - begin, context->meshlet_count > 0xffff ? 0xffff : context->meshlet_count);
#else
      hw->log(hw, s8("cpu: %u ms"), end - begin);
#endif
      timer = hw->timer.time();
   }
   begin = end;

   // wait until all queue ops are done
   // TODO: This is bad way to do sync but who cares for now
   vk_assert(vkDeviceWaitIdle(context->logical_device));
}

static bool vk_shader_load(VkDevice logical_device, arena scratch, const char* shader_name, vk_shader_modules* shader_modules)
{
   assert(vk_valid_handle(logical_device));

   arena project_dir = vk_project_directory(&scratch);

   if(is_stub(project_dir))
      return false;

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

   return true;
}

static VkDescriptorSetLayout vk_pipeline_set_layout_create(VkDevice logical_device)
{
   assert(vk_valid_handle(logical_device));
   VkDescriptorSetLayout set_layout = 0;

#if RTX
   VkDescriptorSetLayoutBinding bindings[2] = {};
   bindings[0].binding = 0;
   bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   bindings[0].descriptorCount = 1;
   bindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

   bindings[1].binding = 1;
   bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   bindings[1].descriptorCount = 1;
   bindings[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
#else
   VkDescriptorSetLayoutBinding bindings[1] = {};
   bindings[0].binding = 0;
   bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   bindings[0].descriptorCount = 1;
   bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
#endif

   VkDescriptorSetLayoutCreateInfo info = {vk_info(DESCRIPTOR_SET_LAYOUT)};

   info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
   info.bindingCount = array_count(bindings);
   info.pBindings = bindings;

   vkCreateDescriptorSetLayout(logical_device, &info, 0, &set_layout);

   return set_layout;
}

static VkPipelineLayout vk_pipeline_layout_create(VkDevice logical_device)
{
   assert(vk_valid_handle(logical_device));
   VkPipelineLayout layout = 0;

   VkPipelineLayoutCreateInfo info = {vk_info(PIPELINE_LAYOUT)};

   VkDescriptorSetLayout set_layout = vk_pipeline_set_layout_create(logical_device);
   if(!vk_valid_handle(set_layout))
      return VK_NULL_HANDLE;

   VkPushConstantRange push_constants = {};
#if RTX
   push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;
#else
   push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
#endif
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

   if(scratch_left(scratch, VkExtensionProperties) < ext_count)
      return 0;

   VkExtensionProperties* extensions = new(&scratch, VkExtensionProperties, ext_count).beg;
   if(!vk_valid(vkEnumerateInstanceExtensionProperties(0, &ext_count, extensions)))
      return 0;

   if(scratch_left(scratch, const char*) < ext_count)
      return 0;

   const char** ext_names = new(&scratch, const char*, ext_count).beg;

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

   vk_context* context = new(&hw->vk_storage, vk_context).beg;
   if(!context)
      return false;

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

   context->queue_family_index = vk_ldevice_select_family_index();
   context->physical_device = vk_pdevice_select(instance);
   context->logical_device = vk_ldevice_create(context->physical_device, context->queue_family_index);
   context->surface = hw->renderer.window_surface_create(instance, hw->renderer.window.handle);
   context->image_ready_semaphore = vk_semaphore_create(context->logical_device);
   context->image_done_semaphore = vk_semaphore_create(context->logical_device);
   context->graphics_queue = vk_graphics_queue_create(context->logical_device, context->queue_family_index);
   context->command_pool = vk_command_pool_create(context->logical_device, context->queue_family_index);
   context->command_buffer = vk_command_buffer_create(context->logical_device, context->command_pool);
   context->swapchain_info = vk_swapchain_info_create(context, hw->renderer.window.width, hw->renderer.window.height, context->queue_family_index);
   context->renderpass = vk_renderpass_create(context->logical_device, context->swapchain_info.format, VK_FORMAT_D32_SFLOAT);

#if RTX
   context->max_meshlet_count = vk_mesh_shader_max_tasks(context->physical_device);
#endif

   VkExtent3D depth_extent = {context->swapchain_info.image_width, context->swapchain_info.image_height, 1};
   for(u32 i = 0; i < context->swapchain_info.image_count; ++i)
      context->swapchain_info.depths[i] = vk_depth_image_create(context->logical_device, context->physical_device, VK_FORMAT_D32_SFLOAT, depth_extent);

   if(!vk_swapchain_update(context))
      return false;

   u32 shader_count = 0;
   const char** shader_names = vk_shader_folder_read(&scratch, "bin\\assets\\shaders");
   for(const char** p = shader_names; p && *p; ++p)
      shader_count++;

   // TODO: store this inside the context
   spv_hash_table shader_hash_table = {};
   shader_hash_table.max_count = shader_count;

   // TODO: make function for hash tables
   arena keys = new(&scratch, const char*, shader_hash_table.max_count);
   arena values = new(&scratch, vk_shader_modules, shader_hash_table.max_count);

   if(is_stub(keys) || is_stub(values))
      return 0;

   shader_hash_table.keys = keys.beg;
   shader_hash_table.values = values.beg;

   memset(shader_hash_table.values, 0, shader_hash_table.max_count * sizeof(vk_shader_modules));

   for(usize i = 0; i < shader_hash_table.max_count; ++i)
      shader_hash_table.keys[i] = 0;

   // TODO: routine to iterate over hash values

   // Compile all the shaders
   for(const char** p = shader_names; p && *p; ++p)
   {
      usize shader_len = strlen(*p);
      const char* shader_name = *p;

      for(usize i = 0; i < shader_len; ++i)
      {
         // TODO: cleanup this mess
#if RTX
         if(strncmp(shader_name + i, "meshlet", strlen("meshlet")) == 0)
         {
            vk_shader_modules ms = spv_hash_lookup(&shader_hash_table, "meshlet");
            if(!vk_shader_load(context->logical_device, scratch, *p, &ms))
               return false;
            spv_hash_insert(&shader_hash_table, "meshlet", ms);
            break;
         }
#else
         if(strncmp(shader_name + i, "graphics", strlen("graphics")) == 0)
         {
            vk_shader_modules gm = spv_hash_lookup(&shader_hash_table, "graphics");
            if(!vk_shader_load(context->logical_device, scratch, *p, &gm))
               return false;
            spv_hash_insert(&shader_hash_table, "graphics", gm);
            break;
         }
#endif
         if(strncmp(shader_name + i, "axis", strlen("axis")) == 0)
         {
            vk_shader_modules am = spv_hash_lookup(&shader_hash_table, "axis");
            if(!vk_shader_load(context->logical_device, scratch, *p, &am))
               return false;
            spv_hash_insert(&shader_hash_table, "axis", am);
            break;
         }
      }
   }

   VkPipelineCache cache = 0; // TODO: enable
   VkPipelineLayout layout = vk_pipeline_layout_create(context->logical_device);

#if RTX
   vk_shader_modules mm = spv_hash_lookup(&shader_hash_table, "meshlet");
   context->graphics_pipeline = vk_mesh_pipeline_create(context->logical_device, context->renderpass, cache, layout, &mm);
#else
   vk_shader_modules gm = spv_hash_lookup(&shader_hash_table, "graphics");
   context->graphics_pipeline = vk_graphics_pipeline_create(context->logical_device, context->renderpass, cache, layout, &gm);
#endif

   vk_shader_modules am = spv_hash_lookup(&shader_hash_table, "axis");
   context->axis_pipeline = vk_axis_pipeline_create(context->logical_device, context->renderpass, cache, layout, &am);

   context->pipeline_layout = layout;

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_props);

   // TODO: fine tune these and get device memory limits
   size buffer_size = MB(1280);
   vk_buffer index_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
   vk_buffer vertex_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
#if RTX 
   vk_buffer meshlet_buffer = vk_buffer_create(context->logical_device, buffer_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
#endif

   // valid gpu buffers
   if(index_buffer.handle == VK_NULL_HANDLE || vertex_buffer.handle == VK_NULL_HANDLE)
      return false;

   context->vb = vertex_buffer;
   context->ib = index_buffer;
#if RTX
   context->mb = meshlet_buffer;
#endif

   if(!obj_load(context, scratch))
      return false;

   // app callbacks
   hw->renderer.backends[vk_renderer_index] = context;
   hw->renderer.frame_present = vk_present;
   hw->renderer.frame_resize = vk_resize;
   hw->renderer.renderer_index = vk_renderer_index;

   return true;
}

bool vk_uninitialize(hw* hw)
{
   // TODO: uninitialize
   vk_context* context = hw->renderer.backends[vk_renderer_index];

#if 0
   vkDestroySwapchainKHR(context->logical_device, context->swapchain, 0);
   vkDestroySurfaceKHR(context->instance, context->surface, 0);
   vkDestroyDevice(context->logical_device, 0);
   vkDestroyInstance(context->instance, 0);
#endif

   return true;
}
