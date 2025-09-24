#define _CRT_SECURE_NO_WARNINGS 1

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "../extern/tinyobjloader-c/tinyobj_loader_c.h"

#define CGLTF_IMPLEMENTATION
#include "../extern/cgltf/cgltf.h"
#include "../assets/shaders/mesh.h"

#include "vulkan_ng.h"

static void vk_textures_log(vk_context* context);
static void vk_texture_load(vk_context* context, s8 img_uri, s8 gltf_path);

// TODO: remove .obj path
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

static vk_meshlet_buffer meshlet_build(arena scratch, size vertex_count, u32* index_buffer, size index_count)
{
   vk_meshlet_buffer result = {0};

   struct meshlet ml = {0};

   u8* meshlet_vertices = push(&scratch, u8, vertex_count);
   result.meshlets.arena = &scratch;

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
   vk_buffer buffer = {0};

   VkBufferCreateInfo create_info = {vk_info(BUFFER)};
   create_info.size = size;
   create_info.usage = usage;

   if(!vk_valid(vkCreateBuffer(device, &create_info, 0, &buffer.handle)))
      return (vk_buffer){0};

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
      return (vk_buffer){0};

   VkMemoryAllocateInfo allocate_info = {vk_info_allocate(MEMORY)};
   allocate_info.allocationSize = memory_reqs.size;
   allocate_info.memoryTypeIndex = memory_index;

   VkDeviceMemory memory = 0;
   if(!vk_valid(vkAllocateMemory(device, &allocate_info, 0, &memory)))
      return (vk_buffer){0};

   if(!vk_valid(vkBindBufferMemory(device, buffer.handle, memory, 0)))
      return (vk_buffer){0};

   if(memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      if(!vk_valid(vkMapMemory(device, memory, 0, allocate_info.allocationSize, 0, &buffer.data)))
         return (vk_buffer) {0};

   buffer.size = allocate_info.allocationSize;
   buffer.memory = memory;

   return buffer;
}

// TODO: extract the non-obj parts out of this and reuse for vertex de-duplication
static vk_buffer_objects obj_load(vk_context* context, arena scratch, tinyobj_attrib_t* attrib)
{
   vk_buffer_objects result = {};

   context->mesh_draws.arena = context->storage;
   // single .obj mesh
   array_resize(context->mesh_draws, 1);

   // TODO: obj part
   // TODO: remove and use vertex_deduplicate()
   index_hash_table(hash_key_obj) obj_table = {0};

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
   array(vertex) vb_data = {&scratch};

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
            struct vertex v = {0};
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

   vk_meshlet_buffer mb = meshlet_build(*context->storage, vb_data.count, ib_data, index_count);

   usize vb_size = vb_data.count * sizeof(struct vertex);
   usize mb_size = mb.meshlets.count * sizeof(struct meshlet);
   usize ib_size = index_count * sizeof(u32);

   context->meshlet_count = (u32)mb.meshlets.count;

   result.vb = vk_buffer_create(context->logical_device, vb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   result.mb = vk_buffer_create(context->logical_device, mb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   result.ib = vk_buffer_create(context->logical_device, ib_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   // temp buffer
   size scratch_buffer_size = max(mb_size, max(vb_size, ib_size));
   vk_buffer scratch_buffer = vk_buffer_create(context->logical_device, scratch_buffer_size, memory_props, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

   // upload vertex data
   vk_buffer_upload(context, result.vb, scratch_buffer, vb_data.data, vb_size);
   // upload index data
   vk_buffer_upload(context, result.ib, scratch_buffer, ib_data, ib_size);
   // upload meshlet data
   vk_buffer_upload(context, result.mb, scratch_buffer, mb.meshlets.data, mb_size);

   vk_mesh_draw md = {0};
   //md.index_count = index_count;
   array_add(context->mesh_draws, md);

   vk_buffer_destroy(context->logical_device, &scratch_buffer);

   return result;
}

static size gltf_index_count(cgltf_data* data)
{
   size index_count = 0;
   for(usize i = 0; i < data->meshes_count; ++i)
   {
      cgltf_mesh* gltf_mesh = &data->meshes[i];
      assert(gltf_mesh->primitives_count == 1);

      cgltf_primitive* prim = &gltf_mesh->primitives[0];
      assert(prim->type == cgltf_primitive_type_triangles);

      index_count += prim->indices->count;
   }

   return index_count;
}

static vk_descriptors vk_descriptors_create(vk_context* context, arena scratch)
{
   vk_descriptors result = {};

   // TODO: semcompress these descriptor sets out
   u32 descriptor_count = 1 << 16;

   VkDescriptorPoolSize pool_size =
   {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = descriptor_count
   };

   VkDescriptorPoolCreateInfo pool_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
       .maxSets = 1,
       .poolSizeCount = 1,
       .pPoolSizes = &pool_size
   };

   VkDescriptorPool descriptor_pool = 0;
   vk_assert(vkCreateDescriptorPool(context->logical_device, &pool_info, 0, &descriptor_pool));

   VkSamplerCreateInfo sampler_info =
   {
       .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
       .magFilter = VK_FILTER_LINEAR,
       .minFilter = VK_FILTER_LINEAR,
       .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
       .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
       .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
       .anisotropyEnable = VK_TRUE,
       .maxAnisotropy = 16,
       .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
       .unnormalizedCoordinates = VK_FALSE,
       .compareEnable = VK_FALSE,
       .compareOp = VK_COMPARE_OP_ALWAYS,
       .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
       .mipLodBias = 0.0f,
       .minLod = 0.0f,
       .maxLod = VK_LOD_CLAMP_NONE,
   };

   VkSampler immutable_sampler;
   vk_assert(vkCreateSampler(context->logical_device, &sampler_info, 0, &immutable_sampler));

   VkDescriptorSetLayoutBinding binding =
   {
       .binding = 0,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = descriptor_count,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
       .pImmutableSamplers = 0
   };

   VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

   VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
       .bindingCount = 1,
       .pBindingFlags = &binding_flags
   };

   VkDescriptorSetLayoutCreateInfo layout_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
       .pNext = &binding_flags_info,
       .bindingCount = 1,
       .pBindings = &binding,
   };

   VkDescriptorSetLayout descriptor_set_layout = 0;
   vk_assert(vkCreateDescriptorSetLayout(context->logical_device, &layout_info, 0, &descriptor_set_layout));

   VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
       .descriptorSetCount = 1,
       .pDescriptorCounts = &context->textures.count,
   };

   VkDescriptorSetAllocateInfo alloc_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
       .pNext = &variable_count_info,
       .descriptorPool = descriptor_pool,
       .descriptorSetCount = 1,
       .pSetLayouts = &descriptor_set_layout,
   };

   VkDescriptorSet descriptor_set;
   vk_assert(vkAllocateDescriptorSets(context->logical_device, &alloc_info, &descriptor_set));

   result.set[1] = descriptor_set;
   result.layouts[1] = descriptor_set_layout;
   result.count = descriptor_count;

   array(VkDescriptorImageInfo) image_infos = { &scratch };
   array_resize(image_infos, context->textures.count);

   for(uint32_t i = 0; i < context->textures.count; ++i)
   {
      image_infos.data[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image_infos.data[i].imageView = context->textures.data[i].image.view;
      image_infos.data[i].sampler = immutable_sampler;
   }

   VkWriteDescriptorSet write =
   {
       .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .dstSet = descriptor_set,
       .dstBinding = 0,
       .dstArrayElement = 0,
       .descriptorCount = (u32)context->textures.count,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .pImageInfo = image_infos.data,
   };

   vkUpdateDescriptorSets(context->logical_device, 1, &write, 0, 0);

   return result;
}

static vk_buffer_objects vk_gltf_load(vk_context* context, s8 gltf_path)
{
   vk_buffer_objects result = {};

   cgltf_options options = {0};
   cgltf_data* data = 0;
   cgltf_result gltf_result = cgltf_parse_file(&options, s8_data(gltf_path), &data);

   if(gltf_result != cgltf_result_success)
      return (vk_buffer_objects){};

   gltf_result = cgltf_load_buffers(&options, data, s8_data(gltf_path));
   if(gltf_result != cgltf_result_success)
   {
      cgltf_free(data);
      return (vk_buffer_objects){};
   }

   gltf_result = cgltf_validate(data);
   if(gltf_result != cgltf_result_success)
   {
      cgltf_free(data);
      return (vk_buffer_objects){};
   }

   size index_offset = 0;
   size vertex_offset = 0;

   array(vertex) vertices = {context->storage};

   // preallocate indices
   array(u32) indices = {context->storage};
   array_resize(indices, gltf_index_count(data));

   // preallocate meshes
   context->mesh_draws.arena = context->storage;
   array_resize(context->mesh_draws, data->meshes_count);

   // preallocate instances
   context->mesh_instances.arena = context->storage;
   array_resize(context->mesh_instances, data->nodes_count);

   // preallocate textures
   context->textures.arena = context->storage;
   array_resize(context->textures, data->textures_count);

   for(usize i = 0; i < data->meshes_count; ++i)
   {
      cgltf_mesh* gltf_mesh = data->meshes + i;
      assert(gltf_mesh->primitives_count == 1);

      cgltf_primitive* prim = gltf_mesh->primitives + 0;
      assert(prim->type == cgltf_primitive_type_triangles);

      usize vertex_count = 0;
      cgltf_accessor* position_accessor = 0;
      cgltf_accessor* normal_accessor = 0;
      cgltf_accessor* texcoord_accessor = 0;

      // parse attribute types
      for(usize j = 0; j < prim->attributes_count; ++j)
      {
         cgltf_attribute* attr = prim->attributes + j;

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
      for(usize k = 0; k < vertex_count; ++k)
      {
         vertex vert = {0};

         if(position_accessor)
         {
            f32 pos[3] = {0};
            cgltf_accessor_read_float(position_accessor, k, pos, 3);
            vert.vx = pos[0];
            vert.vy = pos[1];
            vert.vz = pos[2];
         }
         if(normal_accessor)
         {
            f32 norm[3] = {0};
            cgltf_accessor_read_float(normal_accessor, k, norm, 3);
            // pack normals
            vert.nx = (uint8_t)((norm[0] * 0.5f + 0.5f) * 255.0f);
            vert.ny = (uint8_t)((norm[1] * 0.5f + 0.5f) * 255.0f);
            vert.nz = (uint8_t)((norm[2] * 0.5f + 0.5f) * 255.0f);
         }
         if(texcoord_accessor)
         {
            f32 uv[2] = {0};
            cgltf_accessor_read_float(texcoord_accessor, k, uv, 2);
            vert.tu = uv[0];
            vert.tv = uv[1];
         }

         array_push(vertices) = vert;
      }

      // load indices
      cgltf_accessor* accessor = prim->indices;
      usize index_count = cgltf_accessor_unpack_indices(prim->indices, indices.data + indices.count, 4, prim->indices->count);
      indices.count += index_count;

      // mesh offsets
      vk_mesh_draw md = {0};
      md.index_count = index_count;
      md.index_offset = index_offset;
      md.vertex_offset = vertex_offset;

      array_add(context->mesh_draws, md);

      index_offset += index_count;
      vertex_offset += vertex_count;
   }

   for(usize i = 0; i < data->nodes_count; ++i)
   {
      cgltf_node* node = data->nodes + i;
      if(node->mesh)
      {
         mat4 wm = {0};
         cgltf_node_transform_world(node, wm.data);

         usize mesh_index = cgltf_mesh_index(data, node->mesh);

         vk_mesh_instance mi = {0};

         // index into the mesh to draw
         mi.mesh_index = mesh_index;
#if 0

         f32 s[3], r[4], t[3];
         transform_decompose(t, r, s, wm);

         // mesh instance geometry
         mi.orientation = (vec4){r[0], r[1], r[2], r[3]};
         mi.pos = (vec3){t[0], t[1], t[2]};
         // TODO: no uniform scaling
         mi.scale = max(max(s[0], s[1]), s[2]);
#else
         mi.model = wm;
#endif

         array_add(context->mesh_instances, mi);
      }
   }

   for(usize i = 0; i < data->textures_count; ++i)
   {
      cgltf_texture* cgltf_tex = data->textures + i;
      assert(cgltf_tex->image);

      cgltf_image* img = cgltf_tex->image;
      assert(img->uri);

      cgltf_decode_uri(img->uri);

      vk_texture_load(context, s8(img->uri), gltf_path);
   }

   cgltf_free(data);

   vk_meshlet_buffer mb = meshlet_build(*context->storage, vertices.count, indices.data, indices.count);
   context->meshlet_count = (u32)mb.meshlets.count;

   usize mb_size = mb.meshlets.count * sizeof(struct meshlet);
   usize vb_size = vertices.count * sizeof(struct vertex);
   usize ib_size = indices.count * sizeof(u32);
   usize scratch_buffer_size = max(mb_size, max(vb_size, ib_size));

   VkPhysicalDeviceMemoryProperties memory_props;
   vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_props);
   vk_buffer scratch_buffer = vk_buffer_create(context->logical_device, scratch_buffer_size, memory_props, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

   result.vb = vk_buffer_create(context->logical_device, vb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   result.mb = vk_buffer_create(context->logical_device, mb_size, memory_props, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   result.ib = vk_buffer_create(context->logical_device, ib_size, memory_props, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   // vertex data
   vk_buffer_upload(context, result.vb, scratch_buffer, vertices.data, vb_size);
   // index data
   vk_buffer_upload(context, result.ib, scratch_buffer, indices.data, ib_size);
   // meshlet data
   vk_buffer_upload(context, result.mb, scratch_buffer, mb.meshlets.data, mb_size);

   vk_buffer_destroy(context->logical_device, &scratch_buffer);

   return result;
}
