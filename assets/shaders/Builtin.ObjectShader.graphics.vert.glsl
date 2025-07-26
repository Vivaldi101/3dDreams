#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require

#include "mesh.h"

layout(push_constant) uniform push_constants_uniform
{
    mat4 projection;
    mat4 view;
    mat4 model;
   float near;
   float far;
   float ar;
   uint meshlet_offset;
} push_constants;

layout(set = 0, binding = 0) readonly buffer Verts
{
   vertex verts[];
};

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_world_frag_pos;
layout(location = 2) out vec2 out_frag_uv;

void main()
{
   vertex v = verts[gl_VertexIndex];

    vec3 world_pos = vec3(v.vx, v.vy, v.vz);
    vec4 world_position = push_constants.model * vec4(world_pos, 1.0);
    gl_Position = push_constants.projection * push_constants.view * world_position;

    // Decode normal and transform to world space using inverse transpose
    vec3 normal = (vec3(v.nx, v.ny, v.nz) - 127.5) / 127.5;
    mat3 normal_matrix = transpose(inverse(mat3(push_constants.model)));
    vec3 world_normal = normalize(normal_matrix * normal);

    out_normal = world_normal;
    out_world_frag_pos = world_position.xyz;
    out_frag_uv = world_pos.xy * 0.5 + 0.5;
}
