#if !defined(_VULKAN_NG_H)
#define _VULKAN_NG_H

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif
// other plats for vulkan
#endif

#include "common.h"
#include <vulkan/vulkan.h>

bool vk_uninitialize(struct hw* hw);
bool vk_initialize(struct hw* hw);

#define vk_valid_handle(v) ((v) != VK_NULL_HANDLE)
#define vk_valid_format(v) ((v) != VK_FORMAT_UNDEFINED)

#define vk_valid(v) ((v) == VK_SUCCESS)

#define vk_info(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO
#define vk_info_allocate(i) VK_STRUCTURE_TYPE_##i##_ALLOCATE_INFO
#define vk_info_khr(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO_KHR
#define vk_info_ext(i) VK_STRUCTURE_TYPE_##i##_CREATE_INFO_EXT

#define vk_info_begin(i) VK_STRUCTURE_TYPE_##i##_BEGIN_INFO
#define vk_info_end(i) VK_STRUCTURE_TYPE_##i##_END_INFO

#ifdef _DEBUG
#define vk_assert(v) \
        do { \
          VkResult _r = (v); \
          assert(vk_valid(_r)); \
        } while(0)
#else
#define vk_assert(v) (v)
#endif

#endif
