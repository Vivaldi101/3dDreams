#include "vulkan_ng.h"

#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION

#include "../extern/stb_image.h"

static VkImageView vk_image_view_create(vk_context* context, VkFormat format, VkImage image, VkImageAspectFlags aspect_mask)
{
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

   if(!vk_valid(vkCreateImageView(context->logical_device, &view_info, 0, &image_view)))
      return VK_NULL_HANDLE;

   return image_view;
}

static VkImage vk_image_create(vk_context* context, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage)
{
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
   image_info.usage = usage;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;  // TODO: pass
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.queueFamilyIndexCount = 0;
   image_info.pQueueFamilyIndices = 0;

   if(vkCreateImage(context->logical_device, &image_info, 0, &result) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   VkMemoryRequirements memory_requirements;
   vkGetImageMemoryRequirements(context->logical_device, result, &memory_requirements);

   VkMemoryAllocateInfo alloc_info = {};
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.allocationSize = memory_requirements.size;

   VkPhysicalDeviceMemoryProperties memory_properties;
   vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_properties);

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
   if(vkAllocateMemory(context->logical_device, &alloc_info, 0, &memory) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   if(vkBindImageMemory(context->logical_device, result, memory, 0) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return result;
}

static VkImage vk_depth_image_create(vk_context* context, VkFormat format, VkExtent3D extent)
{
   return vk_image_create(context, format, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static size vk_texture_size_blocked(u32 w, u32 h, u32 levels, u32 block_size)
{
   size result = 0;

   return result;
}

static size vk_texture_size(u32 w, u32 h, u32 levels)
{
   return vk_texture_size_blocked(w, h, levels, 0);
}

static void vk_textures_parse(vk_context* context, cgltf_data* data, s8 gltf_path)
{
   for(usize i = 0; i < data->textures_count; ++i)
   {
      cgltf_texture* cgltf_tex = data->textures + i;
      assert(cgltf_tex->image);

      cgltf_image* img = cgltf_tex->image;
      assert(img->uri);

      cgltf_decode_uri(img->uri);

      u8* gltf_end = gltf_path.data + gltf_path.len;
      size tex_path_start = gltf_path.len;

      while(*gltf_end != '/' && tex_path_start != 0)
      {
         tex_path_start--;
         gltf_end--;
      }

      assert(*gltf_end == '/' && tex_path_start != 0);

      s8 tex_dir = s8_slice(gltf_path, 0, tex_path_start+1);

      size img_uri_len = strlen(img->uri);
      size tex_len = img_uri_len + tex_dir.len;

      vk_texture tex = {};
      tex.path.arena = context->storage;
      array_resize(tex.path, tex_len+1); // for null terminate

      memcpy(tex.path.data, tex_dir.data, tex_dir.len);
      memcpy(tex.path.data + tex_dir.len, img->uri, img_uri_len);

      tex.path.data[tex_len] = 0;        // null terminate

      i32 tex_width, tex_height, tex_channels;
      stbi_uc* tex_pixels = stbi_load(s8_data(tex.path), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);

      assert(tex_pixels);
      assert(tex_width > 0 && tex_height > 0);

      VkExtent3D extents = { .width = tex_width, .height = tex_height, .depth = 1 };
      VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

      // TODO: think composite contracts for non-narrow functions
      VkImage image = vk_image_create(context, VK_FORMAT_R8G8B8A8_UNORM, extents, usage);
      VkImageView image_view = vk_image_view_create(context, format, image, VK_IMAGE_ASPECT_COLOR_BIT);

      assert(vk_valid_handle(image));
      assert(vk_valid_handle(image_view));

      tex.image.handle = image;
      tex.image.view = image_view;

      // TODO: enable for mip textures
      //size tex_size = vk_texture_size(tex_width, tex_height, 0);
      size tex_size = tex_width * tex_height * STBI_rgb_alpha;

      array_add(context->textures, tex);

      stbi_image_free(tex_pixels);
   }
}

static void vk_textures_log(vk_context* context)
{
   for(size i = 0; i < context->textures.count; i++)
      printf("Texture loaded: %s\n", context->textures.data[i].path.data);
}
