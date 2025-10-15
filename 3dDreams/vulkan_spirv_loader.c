#include "arena.h"

// This must match what is in the shader_build.bat file
#define BUILTIN_SHADER_NAME "Builtin.ObjectShader"

// TODO: Change name to vk_spv_compile
static VkShaderModule vk_shader_spv_module_load(VkDevice logical_device, arena* storage, s8 shader_dir, s8 shader_name)
{
   VkShaderModule result = 0;

   array(char) shader_path = {storage};
   s8 prefix = s8("%s\\bin\\assets\\shaders\\%s");

   shader_path.count = shader_dir.len + prefix.len + shader_name.len;  // TODO s8 for shader_name
   array_resize(shader_path, shader_path.count);

   //wsprintf(shader_path.data, shader_dir.data, array_count(shader_path));
   wsprintf(shader_path.data, s8_data(prefix), shader_dir.data, shader_name.data);

   arena shader_file = win32_file_read(storage, shader_path.data);

   VkShaderModuleCreateInfo module_info = {};
   module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   module_info.pCode = (u32*)shader_file.beg;
   module_info.codeSize = arena_left(&shader_file);

   if((vkCreateShaderModule(logical_device, &module_info, 0, &result)) != VK_SUCCESS)
      return 0;

   return result;
}

// TODO: Should be in win32.c
static s8 win32_module_path(arena* a)
{
   size dir_path_len = 0;
   u8* buffer = push(a, u8, MAX_PATH);

   for (;;)
   {
      dir_path_len = GetModuleFileName(NULL, buffer, MAX_PATH);
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
   u32 count = 0;
   s8 buffer = win32_module_path(a);
   if(buffer.len == 0)  // TODO: Handle invalid module paths on the calling side
      return buffer;

   size exe_dir_len = buffer.len;
   size index = s8_is_substr_count(buffer, s8("3dDreams"));

   if(index != -1)
   {
      buffer.len = index + s8("3dDreams").len;
      buffer.data[buffer.len] = 0;
   }

   return buffer;
}

static const char** vk_shader_folder_read(arena* files, s8 shader_folder_path)
{
   array(char) shader_path = {files};

   s8 prefix = s8("%sbin\\assets\\shaders\\%s");
   s8 exe_dir = vk_exe_directory(files);

   shader_path.count = prefix.len + exe_dir.len + shader_folder_path.len;
   array_resize(shader_path, shader_path.count);

   wsprintf(shader_path.data, "%s\\%s\\*", (const char*)exe_dir.data, shader_folder_path.data);

   WIN32_FIND_DATA file_data;
   HANDLE first_file = FindFirstFile(shader_path.data, &file_data);

   if(first_file == INVALID_HANDLE_VALUE)
      (arena){0};

   u32 shader_count = 0;

   do {
      if(!(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
         shader_count++;
   } while(FindNextFile(first_file, &file_data) != 0);

   first_file = FindFirstFile(shader_path.data, &file_data);

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
