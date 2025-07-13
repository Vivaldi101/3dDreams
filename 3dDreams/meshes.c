#define _CRT_SECURE_NO_WARNINGS 1

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "../extern/tinyobjloader-c/tinyobj_loader_c.h"

#define CGLTF_IMPLEMENTATION
#include "../extern/cgltf/cgltf.h"
#include "../assets/shaders/mesh.h"

#include "vulkan_ng.h"

typedef struct vertex vertex;
static void vk_buffer_destroy(VkDevice device, vk_buffer* buffer);

typedef struct 
{
   arena scratch;
} obj_user_ctx;

typedef struct 
{
   arena scratch;
} gltf_user_ctx;

static void meshlet_add_new_vertex_index(u32 index, u8* meshlet_vertices, struct meshlet* ml)
{
   if(meshlet_vertices[index] == 0xff)
   {
      meshlet_vertices[index] = ml->vertex_count;

      assert(meshlet_vertices[index] < array_count(ml->vertex_index_buffer));
      // store index into the main vertex buffer
      ml->vertex_index_buffer[meshlet_vertices[index]] = index;
      ml->vertex_count++;
   }
}

static mesh meshlet_build(arena* storage, size vertex_count, u32* index_buffer, size index_count)
{
   mesh result = {};

   struct meshlet ml = {};

   u8* meshlet_vertices = push(storage, u8, vertex_count);
   result.meshlets.arena = storage;

   // 0xff means the vertex index is not in use yet
   memset(meshlet_vertices, 0xff, vertex_count);

   usize max_index_count = array_count(ml.primitive_indices);
   usize max_vertex_count = array_count(ml.vertex_index_buffer);
   usize max_triangle_count = max_index_count/3;

   for(size i = 0; i < index_count; i += 3)
   {
      // original per primitive (triangle indices)
      u32 i0 = index_buffer[i + 0];
      u32 i1 = index_buffer[i + 1];
      u32 i2 = index_buffer[i + 2];

      // are the mesh vertex indices not used yet
      bool mi0 = meshlet_vertices[i0] == 0xff;
      bool mi1 = meshlet_vertices[i1] == 0xff;
      bool mi2 = meshlet_vertices[i2] == 0xff;

      // flush meshlet if vertexes or primitives overflow
      if((ml.vertex_count + (mi0 + mi1 + mi2) > max_vertex_count) || 
         (ml.triangle_count + 1 > max_triangle_count))
      {
         array_push(result.meshlets) = ml;

         // clear the vertex indices used for this meshlet so that they can be used for the next one
         for(u32 j = 0; j < ml.vertex_count; ++j)
         {
            assert(ml.vertex_index_buffer[j] < vertex_count);
            meshlet_vertices[ml.vertex_index_buffer[j]] = 0xff;
         }

         // begin another meshlet
         struct_clear(ml);
      }

      assert(i0 < vertex_count);
      assert(i1 < vertex_count);
      assert(i2 < vertex_count);

      meshlet_add_new_vertex_index(i0, meshlet_vertices, &ml);
      meshlet_add_new_vertex_index(i1, meshlet_vertices, &ml);
      meshlet_add_new_vertex_index(i2, meshlet_vertices, &ml);

      assert(ml.triangle_count*3 + 2 < array_count(ml.primitive_indices));

      assert(meshlet_vertices[i0] >= 0);
      assert(meshlet_vertices[i1] >= 0);
      assert(meshlet_vertices[i2] >= 0);

      assert(meshlet_vertices[i0] <= ml.vertex_count);
      assert(meshlet_vertices[i1] <= ml.vertex_count);
      assert(meshlet_vertices[i2] <= ml.vertex_count);

      ml.primitive_indices[ml.triangle_count * 3 + 0] = meshlet_vertices[i0];
      ml.primitive_indices[ml.triangle_count * 3 + 1] = meshlet_vertices[i1];
      ml.primitive_indices[ml.triangle_count * 3 + 2] = meshlet_vertices[i2];

      ml.triangle_count++;   // primitive index triplet done

      // within max bounds
      assert(ml.vertex_count <= max_vertex_count);
      assert(ml.triangle_count <= max_triangle_count);
   }

   // add any left over meshlets
   if(ml.vertex_count > 0)
      array_push(result.meshlets) = ml;

   return result;
}

