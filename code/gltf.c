#define _CRT_SECURE_NO_WARNINGS 1

// TODO: Rename to gltf.c

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "../extern/tinyobjloader-c/tinyobj_loader_c.h"

#define CGLTF_IMPLEMENTATION
#include "../extern/cgltf/cgltf.h"
#include "../assets/shaders/mesh.h"

#include "vulkan_ng.h"

static void vk_textures_log(vk_context* context);
static void vk_texture_load(vk_context* context, s8 img_uri, s8 gltf_path);

typedef struct vertex vertex;
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

static void meshlet_build(array_meshlet* result, u8* meshlet_vertices, u32* index_buffer, size index_count, u32 index_offset)
{
   struct meshlet ml = {0};

   usize max_index_count = array_count(ml.primitive_indices);
   usize max_vertex_count = array_count(ml.vertex_index_buffer);
   usize max_triangle_count = max_index_count/3;

   for(size i = 0; i < index_count; i += 3)
   {
      // original per primitive (triangle indices)
      u32 i0 = index_buffer[index_offset + i + 0];
      u32 i1 = index_buffer[index_offset + i + 1];
      u32 i2 = index_buffer[index_offset + i + 2];

      // are the mesh vertex indices not used yet
      bool mi0 = meshlet_vertices[i0] == 0xff;
      bool mi1 = meshlet_vertices[i1] == 0xff;
      bool mi2 = meshlet_vertices[i2] == 0xff;

      // flush meshlet if vertexes or primitives overflow
      if((ml.vertex_count + (mi0 + mi1 + mi2) > max_vertex_count) || 
         (ml.triangle_count + 1 > max_triangle_count))
      {
         arrayp_push(result) = ml;

         // clear the vertex indices used for this meshlet so that they can be used for the next one
         for(u32 j = 0; j < ml.vertex_count; ++j)
            meshlet_vertices[ml.vertex_index_buffer[j]] = 0xff;

         // begin another meshlet
         struct_clear(&ml);
      }

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
      arrayp_push(result) = ml;
}

#if 0
// TODO: extract the non-obj parts out of this and reuse for vertex de-duplication
static vk_buffer_objects obj_load(vk_context* context, arena scratch, tinyobj_attrib_t* attrib)
{
   vk_buffer_objects result = {0};

   context->geometry.mesh_draws.arena = context->storage;
   // single .obj mesh
   array_resize(context->geometry.mesh_draws, 1);

   context->geometry.mesh_instances.arena = context->storage;
   // single .obj mesh
   array_resize(context->geometry.mesh_instances, 1);

   // TODO: obj part
   // TODO: remove and use vertex_deduplicate()
   index_hash_table obj_table = {0};

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
   vkGetPhysicalDeviceMemoryProperties(context->devices.physical, &memory_props);

   vk_meshlet_buffer mb = meshlet_build(*context->storage, vb_data.count, ib_data, index_count);

   usize vb_size = vb_data.count * sizeof(vertex);
   usize mb_size = mb.meshlets.count * sizeof(meshlet);
   usize ib_size = index_count * sizeof(u32);

   context->meshlet_count = (u32)mb.meshlets.count;

   result.vb.size = vb_size;
   vk_buffer_allocate(&result.vb, context->devices.logical, context->devices.physical, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   //result.vb = vk_buffer_create_and_bind(context->devices.logical, vb_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, context->devices.physical, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   result.mb = vk_buffer_create_and_bind(context->devices.logical, mb_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, context->devices.physical, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   result.ib = vk_buffer_create_and_bind(context->devices.logical, ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, context->devices.physical, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   // temp buffer
   size scratch_buffer_size = max(result.mb.size, max(result.vb.size, result.ib.size));
   vk_buffer scratch_buffer = vk_buffer_create_and_bind(context->devices.logical, scratch_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, context->devices.physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

   // upload vertex data
   vk_buffer_upload(context, result.vb, scratch_buffer, vb_data.data, result.vb.size);
   // upload index data
   vk_buffer_upload(context, result.ib, scratch_buffer, ib_data, result.ib.size);
   // upload meshlet data
   vk_buffer_upload(context, result.mb, scratch_buffer, mb.meshlets.data, result.mb.size);

   vk_mesh_draw md = {0};
   md.index_count = index_count;
   array_add(context->geometry.mesh_draws, md);

   vk_mesh_instance mi = {0};
   mi.mesh_index = 0;   // single mesh
   array_add(context->geometry.mesh_instances, mi);

   vk_buffer_destroy(context->devices.logical, &scratch_buffer);

   return result;
}
#endif

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

static bool texture_descriptor_create(vk_context* context, u32 max_descriptor_count)
{
   arena scratch = *context->storage;

   // descriptor_count image samplers
   VkDescriptorPoolSize pool_size =
   {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = max_descriptor_count
   };

   // one set of image samplers
   VkDescriptorPoolCreateInfo pool_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
       .maxSets = 1,
       .poolSizeCount = 1,
       .pPoolSizes = &pool_size
   };

   VkDescriptorPool descriptor_pool = 0;
   if(!vk_valid(vkCreateDescriptorPool(context->devices.logical, &pool_info, 0, &descriptor_pool)))
      return false;

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
   if(!vk_valid(vkCreateSampler(context->devices.logical, &sampler_info, 0, &immutable_sampler)))
      return false;

   // variable and partially bound descriptor arrays
   VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

   VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
       .bindingCount = 1,
       .pBindingFlags = &binding_flags
   };

   // descriptor layout binding with descriptor_count image samplers
   VkDescriptorSetLayoutBinding binding =
   {
       .binding = 0,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = max_descriptor_count,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
       .pImmutableSamplers = 0
   };

   // descriptor layout with one image sampler binding
   VkDescriptorSetLayoutCreateInfo layout_info =
   {
       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
       .pNext = &binding_flags_info,
       .bindingCount = 1,
       .pBindings = &binding,
   };

   VkDescriptorSetLayout descriptor_set_layout = 0;
   if (!vk_valid(vkCreateDescriptorSetLayout(context->devices.logical, &layout_info, 0, &descriptor_set_layout)))
      return false;

	if(context->textures.count > 0)
   {
		VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info =
		{
			 .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
			 .descriptorSetCount = 1,
			 .pDescriptorCounts = &(u32)context->textures.count, // actual allocate count
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
      if(!vk_valid(vkAllocateDescriptorSets(context->devices.logical, &alloc_info, &descriptor_set)))
         return false;

		array(VkDescriptorImageInfo) image_infos = {&scratch};
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

		context->texture_descriptor.set = descriptor_set;
		context->texture_descriptor.layout = descriptor_set_layout;
		context->texture_descriptor.descriptor_pool = descriptor_pool;

      vkUpdateDescriptorSets(context->devices.logical, 1, &write, 0, 0);
      vkDestroySampler(context->devices.logical, immutable_sampler, 0);
	}

   return true;
}

static bool gltf_load_data(cgltf_data** data, s8 gltf_path)
{
   cgltf_options options = {0};
   cgltf_result gltf_result = cgltf_parse_file(&options, s8_data(gltf_path), data);

   if (gltf_result != cgltf_result_success)
      return false;

   gltf_result = cgltf_load_buffers(&options, *data, s8_data(gltf_path));
   if(gltf_result != cgltf_result_success)
   {
      cgltf_free(*data);
      return false;
   }

   gltf_result = cgltf_validate(*data);
   if(gltf_result != cgltf_result_success)
   {
      cgltf_free(*data);
      return false;
   }

   return true;
}

static bool gltf_load_mesh(vk_context* context, cgltf_data* data, s8 gltf_path)
{
   arena* a = context->storage;

   vk_geometry* geometry = &context->geometry;

   array(vertex) vertices = {a};

   // preallocate indices
   array(u32) indices = {a};
   array_resize(indices, gltf_index_count(data));

   // preallocate meshes
   geometry->mesh_draws.arena = a;
   array_resize(geometry->mesh_draws, data->meshes_count);

   // preallocate instances
   geometry->mesh_instances.arena = a;
   array_resize(geometry->mesh_instances, data->nodes_count);

   // preallocate textures
   context->textures.arena = a;
   array_resize(context->textures, data->textures_count);

   size index_offset = 0;
   size vertex_offset = 0;

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
      usize index_count = cgltf_accessor_unpack_indices(prim->indices, indices.data + indices.count, 4, prim->indices->count);
      indices.count += index_count;

      // add this mesh geometry
      vk_mesh_draw md = {0};
      md.index_count = index_count;
      md.index_offset = index_offset;
      md.vertex_offset = vertex_offset;
      md.vertex_count = vertex_count;

      array_add(geometry->mesh_draws, md);

      index_offset += index_count;
      vertex_offset += vertex_count;
   }

   for(usize i = 0; i < data->nodes_count; ++i)
   {
      cgltf_node* node = data->nodes + i;
      if(!node->mesh)
         continue;

      cgltf_mesh* mesh = node->mesh;

      for(cgltf_size pi = 0; pi < mesh->primitives_count; ++pi)
      {
         cgltf_primitive* prim = &mesh->primitives[pi];
         cgltf_material* material = prim->material;

         mat4 wm = {0};
         cgltf_node_transform_world(node, wm.data);

         vk_mesh_instance mi = {0};
         u32 mesh_index = (u32)cgltf_mesh_index(data, node->mesh);
         // index into the mesh to draw
         mi.mesh_index = mesh_index;
         mi.world = wm;

         cgltf_size albedo_index = material && material->pbr_metallic_roughness.base_color_texture.texture
            ? cgltf_texture_index(data, material->pbr_metallic_roughness.base_color_texture.texture)
            : -1;

         cgltf_size normal_index = material && material->normal_texture.texture
            ? cgltf_texture_index(data, material->normal_texture.texture)
            : -1;

         cgltf_size ao_index = material && material->occlusion_texture.texture
            ? cgltf_texture_index(data, material->occlusion_texture.texture)
            : -1;

         cgltf_size metal_index = material && material->pbr_metallic_roughness.metallic_roughness_texture.texture
            ? cgltf_texture_index(data, material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            : -1;

         cgltf_size emissive_index = material && material->emissive_texture.texture
            ? cgltf_texture_index(data, material->emissive_texture.texture)
            : -1;

         mi.albedo = (u32)albedo_index;
         mi.normal = (u32)normal_index;
         mi.metal = (u32)metal_index;
         mi.ao = (u32)ao_index;
         mi.emissive = (u32)emissive_index;

         array_add(geometry->mesh_instances, mi);
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

   size max_vertex_count = 0;
   const size mesh_draws_count = 0;
   for(size i = 0; i < mesh_draws_count; ++i)
   {
      size vertex_count = geometry->mesh_draws.data[i].vertex_count;
      if(vertex_count > max_vertex_count)
         max_vertex_count = vertex_count;
   }

   u8* meshlet_vertices = push(a, u8, max_vertex_count);

   context->meshlet_counts.arena = a;
   array_resize(context->meshlet_counts, mesh_draws_count);

   context->meshlet_offsets.arena = a;
   array_resize(context->meshlet_offsets, mesh_draws_count);

   context->vertex_offsets.arena = a;
   array_resize(context->vertex_offsets, mesh_draws_count);

   context->meshlets.arena = a;
   array_resize(context->meshlets, max_vertex_count);

   size meshlet_offset = 0;
   vertex_offset = 0;

   for(size i = 0; i < mesh_draws_count; ++i)
   {
      // 0xff means the vertex index is not in use yet
      memset(meshlet_vertices, 0xff, max_vertex_count);

      size vertex_count = geometry->mesh_draws.data[i].vertex_count;
      size index_count = geometry->mesh_draws.data[i].index_count;

      array_meshlet meshlets = {a};
      meshlet_build(&meshlets, meshlet_vertices, indices.data, index_count,
                    (u32)geometry->mesh_draws.data[i].index_offset);

      for(size j = 0; j < meshlets.count; ++j)
         array_add(context->meshlets, meshlets.data[j]);

      array_add(context->meshlet_counts, meshlets.count);
      array_add(context->meshlet_offsets, meshlet_offset);
      array_add(context->vertex_offsets, vertex_offset);

      meshlet_offset += meshlets.count;
      vertex_offset += vertex_count;
   }

   usize mb_size = context->meshlets.count * sizeof(meshlet);
   usize vb_size = vertices.count * sizeof(vertex);
   usize ib_size = indices.count * sizeof(u32);

   vk_buffer mb = {.size = mb_size};
   vk_buffer vb = {.size = vb_size};
   vk_buffer ib = {.size = ib_size};

   usize scratch_buffer_size = max(mb.size, max(vb.size, ib.size));
   vk_buffer scratch_buffer = { .size = scratch_buffer_size };

   if(!vk_buffer_create_and_bind(&scratch_buffer, &context->devices, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      return false;

   VkBufferUsageFlagBits buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

   if(context->raytracing_supported)
      buffer_usage_flags |= (VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

   // vertex data
   if(!vk_buffer_create_and_bind(&vb, &context->devices, buffer_usage_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return false;
   vk_buffer_upload(context, &vb, &scratch_buffer, vertices.data, vb.size);
   buffer_hash_insert(&context->buffer_table, vb_buffer_name, vb);

   // meshlet data
   if (!vk_buffer_create_and_bind(&mb, &context->devices, buffer_usage_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return false;
   vk_buffer_upload(context, &mb, &scratch_buffer, context->meshlets.data, mb.size);
   buffer_hash_insert(&context->buffer_table, mb_buffer_name, mb);

   // index data
   buffer_usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
   if (!vk_buffer_create_and_bind(&ib, &context->devices, buffer_usage_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return false;
   vk_buffer_upload(context, &ib, &scratch_buffer, indices.data, ib.size);
   buffer_hash_insert(&context->buffer_table, ib_buffer_name, ib);

   vk_buffer_destroy(&context->devices, &scratch_buffer);

   return true;
}

static bool gltf_load(vk_context* context, s8 gltf_path)
{
   cgltf_data* data = 0;

   if(!gltf_load_data(&data, gltf_path))
   {
      printf("Could not load gltf: %s\n", s8_data(gltf_path));
      return false;
   }

   assert(data);

   if(!gltf_load_mesh(context, data, gltf_path))
   {
      printf("Could not load mesh in gltf: %s\n", s8_data(gltf_path));
      cgltf_free(data);
      return false;
   }

   cgltf_free(data);

   return true;
}
