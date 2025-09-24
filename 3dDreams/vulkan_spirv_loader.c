#include "arena.h"

// This must match what is in the shader_build.bat file
#define BUILTIN_SHADER_NAME "Builtin.ObjectShader"

// TODO: Change name to vk_spv_compile
static VkShaderModule vk_shader_spv_module_load(VkDevice logical_device, arena* storage, s8 shader_dir, const char* shader_name)
{
   VkShaderModule result = 0;

   array(char) shader_path = {storage};
   s8 prefix = s8("%sbin\\assets\\shaders\\%s");

   shader_path.count = shader_dir.len + prefix.len + strlen(shader_name);  // TODO s8 for shader_name
   array_resize(shader_path, shader_path.count);

   //wsprintf(shader_path.data, shader_dir.data, array_count(shader_path));
   wsprintf(shader_path.data, s8_data(prefix), shader_dir.data, shader_name);

   arena shader_file = win32_file_read(storage, shader_path.data);

   VkShaderModuleCreateInfo module_info = {};
   module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   module_info.pCode = (u32*)shader_file.beg;
   module_info.codeSize = arena_left(&shader_file);

   if((vkCreateShaderModule(logical_device, &module_info, 0, &result)) != VK_SUCCESS)
      return 0;

   return result;
}

static s8 vk_project_directory(arena* storage)
{
   s8 result = {};

   size dir_path_len = GetCurrentDirectory(0, 0);

   u8* buffer = push(storage, u8, dir_path_len);

   GetCurrentDirectory((u32)dir_path_len, (char*)buffer);

   u32 count = 0;

   assert(dir_path_len != 0u);

   for(size i = dir_path_len-1; i-- >= 0;)
   {
      if(buffer[i] == '\\')
         ++count;
      if(count == 2)
      {
         buffer[i+1] = 0;
         break;
      }
   }

   return (s8){.data = buffer, .len = dir_path_len};
}

static const char** vk_shader_folder_read(arena* files, const char* shader_folder_path)
{
   s8 project_dir = vk_project_directory(files);

   WIN32_FIND_DATA file_data;

   char path[MAX_PATH];
   wsprintf(path, "%s\\%s\\*", (const char*)project_dir.data, shader_folder_path);
   HANDLE first_file = FindFirstFile(path, &file_data);

   if(first_file == INVALID_HANDLE_VALUE)
      (arena){0};

   u32 shader_count = 0;

   do {
      if(!(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
         shader_count++;
   } while(FindNextFile(first_file, &file_data) != 0);

   first_file = FindFirstFile(path, &file_data);

   const char** shader_names = push(files, const char*, shader_count+1);

   u32 i = 0;
   do {
      if(!(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      {
         usize file_len = strlen(file_data.cFileName);
         char* p = push(files, char, file_len+1);

         if(p) 
         {
            memcpy(p, file_data.cFileName, file_len);
            p[file_len] = 0;

            shader_names[i++] = p;
         }
      }
   } while(FindNextFile(first_file, &file_data) != 0);

   shader_names[i] = 0;
   FindClose(first_file);

   return shader_names;
}
