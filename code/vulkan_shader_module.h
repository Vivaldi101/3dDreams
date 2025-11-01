#if !defined(_VULKAN_SHADER_MODULE_H)
#define _VULKAN_SHADER_MODULE_H

#include "common.h"

align_struct vk_shader_module
{
   VkShaderModule module;
   VkShaderStageFlagBits stage;
} vk_shader_module;

align_struct spv_hash_table
{
   vk_shader_module* values;
   const char** keys;
   size max_count;
   size count;
} spv_hash_table;

#endif
