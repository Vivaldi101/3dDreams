#if !defined(_VULKAN_SHADER_MODULE_H)
#define _VULKAN_SHADER_MODULE_H

#include "common.h"

align_struct vk_shader_modules
{
   VkShaderModule vs;
   VkShaderModule fs;
   VkShaderModule ms;
} vk_shader_modules;

align_struct spv_hash_table
{
   vk_shader_modules* values;
   const char** keys;
   size max_count;
   size count;
} spv_hash_table;

#endif
