#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_nonuniform_qualifier : require

#include "mesh.h"

layout(push_constant) uniform block
{
    mat4 projection;
    mat4 view;
   float near;
   float far;
   float ar;
   uint meshlet_offset;
   bool is_procedural;
} globals;

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
    vec3 light_color = vec3(1.f);
    float ambient = .3f;
    if(!globals.is_procedural)
    {
       mesh_draw draw = draws[in_draw_ID];
   
       vec4 albedo = vec4(1, 0, 1, 1);
       if(draw.albedo != -1)
         albedo = vec4(texture(textures[draw.albedo], in_uv).rgb, 1.f);
   
       vec4 nmap = vec4(0, 0, 1, 0);
       if(draw.normal != -1)
         nmap = texture(textures[draw.normal], in_uv);
   
       float diffuse_factor = max(dot(normalize(in_normal), vec3(0, 0, 1)), 0.0);
       vec3 diffuse = diffuse_factor * light_color;
       out_color = vec4(albedo.rgb * (diffuse + ambient), 1.0); // keep opaque
    }
    else
    {
         vec3 procedural_center = vec3(0.0, 0.0, 0.0); // center of quad in world space
         float procedural_radius = 3.3;                // size of glow
         float glow_intensity = 2.0;                   // HDR multiplier
         float dist = length(in_world_frag_pos - procedural_center);
         float falloff = 1.0 - smoothstep(0.0, procedural_radius, dist);
         vec3 glow_color = vec3(0.0, 0.0, 0.85) * falloff * glow_intensity;
         vec3 base_color = vec3(0, 0, 0);
         vec3 final_color = clamp(base_color + glow_color, 0.0, 1.0);
         out_color = vec4(final_color, 1.0);
    }
}
