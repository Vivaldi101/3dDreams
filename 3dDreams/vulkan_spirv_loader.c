#include "arena.h"

// This must match what is in the shader_build.bat file
#define BUILTIN_SHADER_NAME "Builtin.ObjectShader"

// TODO: Change name to vk_spv_compile
static VkShaderModule vk_shader_spv_module_load(VkDevice logical_device, arena* storage, const char* shader_dir, const char* shader_name)
{
   VkShaderModule result = 0;

   char shader_path[MAX_PATH];
   wsprintf(shader_path, shader_dir, array_count(shader_path));
   wsprintf(shader_path, "%sbin\\assets\\shaders\\%s", shader_dir, shader_name);

   assert(strlen(shader_path) <= MAX_PATH);

   arena shader_file = win32_file_read(storage, shader_path);

   VkShaderModuleCreateInfo module_info = {};
   module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   module_info.pCode = (u32*)shader_file.beg;
   module_info.codeSize = scratch_left(shader_file);

   if((vkCreateShaderModule(logical_device, &module_info, 0, &result)) != VK_SUCCESS)
      return 0;

   return result;
}

static arena vk_project_directory(arena* storage)
{
   arena result = {};

   char* buffer = push(storage, char, MAX_PATH);

   GetCurrentDirectory(MAX_PATH, buffer);

   usize file_size = strlen(buffer);

   u32 count = 0;

   assert(file_size != 0u);

   for(size i = file_size-1; i-- >= 0;)
   {
      if(buffer[i] == '\\')
         ++count;
      if(count == 2)
      {
         buffer[i+1] = 0;
         break;
      }
   }

   file_size = strlen(buffer);

   result.beg = buffer;
   result.end = buffer + file_size;

   return result;
}

// TODO: path must end with wildcard - test it
static const char** vk_shader_folder_read(arena* files, const char* shader_folder_path)
{
   arena project_dir = vk_project_directory(files);

   WIN32_FIND_DATA file_data;

   char path[MAX_PATH];
   wsprintf(path, "%s\\%s\\*", project_dir.beg, shader_folder_path);
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
