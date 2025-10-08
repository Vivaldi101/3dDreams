#include "vulkan_ng.h"

static void vk_buffer_destroy(VkDevice device, vk_buffer* buffer)
{
   vkFreeMemory(device, buffer->memory, 0);
   vkDestroyBuffer(device, buffer->handle, 0);
}

static void vk_buffer_to_image_upload(vk_context* context, vk_buffer scratch, VkImage image, VkExtent3D image_extent, const void* data, VkDeviceSize dev_size)
{
   assert(data);
   assert(dev_size > 0);
   assert(scratch.data && scratch.size >= (size)dev_size);
   assert(image_extent.width && image_extent.height && image_extent.depth);
   assert(vk_valid_handle(image));

   memcpy(scratch.data, data, dev_size);

   vk_assert(vkResetCommandPool(context->logical_device, context->command_pool, 0));

   VkCommandBufferBeginInfo begin_info = {};
   begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vk_assert(vkBeginCommandBuffer(context->command_buffer, &begin_info));

   VkImageMemoryBarrier img_barrier_to_transfer = {};
   img_barrier_to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   img_barrier_to_transfer.srcAccessMask = 0;
   img_barrier_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   img_barrier_to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   img_barrier_to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   img_barrier_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_transfer.image = image;
   img_barrier_to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   img_barrier_to_transfer.subresourceRange.baseMipLevel = 0;
   img_barrier_to_transfer.subresourceRange.levelCount = 1;
   img_barrier_to_transfer.subresourceRange.baseArrayLayer = 0;
   img_barrier_to_transfer.subresourceRange.layerCount = 1;

   vkCmdPipelineBarrier(
      context->command_buffer,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &img_barrier_to_transfer
   );

   VkBufferImageCopy region = {};
   region.bufferOffset = 0;
   region.bufferRowLength = 0;
   region.bufferImageHeight = 0;
   region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   region.imageSubresource.mipLevel = 0;
   region.imageSubresource.baseArrayLayer = 0;
   region.imageSubresource.layerCount = 1;
   region.imageOffset.x = 0;
   region.imageOffset.y = 0;
   region.imageOffset.z = 0;
   region.imageExtent = image_extent;

   vkCmdCopyBufferToImage(
      context->command_buffer,
      scratch.handle,
      image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region
   );

   VkImageMemoryBarrier img_barrier_to_shader = {};
   img_barrier_to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   img_barrier_to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   img_barrier_to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   img_barrier_to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   img_barrier_to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   img_barrier_to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   img_barrier_to_shader.image = image;
   img_barrier_to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   img_barrier_to_shader.subresourceRange.baseMipLevel = 0;
   img_barrier_to_shader.subresourceRange.levelCount = 1;
   img_barrier_to_shader.subresourceRange.baseArrayLayer = 0;
   img_barrier_to_shader.subresourceRange.layerCount = 1;

   vkCmdPipelineBarrier(
      context->command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &img_barrier_to_shader
   );

   vk_assert(vkEndCommandBuffer(context->command_buffer));

   VkSubmitInfo submit_info = {};
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.waitSemaphoreCount = 0;
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &context->command_buffer;
   submit_info.signalSemaphoreCount = 0;

   vk_assert(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
   // instead of explicit memory sync between queue submissions with fences etc we wait for all gpu jobs to complete before moving on
   // TODO: bad for perf
   vk_assert(vkDeviceWaitIdle(context->logical_device));
}

static void vk_buffer_upload(vk_context* context, vk_buffer buffer, vk_buffer scratch, const void* data, VkDeviceSize dev_size)
{
   assert(data);
   assert(dev_size > 0);
   assert(scratch.data && scratch.size >= (size)dev_size);
   memcpy(scratch.data, data, dev_size);

   vk_assert(vkResetCommandPool(context->logical_device, context->command_pool, 0));

   VkCommandBufferBeginInfo buffer_begin_info = {vk_info_begin(COMMAND_BUFFER)};
   buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vk_assert(vkBeginCommandBuffer(context->command_buffer, &buffer_begin_info));

   VkBufferCopy buffer_region = {0, 0, dev_size};
   vkCmdCopyBuffer(context->command_buffer, scratch.handle, buffer.handle, 1, &buffer_region);

   VkBufferMemoryBarrier copy_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
   copy_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   copy_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier.buffer = buffer.handle;
   copy_barrier.size = dev_size;
   copy_barrier.offset = 0;

   vkCmdPipelineBarrier(context->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 1, &copy_barrier, 0, 0);

   vk_assert(vkEndCommandBuffer(context->command_buffer));

   VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &context->command_buffer;

   vk_assert(vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
   // instead of explicit memory sync between queue submissions with fences etc we wait for all gpu jobs to complete before moving on
   // TODO: bad for perf
   vk_assert(vkDeviceWaitIdle(context->logical_device));
}

static vk_buffer vk_buffer_transforms_create(vk_context* context, arena scratch)
{
   struct mesh_draw* draws = push(&scratch, struct mesh_draw, context->mesh_instances.count);

   for(u32 i = 0; i < context->mesh_instances.count; ++i)
   {
      draws[i].world = context->mesh_instances.data[i].world;
      draws[i].normal = (u32)context->mesh_instances.data[i].normal;
      draws[i].albedo = (u32)context->mesh_instances.data[i].albedo;
      draws[i].metal = (u32)context->mesh_instances.data[i].metal;
      draws[i].ao = (u32)context->mesh_instances.data[i].ao;
      draws[i].emissive = (u32)context->mesh_instances.data[i].emissive;
   }

   size scratch_buffer_size = context->mesh_instances.count * sizeof(struct mesh_draw);

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_props);
   vk_buffer scratch_buffer = vk_buffer_create(context, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   vk_buffer transform_buffer = vk_buffer_create(context, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   vk_buffer_upload(context, transform_buffer, scratch_buffer, draws, sizeof(struct mesh_draw) * context->mesh_instances.count);

   return transform_buffer;
}

static vk_buffer vk_buffer_indirect_create(vk_context* context, arena scratch, bool rtx_supported)
{
   vk_buffer indirect_buffer = {};

   if(!rtx_supported)
   {
      VkDrawIndexedIndirectCommand* draw_commands = push(&scratch, VkDrawIndexedIndirectCommand, context->mesh_instances.count);

      for(u32 i = 0; i < context->mesh_instances.count; ++i)
      {
         vk_mesh_instance mi = context->mesh_instances.data[i];
         vk_mesh_draw md = context->mesh_draws.data[mi.mesh_index];

         VkDrawIndexedIndirectCommand cmd =
         {
             .indexCount = (u32)md.index_count,
             .instanceCount = 1,               // one instance per mesh_instance
             .firstIndex = (u32)md.index_offset,
             .vertexOffset = (i32)md.vertex_offset,
             .firstInstance = i                // important: matches instance ID
         };

         draw_commands[i] = cmd;
      }

      size scratch_buffer_size = context->mesh_instances.count * sizeof(VkDrawIndexedIndirectCommand);

      vk_buffer scratch_buffer = vk_buffer_create(context, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      indirect_buffer = vk_buffer_create(context, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      vk_buffer_upload(context, indirect_buffer, scratch_buffer, draw_commands, sizeof(VkDrawIndexedIndirectCommand) * context->mesh_instances.count);
   }
   else
   {
      VkDrawMeshTasksIndirectCommandEXT* draw_commands = push(&scratch, VkDrawMeshTasksIndirectCommandEXT, context->mesh_instances.count);

      for(u32 i = 0; i < context->mesh_instances.count; ++i)
      {
         VkDrawMeshTasksIndirectCommandEXT cmd =
         {
            .groupCountX = 1,
            .groupCountY = 1,
            .groupCountZ = 1,
         };

         draw_commands[i] = cmd;
      }

      size scratch_buffer_size = context->mesh_instances.count * sizeof(VkDrawMeshTasksIndirectCommandEXT);

      vk_buffer scratch_buffer = vk_buffer_create(context, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      indirect_buffer = vk_buffer_create(context, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      vk_buffer_upload(context, indirect_buffer, scratch_buffer, draw_commands, sizeof(VkDrawMeshTasksIndirectCommandEXT) * context->mesh_instances.count);
   }

   return indirect_buffer;
}
