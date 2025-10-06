#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_ARB_shader_draw_parameters : require

#include "mesh.h"
#include "common.glsl"

vec3 quad[4] = vec3[]
(
    vec3(-30.0f, 0.0f, -30.0f),  // top-left
    vec3(-30.0f, 0.0f,  30.0f),  // bottom-left
    vec3(30.0f, 0.0f,   -30.0f), // top-right
    vec3(30.0f, 0.0f,   30.0f)   // bottom-right
);

layout(set = 0, binding = 0) readonly buffer vertex_block
{
   vertex verts[];
};

layout(set = 0, binding = 1) readonly buffer mesh_draw_block
{
   mesh_draw draws[];
};

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_world_frag_pos;
layout(location = 2) out vec2 out_uv;
layout(location = 3) flat out uint out_draw_ID;

void main()
{
    int draw_ID = gl_DrawIDARB;

    vertex v;
    vec3 p = vec3(0.f);
    vec3 n = vec3(0.f);
    vec2 t = vec2(0.f);

    vec4 world_pos = vec4(0.f);

    if(!globals.draw_ground_plane)
    {
        p = vec3(verts[gl_VertexIndex].vx, verts[gl_VertexIndex].vy, verts[gl_VertexIndex].vz) * 1.f;
        n = vec3(verts[gl_VertexIndex].nx, verts[gl_VertexIndex].ny, verts[gl_VertexIndex].nz);
        t = vec2(verts[gl_VertexIndex].tu, verts[gl_VertexIndex].tv);

        world_pos = draws[draw_ID].world * vec4(p, 1.0);
    }
    else
    {
        p = quad[gl_VertexIndex];
        world_pos = vec4(p, 1.0);
    }

    gl_Position = globals.projection * globals.view * world_pos;

    // Decode normal and transform to world space using inverse transpose
    vec3 normal = (n - 127.5) / 127.5;
    mat3 normal_matrix = transpose(inverse(mat3(draws[draw_ID].world)));
    vec3 world_normal = normalize(normal_matrix * normal);
    vec2 texcoord = t;

    out_normal = world_normal;
    out_world_frag_pos = world_pos.xyz;
    out_uv = texcoord;
    out_draw_ID = draw_ID;
}
