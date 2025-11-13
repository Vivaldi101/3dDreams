#include "vulkan_ng.h"

#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION

#include "../extern/stb_image.h"

// TODO: wide contract
static VkImageView vk_image_view_create(vk_context* context, VkFormat format, VkImage image, VkImageAspectFlags aspect_mask)
{
   VkImageView image_view = 0;

   VkImageViewCreateInfo view_info = {vk_info(IMAGE_VIEW)};
   view_info.image = image;
   view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view_info.format = format;
   view_info.subresourceRange.aspectMask = aspect_mask;
   view_info.subresourceRange.layerCount = 1;
   view_info.subresourceRange.levelCount = 1;

   if(!vk_valid(vkCreateImageView(context->devices.logical, &view_info, 0, &image_view)))
      return VK_NULL_HANDLE;

   return image_view;
}

static bool vk_image_create(vk_image* image, vk_context* context, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage)
{
   VkImageCreateInfo image_info = {0};
   image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.imageType = VK_IMAGE_TYPE_2D; 
   image_info.extent = extent;
   image_info.mipLevels = 1;
   image_info.arrayLayers = 1;
   image_info.format = format;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   image_info.usage = usage;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;  // TODO: pass
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.queueFamilyIndexCount = 0;
   image_info.pQueueFamilyIndices = 0;

   if(vkCreateImage(context->devices.logical, &image_info, 0, &image->handle) != VK_SUCCESS)
      return false;

   VkMemoryRequirements memory_requirements;
   vkGetImageMemoryRequirements(context->devices.logical, image->handle, &memory_requirements);

   VkMemoryAllocateInfo alloc_info = {0};
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.allocationSize = memory_requirements.size;

   VkPhysicalDeviceMemoryProperties memory_properties;
   vkGetPhysicalDeviceMemoryProperties(context->devices.physical, &memory_properties);

   uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
   for(uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
      if((memory_requirements.memoryTypeBits & (1 << i)) &&
          (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      {
         memory_type_index = i;
         break;
      }

   if(memory_type_index == VK_MAX_MEMORY_TYPES)
      return false;

   alloc_info.memoryTypeIndex = memory_type_index;

   VkDeviceMemory memory;
   if(vkAllocateMemory(context->devices.logical, &alloc_info, 0, &memory) != VK_SUCCESS)
      return false;

   image->memory = memory;

   if(vkBindImageMemory(context->devices.logical, image->handle, memory, 0) != VK_SUCCESS)
      return false;

   return true;
}

static bool vk_depth_image_create(vk_image* image, vk_context* context, VkFormat format, VkExtent3D extent)
{
   return vk_image_create(image, context, format, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static size vk_texture_size_blocked(u32 w, u32 h, u32 levels, u32 block_size)
{
   (void)w;
   (void)h;
   (void)levels;
   (void)block_size;
   size result = 0;

   return result;
}

static size vk_texture_size(u32 w, u32 h, u32 levels)
{
   return vk_texture_size_blocked(w, h, levels, 0);
}

// TODO: wide contract
static void vk_texture_load(vk_context* context, s8 img_uri, s8 gltf_path)
{
   u8* gltf_end = gltf_path.data + gltf_path.len;
   size tex_path_start = gltf_path.len;

   while(*gltf_end != '/' && tex_path_start != 0)
   {
      tex_path_start--;
      gltf_end--;
   }

   assert(*gltf_end == '/' && tex_path_start != 0);

   s8 tex_dir = s8_slice(gltf_path, 0, tex_path_start + 1);

   size tex_len = img_uri.len + tex_dir.len;

   vk_texture tex = {0};
   tex.path.arena = context->storage;
   array_resize(tex.path, tex_len + 1); // for null terminate

   memcpy(tex.path.data, tex_dir.data, tex_dir.len);
   memcpy(tex.path.data + tex_dir.len, img_uri.data, img_uri.len);

   tex.path.data[tex_len] = 0;        // null terminate

   i32 tex_width = 0;
   i32 tex_height = 0;
   i32 tex_channels = 0;
   stbi_uc* tex_pixels = stbi_load(s8_data(tex.path), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);

   VkExtent3D extents = {.width = tex_width, .height = tex_height, .depth = 1};
   VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

   vk_image image = {0};
   if(!vk_image_create(&image, context, VK_FORMAT_R8G8B8A8_UNORM, extents, usage))
      return;  // false

   VkImageView image_view = vk_image_view_create(context, format, image.handle, VK_IMAGE_ASPECT_COLOR_BIT);

   // TODO: enable for mip textures
   size tex_size = tex_width * tex_height * STBI_rgb_alpha;

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->devices.physical, &memory_props);
   vk_buffer scratch_buffer = {.size = tex_size};
   vk_buffer_create_and_bind(&scratch_buffer, &context->devices, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

   vk_buffer_to_image_upload(context, scratch_buffer, image.handle, extents, tex_pixels, scratch_buffer.size);

   tex.image.handle = image.handle;
   tex.image.memory = image.memory;
   tex.image.view = image_view;

   array_add(context->textures, tex);

   vk_buffer_destroy(&context->devices, &scratch_buffer);

   stbi_image_free(tex_pixels);
}


static void vk_textures_log(vk_context* context)
{
   for(size i = 0; i < context->textures.count; i++)
      printf("Texture loaded: %s\n", context->textures.data[i].path.data);
}
