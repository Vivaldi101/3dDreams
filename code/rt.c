#include "vulkan_ng.h"

static void rt_blas_build(arena scratch, vk_buffer_hash_table* buffer_hash, vk_geometry* geometry, vk_device* devices)
{
   const size geometry_count = geometry->mesh_draws.count;

   array_fixed(primitive_count, u32, geometry_count, scratch);
   array_fixed(acceleration_geometries, VkAccelerationStructureGeometryKHR, geometry_count, scratch);
   array_fixed(build_infos, VkAccelerationStructureBuildGeometryInfoKHR, geometry_count, scratch);

   for(size i = 0; i < geometry_count; ++i)
   {
      vk_mesh_draw* draw = geometry->mesh_draws.data + i;

      VkAccelerationStructureGeometryKHR* ags = acceleration_geometries.data + i;

      // static assert the vertex format
      static_assert(offsetof(vertex, vz) == offsetof(vertex, vx) + sizeof(float)*2);

      ags->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
      ags->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
      ags->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

      ags->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
      ags->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
      ags->geometry.triangles.vertexStride = sizeof(vertex);
      ags->geometry.triangles.maxVertex = (u32)draw->vertex_count;
      ags->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;

      VkAccelerationStructureBuildGeometryInfoKHR build_info =
      {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      build_info.geometryCount = 1;
      build_info.pGeometries = ags;

      VkAccelerationStructureBuildSizesInfoKHR size_info =
      {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

      array_add(primitive_count, (u32)draw->index_count / 3);   // triangles

      vkGetAccelerationStructureBuildSizesKHR(devices->logical,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &build_info, primitive_count.data + i, &size_info);
   }
}
