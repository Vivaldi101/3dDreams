#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_ARB_shader_draw_parameters : require

#include "mesh.h"

layout(push_constant) uniform block
{
    mat4 projection;
    mat4 view;
   float near;
   float far;
   float ar;
   uint meshlet_offset;
} globals;

layout(set = 0, binding = 0) readonly buffer vertex_block
{
   vertex verts[];
};

layout(set = 0, binding = 1) readonly buffer transform_block
{
   mat4 worlds[];
};

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_world_frag_pos;
layout(location = 2) out vec2 out_uv;
layout(location = 3) flat out uint out_textureID;  // pass to fragment shader

void main()
{
    int draw_ID = gl_DrawIDARB;
    vertex v = verts[gl_VertexIndex];

    vec3 local_pos = vec3(v.vx, v.vy, v.vz);
    vec4 world_pos = worlds[draw_ID] * vec4(local_pos, 1.0);
    gl_Position = globals.projection * globals.view * world_pos;

    // Decode normal and transform to world space using inverse transpose
    vec3 normal = (vec3(v.nx, v.ny, v.nz) - 127.5) / 127.5;
    mat3 normal_matrix = transpose(inverse(mat3(worlds[draw_ID])));
    vec3 world_normal = normalize(normal_matrix * normal);
    vec2 texcoord = vec2(v.tu, v.tv);

    out_normal = world_normal;
    out_world_frag_pos = world_pos.xyz;
    out_uv = texcoord;
    out_textureID = draw_ID;
}
