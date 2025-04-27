#include "arena.h"

// This must match what is in the shader_build.bat file
#define BUILTIN_SHADER_NAME "Builtin.ObjectShader"

static arena vk_shader_spv_read(arena* storage, const char* shader_dir, const char* shader_name, VkShaderStageFlagBits type)
{
   char* type_name;
   char shader_path[MAX_PATH];

   switch(type)
   {
      default:
         type_name = "invalid";
      case VK_SHADER_STAGE_VERTEX_BIT:
         type_name = "vert";
         break;
      case VK_SHADER_STAGE_FRAGMENT_BIT:
         type_name = "frag";
         break;
   }

   wsprintf(shader_path, shader_dir, array_count(shader_path));
   wsprintf(shader_path, "%sbin\\assets\\shaders\\%s.%s.%s.spv", shader_dir, BUILTIN_SHADER_NAME, shader_name, type_name);

   return win32_file_read(storage, shader_path);
}

static arena vk_project_directory(arena* storage)
{
   arena result = new(storage, char, MAX_PATH);
   if(result.beg == result.end)
      return (arena){0};

   char* fb = result.beg;

   GetCurrentDirectory(MAX_PATH, fb);

   usize file_size = strlen(fb);

   u32 count = 0;

   assert(file_size != 0u);

   for(size i = file_size-1; i-- >= 0;)
   {
      if(fb[i] == '\\')
         ++count;
      if(count == 2)
      {
         fb[i+1] = 0;
         break;
      }
   }

   file_size = strlen(fb);

   scratch_shrink(result, file_size, char);

   return result;
}