static vk_buffer vk_buffer_create(VkDevice device, size size, VkPhysicalDeviceMemoryProperties memory_properties, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags)
{
   vk_buffer buffer = {};

   VkBufferCreateInfo create_info = {vk_info(BUFFER)};
   create_info.size = size;
   create_info.usage = usage;

   if(!vk_valid(vkCreateBuffer(device, &create_info, 0, &buffer.handle)))
      return (vk_buffer){};

   VkMemoryRequirements memory_reqs;
   vkGetBufferMemoryRequirements(device, buffer.handle, &memory_reqs);

   u32 memory_index = memory_properties.memoryTypeCount;
   u32 i = 0;

   while(i < memory_index)
   {
      if(((memory_reqs.memoryTypeBits & (1 << i)) && memory_properties.memoryTypes[i].propertyFlags == memory_flags))
         memory_index = i;

      ++i;
   }

   if(i == memory_properties.memoryTypeCount)
      return (vk_buffer){};

   VkMemoryAllocateInfo allocate_info = {vk_info_allocate(MEMORY)};
   allocate_info.allocationSize = memory_reqs.size;
   allocate_info.memoryTypeIndex = memory_index;

   VkDeviceMemory memory = 0;
   if(!vk_valid(vkAllocateMemory(device, &allocate_info, 0, &memory)))
      return (vk_buffer){};

   if(!vk_valid(vkBindBufferMemory(device, buffer.handle, memory, 0)))
      return (vk_buffer){};

   if(memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      if(!vk_valid(vkMapMemory(device, memory, 0, allocate_info.allocationSize, 0, &buffer.data)))
         return (vk_buffer) {};

   buffer.size = allocate_info.allocationSize;
   buffer.memory = memory;

   return buffer;
}

#if 1
// TODO: extract the non-obj parts out of this and reuse for vertex de-duplication
static void obj_parse(vk_context* context, arena scratch, tinyobj_attrib_t* attrib)
{
   // TODO: obj part
   // TODO: remove and use vertex_deduplicate()
   index_hash_table(hash_key_obj) obj_table = {};

   // only triangles allowed
   assert(attrib->num_face_num_verts * 3 == attrib->num_faces);

   const usize index_count = attrib->num_faces;

   assert((size)index_count*sizeof(u32) <= (u32)~0u);

   obj_table.max_count = index_count;

   obj_table.keys = push(&scratch, hash_key_obj, obj_table.max_count);
   obj_table.values = push(&scratch, hash_value, obj_table.max_count);

   memset(obj_table.keys, -1, sizeof(hash_key_obj) * obj_table.max_count);

   u32 vertex_index = 0;
   u32 primitive_index = 0;

   u32* ib_data = push(&scratch, u32, index_count);
   array(vertex) vb_data = {.arena = &scratch};

   for(usize f = 0; f < index_count; f += 3)
   {
      const tinyobj_vertex_index_t* vidx = attrib->faces + f;

      for(usize i = 0; i < 3; ++i)
      {
         i32 vi = vidx[i].v_idx;
         i32 vti = vidx[i].vt_idx;
         i32 vni = vidx[i].vn_idx;

         hash_key_obj index = (hash_key_obj){.vi = vi, .vni = vni, .vti = vti};
         hash_value lookup = hash_lookup(&obj_table, index);

         if(lookup == ~0u)
         {
            struct vertex v = {};
            if(vi >= 0)
            {
               v.vx = attrib->vertices[vi * 3 + 0];
               v.vy = attrib->vertices[vi * 3 + 1];
               v.vz = attrib->vertices[vi * 3 + 2];
            }

            if(vni >= 0)
            {
               f32 nx = attrib->normals[vni * 3 + 0];
               f32 ny = attrib->normals[vni * 3 + 1];
               f32 nz = attrib->normals[vni * 3 + 2];
               v.nx = (u8)(nx * 127.f + 127.f);
               v.ny = (u8)(ny * 127.f + 127.f);
               v.nz = (u8)(nz * 127.f + 127.f);
            }

            if(vti >= 0)
            {
               v.tu = attrib->texcoords[vti * 2 + 0];
               v.tv = attrib->texcoords[vti * 2 + 1];
            }

            hash_insert(&obj_table, index, vertex_index);
            ib_data[primitive_index] = vertex_index++;
            array_push(vb_data) = v;
         }
         else
            ib_data[primitive_index] = lookup;
         ++primitive_index;
      }
   }

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_props);

   mesh m = meshlet_build(context->storage, vb_data.count, ib_data, index_count);

   usize vb_size = vb_data.count * sizeof(struct vertex);
   usize mb_size = m.meshlets.count * sizeof(struct meshlet);
   usize ib_size = index_count * sizeof(u32);

   context->index_count = (u32)index_count;
   context->meshlet_count = (u32)m.meshlets.count;
   context->meshlet_buffer = m.meshlets.data;

   vk_buffer index_buffer = vk_buffer_create(context->logical_device, ib_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   vk_buffer vertex_buffer = vk_buffer_create(context->logical_device, vb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   vk_buffer meshlet_buffer = vk_buffer_create(context->logical_device, mb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   context->vb = vertex_buffer;
   context->mb = meshlet_buffer;
   context->ib = index_buffer;

   // temp buffer
   size scratch_buffer_size = max(mb_size, max(vb_size, ib_size));
   vk_buffer scratch_buffer = vk_buffer_create(context->logical_device, scratch_buffer_size, memory_props, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

   // upload vertex data
   vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->vb,
      scratch_buffer, vb_data.data, vb_size);
   // upload meshlet data
   vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->mb,
      scratch_buffer, context->meshlet_buffer, mb_size);
   // upload index data
   vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->ib,
      scratch_buffer, ib_data, ib_size);

   vk_buffer_destroy(context->logical_device, &scratch_buffer);
}
#endif

static void vertex_deduplicate(arena scratch, size index_count)
{
   index_hash_table hash_table = {};

   hash_table.max_count = index_count;

   hash_table.keys = push(&scratch, hash_key_obj, hash_table.max_count);
   hash_table.values = push(&scratch, hash_value, hash_table.max_count);

   memset(hash_table.keys, -1, sizeof(hash_key_obj) * hash_table.max_count);

   u32 vertex_index = 0;
   u32 primitive_index = 0;

   u32* ib_data = push(&scratch, u32, index_count);
   array(vertex) vb_data = {.arena = &scratch};

#if 0
   for(usize f = 0; f < index_count; f += 3)
   {
      const tinyobj_vertex_index_t* vidx = attrib->faces + f;

      for(usize i = 0; i < 3; ++i)
      {
         i32 vi = vidx[i].v_idx;
         i32 vti = vidx[i].vt_idx;
         i32 vni = vidx[i].vn_idx;

         hash_key_obj index = (hash_key_obj){.vi = vi, .vni = vni, .vti = vti};
         hash_value lookup = hash_lookup(&hash_table, index);

         if(lookup == ~0u)
         {
            struct vertex v = {};
            if(vi >= 0)
            {
               v.vx = attrib->vertices[vi * 3 + 0];
               v.vy = attrib->vertices[vi * 3 + 1];
               v.vz = attrib->vertices[vi * 3 + 2];
            }

            if(vni >= 0)
            {
               f32 nx = attrib->normals[vni * 3 + 0];
               f32 ny = attrib->normals[vni * 3 + 1];
               f32 nz = attrib->normals[vni * 3 + 2];
               v.nx = (u8)(nx * 127.f + 127.f);
               v.ny = (u8)(ny * 127.f + 127.f);
               v.nz = (u8)(nz * 127.f + 127.f);
            }

            if(vti >= 0)
            {
               v.tu = attrib->texcoords[vti * 2 + 0];
               v.tv = attrib->texcoords[vti * 2 + 1];
            }

            hash_insert(&hash_table, index, vertex_index);
            ib_data[primitive_index] = vertex_index++;
            array_push(vb_data) = v;
         }
         else
            ib_data[primitive_index] = lookup;
         ++primitive_index;
      }
   }
#endif
}

static vk_buffer vk_buffer_create(VkDevice device, size size, VkPhysicalDeviceMemoryProperties memory_properties, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags);

static bool gltf_parse(vk_context* context, arena scratch, s8 gltf_path)
{
   cgltf_options options = {};
   cgltf_data* data = 0;
   cgltf_result result = cgltf_parse_file(&options, s8_data(gltf_path), &data);

   if(result != cgltf_result_success)
      return false;

   result = cgltf_load_buffers(&options, data, s8_data(gltf_path));
   if(result != cgltf_result_success)
   {
      cgltf_free(data);
      return false;
   }

   result = cgltf_validate(data);
   if(result != cgltf_result_success)
   {
      cgltf_free(data);
      return false;
   }

   context->mesh_draws.arena = context->storage;

   for(cgltf_size i = 0; i < data->meshes_count; ++i)
   {
      cgltf_mesh* gltf_mesh = &data->meshes[i];
      assert(gltf_mesh->primitives_count == 1);

      cgltf_primitive* prim = &gltf_mesh->primitives[0];
      assert(prim->type == cgltf_primitive_type_triangles);

      cgltf_size vertex_count = 0;
      cgltf_accessor* position_accessor = 0;
      cgltf_accessor* normal_accessor = 0;
      cgltf_accessor* texcoord_accessor = 0;

      // parse attribute types
      for(cgltf_size j = 0; j < prim->attributes_count; ++j)
      {
         cgltf_attribute* attr = &prim->attributes[j];

         cgltf_attribute_type attr_type;
         i32 attr_index;
         cgltf_parse_attribute_type(attr->name, &attr_type, &attr_index);

         switch(attr_type)
         {
            case cgltf_attribute_type_position:
               position_accessor = attr->data;
               vertex_count = position_accessor->count;
               break;

            case cgltf_attribute_type_normal:
               normal_accessor = attr->data;
               break;

            case cgltf_attribute_type_texcoord:
               if(attr_index == 0) // first uv set only
                  texcoord_accessor = attr->data;
               break;

            default:
               // ignore other attributes (e.g., color, joints, etc.)
               break;
         }
      }

      // load vertices
      array vertices = array_make(context->storage, sizeof(vertex), vertex_count);
      for(cgltf_size k = 0; k < vertex_count; ++k)
      {
         vertex vert = {};

         if(position_accessor)
         {
            f32 pos[3] = {};
            cgltf_accessor_read_float(position_accessor, k, pos, 3);
            vert.vx = pos[0];
            vert.vy = pos[1];
            vert.vz = pos[2];
         }
         if(normal_accessor)
         {
            f32 norm[3] = {};
            cgltf_accessor_read_float(normal_accessor, k, norm, 3);
            // pack normals
            vert.nx = (uint8_t)((norm[0] * 0.5f + 0.5f) * 255.0f);
            vert.ny = (uint8_t)((norm[1] * 0.5f + 0.5f) * 255.0f);
            vert.nz = (uint8_t)((norm[2] * 0.5f + 0.5f) * 255.0f);
         }
         if(texcoord_accessor)
         {
            f32 uv[2] = {};
            cgltf_accessor_read_float(texcoord_accessor, k, uv, 2);
            vert.tu = uv[0];
            vert.tv = uv[1];
         }

         array_add(vertices, vert);
      }

      // load indices
      cgltf_accessor* accessor = prim->indices;
      array indices = array_make(context->storage, sizeof(u32), prim->indices->count);
      cgltf_size index_count = cgltf_accessor_unpack_indices(prim->indices, indices.data, 4, prim->indices->count);
      indices.count = index_count;

      // TODO: Fill mesh draw offsets for multi-mesh rendering
      array_push(context->mesh_draws) = (mesh_draw){};

      VkPhysicalDeviceMemoryProperties memory_props;
      vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_props);

      mesh m = meshlet_build(context->storage, vertex_count, indices.data, index_count);

      usize vb_size = vertex_count * sizeof(struct vertex);
      usize mb_size = m.meshlets.count * sizeof(struct meshlet);
      usize ib_size = index_count * sizeof(u32);

      context->index_count = (u32)index_count;
      context->meshlet_count = (u32)m.meshlets.count;
      context->meshlet_buffer = m.meshlets.data;

      vk_buffer index_buffer = vk_buffer_create(context->logical_device, ib_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vk_buffer vertex_buffer = vk_buffer_create(context->logical_device, vb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vk_buffer meshlet_buffer = vk_buffer_create(context->logical_device, mb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      context->vb = vertex_buffer;
      context->mb = meshlet_buffer;
      context->ib = index_buffer;

      // temp buffer
      size scratch_buffer_size = max(mb_size, max(vb_size, ib_size));
      vk_buffer scratch_buffer = vk_buffer_create(context->logical_device, scratch_buffer_size, memory_props, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

      // upload vertex data
      vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->vb,
         scratch_buffer, vertices.data, vb_size);
      // upload meshlet data
      vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->mb,
         scratch_buffer, context->meshlet_buffer, mb_size);
      // upload index data
      vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->ib,
         scratch_buffer, indices.data, ib_size);

      vk_buffer_destroy(context->logical_device, &scratch_buffer);
   }

   cgltf_free(data);

   return true;
}
