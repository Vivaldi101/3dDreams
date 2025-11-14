#if !defined(_VULKAN_SHADER_MODULE_H)
#define _VULKAN_SHADER_MODULE_H

#include "common.h"

align_struct vk_shader_module
{
   VkShaderModule handle;
   VkShaderStageFlagBits stage;
} vk_shader_module;

align_struct vk_shader_module_name
{
   vk_shader_module module;
   const char* name;
} vk_shader_module_name;

align_struct spv_hash_table
{
   vk_shader_module* values;
   const char** keys;
   size max_count;
   size count;
} spv_hash_table;

#endif
