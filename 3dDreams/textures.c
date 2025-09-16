#include "vulkan_ng.h"

#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION

#include "../extern/stb_image.h"

static void vk_textures_parse(vk_context* context, cgltf_data* data, s8 gltf_path)
{
   // TODO: semcompress this texture parsing
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
      array_add(context->textures, tex);

      i32 tex_width, tex_height, tex_channels;
      stbi_uc* tex_pixels = stbi_load(s8_data(tex.path), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);

      assert(tex_pixels);
      assert(tex_width > 0 && tex_height > 0);

      // ... create the vulkan tex objects

      stbi_image_free(tex_pixels);
   }
}

static void vk_textures_log(vk_context* context)
{
   for(size i = 0; i < context->textures.count; i++)
      printf("Texture loaded: %s\n", context->textures.data[i].path.data);
}
