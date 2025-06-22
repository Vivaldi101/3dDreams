#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "../extern/tinyobjloader-c/tinyobj_loader_c.h"

#include "../assets/shaders/mesh.h"

typedef struct 
{
   arena scratch;
} obj_user_ctx;

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

static mesh meshlet_build(arena scratch, arena* storage, u32 vertex_count, u32* index_buffer, u32 index_count)
{
   mesh result = {};

   struct meshlet ml = {};

   u8* meshlet_vertices = push(&scratch, u8, vertex_count);
   result.meshlet_buffer.arena = scratch;

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
         *push_array(&result.meshlet_buffer, struct meshlet) = ml;

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
      *push_array(&result.meshlet_buffer, struct meshlet) = ml;

   return result;
}

static void obj_load(vk_context* context, arena scratch, tinyobj_attrib_t* attrib, vk_buffer scratch_buffer)
{
   index_hash_table obj_table = {};

   // only triangles allowed
   assert(attrib->num_face_num_verts * 3 == attrib->num_faces);

   const usize index_count = attrib->num_faces;

   assert((size)index_count*sizeof(u32) <= (u32)~0u);

   obj_table.max_count = index_count;

   obj_table.keys = push(&scratch, hash_key, obj_table.max_count);
   obj_table.values = push(&scratch, hash_value, obj_table.max_count);

   memset(obj_table.keys, -1, sizeof(hash_key) * obj_table.max_count);

   u32 vertex_index = 0;
   u32 primitive_index = 0;

   u32* ib_data = push(&scratch, u32, index_count);
   scratch_array vb_data = {};
   vb_data.arena = scratch;

   for(usize f = 0; f < index_count; f += 3)
   {
      const tinyobj_vertex_index_t* vidx = attrib->faces + f;

      for(usize i = 0; i < 3; ++i)
      {
         i32 vi = vidx[i].v_idx;
         i32 vti = vidx[i].vt_idx;
         i32 vni = vidx[i].vn_idx;

         hash_key index = (hash_key){.vi = vi, .vni = vni, .vti = vti};
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
            *push_array(&vb_data, struct vertex) = v;
         }
         else
            ib_data[primitive_index] = lookup;

         ++primitive_index;
      }
   }

   context->index_count = (u32)index_count;

   usize vb_size = vertex_index * sizeof(struct vertex);
   vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->vb,
      scratch_buffer, vb_data.data, vb_size);

   mesh obj_mesh = meshlet_build(scratch, context->storage, (u32)obj_table.count, ib_data, (u32)index_count);
   context->meshlet_count = (u32)obj_mesh.meshlet_buffer.count;
   context->meshlet_buffer = obj_mesh.meshlet_buffer.data;

   vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->mb,
      scratch_buffer, context->meshlet_buffer, context->meshlet_count * sizeof(struct meshlet));

   usize ib_size = index_count * sizeof(u32);
   vk_buffer_upload(context->logical_device, context->graphics_queue, context->command_buffer, context->command_pool, context->ib,
      scratch_buffer, ib_data, ib_size);
}
