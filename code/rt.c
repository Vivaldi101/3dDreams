#include "vulkan_ng.h"

static void rt_blas_build(arena scratch, vk_geometry* geometry, vk_device* devices)
{
   const size geometry_count = geometry->mesh_draws.count;

   array_fixed(primitive_count, u32, geometry_count, scratch);
   array_fixed(acceleration_geometries, VkAccelerationStructureGeometryKHR, geometry_count, scratch);

   for(size i = 0; i < geometry_count; ++i)
   {
      vk_mesh_draw* draw = geometry->mesh_draws.data + i;
      array_add(primitive_count, (u32)draw->index_count / 3);
   }

   VkAccelerationStructureGeometryDataKHR triangle_geometry =
   {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
   triangle_geometry.triangles.vertexFormat;
   triangle_geometry.triangles.vertexData;
   triangle_geometry.triangles.vertexStride;
   triangle_geometry.triangles.maxVertex;
   triangle_geometry.triangles.indexType;
   triangle_geometry.triangles.indexData;
   triangle_geometry.triangles.transformData;

   VkAccelerationStructureGeometryKHR acceleration_geometry =
   {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
   acceleration_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
   acceleration_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
   acceleration_geometry.geometry = triangle_geometry;

   VkAccelerationStructureBuildGeometryInfoKHR build_info =
   {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
   build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
   build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
   build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
   //build_info.geometryCount = (u32)geometry_count;
   build_info.pGeometries = 0;
   build_info.ppGeometries = 0;

   VkAccelerationStructureBuildSizesInfoKHR size_info =
   {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

   vkGetAccelerationStructureBuildSizesKHR(devices->logical, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, primitive_count.data, &size_info);
}
