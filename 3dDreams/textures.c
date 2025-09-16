#include "vulkan_ng.h"

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

      while(*gltf_end-- != '/')
         tex_path_start--;

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
   }
}

static void vk_textures_log(vk_context* context)
{
   for(size i = 0; i < context->textures.count; i++)
   {
      vk_image image = {};
      printf("Texture loaded: %s\n", context->textures.data[i].path.data);

      context->textures.data[i].image = image;
   }
}
