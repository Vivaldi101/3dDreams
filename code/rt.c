#include "vulkan_ng.h"

static bool rt_blas_buffer_create(vk_context* context)
{
   const size geometry_count = context->geometry.mesh_draws.count;
   const size alignment = 256;

   size total_acceleration_size = 0;
   size total_scratch_size = 0;

   arena scratch = *context->storage;

   array_fixed(primitive_count, u32, geometry_count, scratch);
   array_fixed(acceleration_geometries, VkAccelerationStructureGeometryKHR, geometry_count, scratch);
   array_fixed(build_infos, VkAccelerationStructureBuildGeometryInfoKHR, geometry_count, scratch);

   array_fixed(acceleration_offsets, size, geometry_count, scratch);
   array_fixed(scratch_offsets, size, geometry_count, scratch);

   array_fixed(acceleration_sizes, size, geometry_count, scratch);

   vk_buffer* ib = buffer_hash_lookup(&context->buffer_table, ib_buffer_name);
   vk_buffer* vb = buffer_hash_lookup(&context->buffer_table, vb_buffer_name);

   vk_device* devices = &context->devices;
   VkDeviceAddress ib_address = buffer_device_address(ib, devices);
   VkDeviceAddress vb_address = buffer_device_address(vb, devices);

   for(size i = 0; i < geometry_count; ++i)
   {
      vk_mesh_draw* draw = context->geometry.mesh_draws.data + i;

      VkAccelerationStructureGeometryKHR* ag = acceleration_geometries.data + i;

      // vertex format must match VK_FORMAT_R32G32B32_SFLOAT
      static_assert(offsetof(vertex, vz) == offsetof(vertex, vx) + sizeof(float)*2);

      ag->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
      ag->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
      ag->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

      ag->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
      ag->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
      ag->geometry.triangles.vertexStride = sizeof(vertex);
      ag->geometry.triangles.maxVertex = (u32)draw->vertex_count;
      ag->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
      ag->geometry.triangles.vertexData.deviceAddress = vb_address + (draw->vertex_offset * sizeof(vertex));
      ag->geometry.triangles.indexData.deviceAddress = ib_address + (draw->index_offset * sizeof(u32));

      VkAccelerationStructureBuildGeometryInfoKHR build_info =
      {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      build_info.geometryCount = 1;
      build_info.pGeometries = ag;

      VkAccelerationStructureBuildSizesInfoKHR size_info =
      {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

      assert((draw->index_count % 3) == 0);
      array_add(primitive_count, (u32)draw->index_count / 3);   // triangles

      vkGetAccelerationStructureBuildSizesKHR(context->devices.logical,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &build_info, primitive_count.data + i, &size_info);

      array_add(acceleration_offsets, total_acceleration_size);
      array_add(scratch_offsets, total_scratch_size);
      array_add(acceleration_sizes, size_info.accelerationStructureSize);

      total_acceleration_size = (total_acceleration_size + size_info.accelerationStructureSize + alignment-1) & ~(alignment-1);
      total_scratch_size = (total_scratch_size + size_info.buildScratchSize + alignment-1) & ~(alignment-1);
   }

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->devices.physical, &memory_props);

   vk_buffer blas_buffer = {.size = total_acceleration_size};

   if(!vk_buffer_create_and_bind(&blas_buffer, devices,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return false;

   vk_buffer scratch_buffer = {.size = total_scratch_size};

   if(!vk_buffer_create_and_bind(&scratch_buffer, devices,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return false;

   array_set_size(context->blas.blases, context->storage, geometry_count);

   for(size i = 0; i < geometry_count; ++i)
   {
      VkAccelerationStructureCreateInfoKHR info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};

      assert(blas_buffer.size >= info.size + info.offset);
      assert((info.offset & 0xff) == 0);

      info.buffer = blas_buffer.handle;
      info.offset = acceleration_offsets.data[i];
      info.size = acceleration_sizes.data[i];
      info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

      if(!vk_valid(vkCreateAccelerationStructureKHR(context->devices.logical, &info, 0, &context->blas.blases.data[i])))
         return false;
   }

   vk_buffer_destroy(devices, &scratch_buffer);

   printf("Ray tracing acceleration structure size: \t%zu KB\n", total_acceleration_size / (1024));
   printf("Ray tracing build scratch size: \t\t%zu KB\n", total_scratch_size / (1024));

   return true;
}
