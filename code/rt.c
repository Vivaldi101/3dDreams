#include "vulkan_ng.h"

static bool rt_blas_buffer_create(vk_context* context)
{
   const size geometry_count = context->geometry.mesh_draws.count;
   const size alignment = 256;

   size total_acceleration_size = 0;
   size total_scratch_size = 0;

   arena s = *context->storage;

   vk_blas* blas = &context->blas;
   array_set_size(blas->blases, geometry_count, &s);

   VkAccelerationStructureGeometryKHR* acceleration_geometries =
      push(&s, typeof(*acceleration_geometries), geometry_count);
   VkAccelerationStructureBuildGeometryInfoKHR* build_infos =
      push(&s, typeof(*build_infos), geometry_count);

   VkAccelerationStructureBuildRangeInfoKHR* build_ranges =
      push(&s, typeof(*build_ranges), geometry_count);
   VkAccelerationStructureBuildRangeInfoKHR** build_range_ptrs =
      push(&s, typeof(*build_range_ptrs), geometry_count);

   u32* primitive_count =
      push(&s, u32, geometry_count);   // triangles
   size* acceleration_offsets =
      push(&s, size, geometry_count);
   size* scratch_offsets =
      push(&s, size, geometry_count);
   size* acceleration_sizes =
      push(&s, size, geometry_count);

   vk_buffer* ib = buffer_hash_lookup(&context->buffer_table, ib_buffer_name);
   vk_buffer* vb = buffer_hash_lookup(&context->buffer_table, vb_buffer_name);

   vk_device* devices = &context->devices;
   VkDeviceAddress ib_address = buffer_device_address(ib, devices);
   VkDeviceAddress vb_address = buffer_device_address(vb, devices);

   for(size i = 0; i < geometry_count; ++i)
   {
      vk_mesh_draw* draw = context->geometry.mesh_draws.data + i;

      VkAccelerationStructureGeometryKHR* ag = acceleration_geometries + i;

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
      primitive_count[i] = (u32)draw->index_count / 3;   // triangles

      vkGetAccelerationStructureBuildSizesKHR(context->devices.logical,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &build_info, primitive_count + i, &size_info);

      acceleration_offsets[i] = total_acceleration_size;
      scratch_offsets[i] = total_scratch_size;
      acceleration_sizes[i] = size_info.accelerationStructureSize;
      build_infos[i] = build_info;

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
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return false;

   VkDeviceAddress scratch_address = buffer_device_address(&scratch_buffer, devices);

   for(size i = 0; i < geometry_count; ++i)
   {
      VkAccelerationStructureCreateInfoKHR info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};

      assert(blas_buffer.size >= info.size + info.offset);
      assert((info.offset & 0xff) == 0);

      info.buffer = blas_buffer.handle;
      info.offset = acceleration_offsets[i];
      info.size = acceleration_sizes[i];
      info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

      if(!vk_valid(vkCreateAccelerationStructureKHR(context->devices.logical, &info, 0, context->blas.blases.data + i)))
         return false;

      build_infos[i].dstAccelerationStructure = blas->blases.data[i];
      build_infos[i].scratchData.deviceAddress = scratch_address + scratch_offsets[i];

      build_ranges[i].primitiveCount = primitive_count[i];
      build_range_ptrs[i] = build_ranges + i;
   }

   vk_assert(vkResetCommandPool(devices->logical, context->command_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vk_assert(vkBeginCommandBuffer(context->command_buffer, &buffer_begin_info));

   vkCmdBuildAccelerationStructuresKHR(context->command_buffer, (u32)geometry_count, build_infos, build_range_ptrs);

   vk_assert(vkEndCommandBuffer(context->command_buffer));

   VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &context->command_buffer;

   vk_assert(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

   if(!vk_valid(vkDeviceWaitIdle(devices->logical)))
      return false;

   vk_buffer_destroy(devices, &scratch_buffer);

   printf("Ray tracing acceleration structure size: \t%zu KB\n", total_acceleration_size / (1024));
   printf("Ray tracing build scratch size: \t\t%zu KB\n", total_scratch_size / (1024));

   return true;
}
