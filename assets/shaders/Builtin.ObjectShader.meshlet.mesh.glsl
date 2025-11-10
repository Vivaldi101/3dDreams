#version 450

#extension GL_EXT_mesh_shader : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_ARB_shading_language_include: require
#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive: require

#include "mesh.h"
#include "common.glsl"

// number of threads inside the work group
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 127) out;

const uint max_vertices = 64;
const uint max_primitives = 127;

vec3 quad[4] = vec3[]
(
    vec3(-100.0f, -1.25f, -100.0f),  // top-left
    vec3(-100.0f, -1.25f,  100.0f),  // bottom-left
    vec3(100.0f,  -1.25f,   -100.0f), // top-right
    vec3(100.0f,  -1.25f,   100.0f)   // bottom-right
);

layout(set = 0, binding = 0) readonly buffer vertex_block
{
   vertex verts[];
};

layout(set = 0, binding = 1) readonly buffer meshlet_block
{
   meshlet meshlets[];
};

layout(set = 0, binding = 2) readonly buffer mesh_draw_block
{
   mesh_draw draws[];
};

#if RAYTRACE
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
#endif

layout(location = 0) out vec4 out_color[];
layout(location = 1) out vec2 out_uv[];

vec3 renormalize_normal(vec3 n)
{
   //float a = 2.f/255.f;
   float a = 255.f/2.f;
   float b = -1.f;

   return n/a + b;
}

uint32_t hash_index(uint32_t a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);

   return a;
}

void main()
{
    int draw_ID = gl_DrawIDARB;

    uint mi = draws[draw_ID].mesh_offset + gl_WorkGroupID.x;    // global meshlet index
    uint ti = gl_LocalInvocationID.x;     // thread index

    uint vertex_count = meshlets[mi].vertex_count;
    uint triangle_count = meshlets[mi].triangle_count;

#if DEBUG
    uint h = hash_index(mi);
    vec3 meshlet_color = vec3(float(h & 255), float((h >> 8) & 255), float((h >> 16) & 255)) / 255.f;
#endif

    // write output counts once
    if(ti == 0)
      SetMeshOutputsEXT(vertex_count, triangle_count);

    for(uint i = ti; i < vertex_count; i += 64)
    {
      uint vi = meshlets[mi].vertex_index_buffer[i];

      uint offset = draws[draw_ID].vertex_offset;
      vertex v = verts[offset + vi];
      vec4 vo = globals.projection * globals.view * draws[draw_ID].world * vec4(vec3(v.vx, v.vy, v.vz), 1.0f);

      gl_MeshVerticesEXT[i].gl_Position = vo;

      //vec3 normal = (vec3(v.nx, v.ny, v.nz) - 127.5) / 127.5;
      vec3 normal = renormalize_normal(vec3(v.nx, v.ny, v.nz));

#if DEBUG
      out_color[i] = vec4(meshlet_color, 1.0);
#else
      out_color[i] = vec4(vec3(normal), 1.0);
#endif
      vec2 texcoord = vec2(v.tu, v.tv);
      out_uv[i] = texcoord;
    }

    for(uint i = ti; i < triangle_count; i += 64)
    {
      // one triangle - 3 primitive indices
      uint i0 = meshlets[mi].primitive_indices[3*i + 0];
      uint i1 = meshlets[mi].primitive_indices[3*i + 1];
      uint i2 = meshlets[mi].primitive_indices[3*i + 2];

      gl_PrimitiveTriangleIndicesEXT[i] = uvec3(i0, i1, i2);
    }
}
