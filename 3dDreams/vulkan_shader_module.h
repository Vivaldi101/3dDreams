#if !defined(_VULKAN_SHADER_MODULE_H)
#define _VULKAN_SHADER_MODULE_H

#include "vulkan_ng.h"

align_struct
{
   VkShaderModule vs;
   VkShaderModule fs;
   VkShaderModule ms;
} vk_shader_modules;

typedef struct 
{
   vk_shader_modules* values;
   const char** keys;
   size max_count;
   size count;
} spv_hash_table;

#endif
