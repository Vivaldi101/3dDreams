#include "arena.h"
#include "vulkan_ng.h"

// This must match what is in the shader_build.bat file
#define BUILTIN_SHADER_NAME "Builtin.ObjectShader"

// TODO: Should be in win32.c
static s8 win32_module_path(arena* a)
{
   size dir_path_len = 0;
   u8* buffer = push(a, u8, MAX_PATH);

   for (;;)
   {
      dir_path_len = GetModuleFileName(NULL, (char*)buffer, MAX_PATH);
      if(dir_path_len == 0)
         return s8("");

      if(dir_path_len == MAX_PATH)
      {
         buffer = push(a, u8, MAX_PATH*2);
         continue;
      }

      return (s8){buffer, dir_path_len};
   }
}

static s8 vk_exe_directory(arena* a)
{
   s8 module_path = win32_module_path(a);

   if(module_path.len == 0)
      return (s8){0};

   s8 project_name = s8("3dDreams");
   size index = s8_is_substr_count(module_path, project_name);

   if(index == -1)
      return (s8){0};

   module_path.len = index + project_name.len;
   module_path.data[module_path.len] = 0;

   return module_path;
}

// TODO: Change name to vk_spv_compile
static VkShaderModule vk_shader_spv_module_load(VkDevice logical_device, arena scratch, s8 shader_name)
{
   VkShaderModule result = 0;

   s8 shader_dir = vk_exe_directory(&scratch);

   array(char) shader_path = {&scratch};
   s8 prefix = s8("%s\\bin\\assets\\shaders\\%s");

   shader_path.count = shader_dir.len + prefix.len + shader_name.len;  // TODO s8 for shader_name
   array_resize(shader_path, shader_path.count);

   wsprintf(shader_path.data, s8_data(prefix), shader_dir.data, shader_name.data);

   arena shader_file = win32_file_read(&scratch, shader_path.data);

   VkShaderModuleCreateInfo module_info = {0};
   module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   module_info.pCode = (u32*)shader_file.beg;
   module_info.codeSize = arena_left(&shader_file);

   if((vkCreateShaderModule(logical_device, &module_info, 0, &result)) != VK_SUCCESS)
      return 0;

   return result;
}

typedef array(s8) s8_array;
static s8_array vk_shader_names_read(arena* a, s8 shader_folder_path)
{
   array(char) shader_path = {a}; // TODO: this should be scratch

   s8 prefix = s8("%sbin\\assets\\shaders\\%s");
   s8 exe_dir = vk_exe_directory(a);

   if(exe_dir.len == 0)
      return (s8_array) { 0 };

   shader_path.count = prefix.len + exe_dir.len + shader_folder_path.len;
   array_resize(shader_path, shader_path.count);

   wsprintf(shader_path.data, "%s\\%s\\*", (const char*)exe_dir.data, shader_folder_path.data);

   WIN32_FIND_DATA file_data;
   HANDLE first_file = FindFirstFile(shader_path.data, &file_data);

   if(first_file == INVALID_HANDLE_VALUE)
      return (s8_array) { 0 };

   u32 shader_count = 0;

   do
   {
      if(!(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
         shader_count++;
   }
   while(FindNextFile(first_file, &file_data) != 0);

   s8_array shader_names = {a};
   array_resize(shader_names, shader_count);

   first_file = FindFirstFile(shader_path.data, &file_data);

   size i = 0;
   do
   {
      if(!(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      {
         usize file_len = strlen(file_data.cFileName);

         shader_names.count++;
         shader_names.data[i].data = push(a, u8, file_len + 1);
         shader_names.data[i].data[file_len] = 0;
         shader_names.data[i].len = file_len;

         memcpy(shader_names.data[i].data, file_data.cFileName, file_len);

         ++i;
      }
   }
   while(FindNextFile(first_file, &file_data) != 0);

   FindClose(first_file);

   return shader_names;
}
