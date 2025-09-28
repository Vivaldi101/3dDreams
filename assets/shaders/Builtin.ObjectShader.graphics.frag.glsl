#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_nonuniform_qualifier : require

#include "mesh.h"

layout(location = 0) out vec4 out_color;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world_frag_pos;
layout(location = 2) in vec2 in_uv;
layout(location = 3) flat in uint in_draw_ID;

layout(set = 0, binding = 1) readonly buffer mesh_draw_block
{
   mesh_draw draws[];
};

layout(set = 1, binding = 0)
uniform sampler2D textures[];

float ndc_to_linear_z(float ndc_z, float near, float far)
{
   float u = far * near;
   float l = (near - far)*ndc_z + far;

   float linear_z = u / l;

   return linear_z;
}

void main()
{
    mesh_draw draw = draws[in_draw_ID];

    vec4 albedo = vec4(1, 0, 1, 1);
    if(draw.albedo != -1)
      albedo = vec4(texture(textures[draw.albedo], in_uv).rgb, 1.f);

    vec4 nmap = vec4(0, 0, 1, 0);
    if(draw.normal != -1)
      nmap = texture(textures[draw.normal], in_uv);

    float ambient = .3f;
    float diffuse = max(dot(normalize(in_normal), vec3(0, 0, 1)), 0.0);
    out_color = vec4(albedo.rgb * (diffuse + ambient), 1.0); // keep opaque
}
