#if !defined(_VULKAN_SHADER_MODULE_H)
#define _VULKAN_SHADER_MODULE_H

#include "vulkan_ng.h"

align_struct
{
   VkShaderModule vs;
   VkShaderModule fs;
   VkShaderModule ms;
} vk_shader_modules;

#endif
