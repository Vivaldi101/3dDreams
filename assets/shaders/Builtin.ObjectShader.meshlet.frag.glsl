#version 460

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_nonuniform_qualifier : require

#include "mesh.h"
#include "common.glsl"

#if RAYTRACE
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 3) uniform accelerationStructureEXT tlas;
#endif

layout(location = 0) out vec4 out_color;

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_wp;
layout(location = 3) in vec3 in_normal;
layout(location = 4) flat in uint in_draw_ID;

layout(set = 1, binding = 0)
uniform sampler2D textures[];

layout(set = 0, binding = 2) readonly buffer mesh_draw_block
{
   mesh_draw draws[];
};

void main()
{
#if DEBUG
   out_color = in_color;
#else
    mesh_draw draw = draws[in_draw_ID];
    vec3 light_color = vec3(1.f);
    float ambient = 0.f;

   vec4 albedo = vec4(.5, .5, .5, 1);
   vec3 emissive = vec3(0.0);

   if(draw.albedo != -1)
      albedo = texture(textures[draw.albedo], in_uv).rgba;
   
   if(draw.emissive != -1)
      emissive = texture(textures[draw.emissive], in_uv).rgb;

   //float diffuse_factor = max(dot(normalize(in_normal), normalize(vec3(1, 1, 0))), 0.0);
   //vec3 diffuse = diffuse_factor * light_color;

   vec3 sun_dir = normalize(vec3(1, 1, 1));
   vec3 N = normalize(in_normal);
   
   // Small offset to avoid self-intersection
   float epsilon = 0.0005;
   vec3 ray_origin = in_wp + N * epsilon;
   
   // Lambertian term
   float ndotl = max(dot(N, sun_dir), 0.0);
   
   // Initialize and cast the shadow ray
   rayQueryEXT rq;
   rayQueryInitializeEXT(
       rq,
       tlas,
       gl_RayFlagsTerminateOnFirstHitEXT, // stop at first hit
       0xff,                               // instance mask (all)
       ray_origin,                         // ray origin with offset
       epsilon,                             // tMin matching offset
       sun_dir,                             // ray direction
       100.0                               // tMax
   );
   rayQueryProceedEXT(rq);
   
   // Visibility factor: 1 = fully lit, 0.1 = in shadow
   bool hit = (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT);
   float visibility = hit ? 0.1 : 1.0;
   
   // Apply Lambertian lighting and shadow
   //vec3 color = albedo * ndotl * visibility + 0.05; // small ambient
   vec3 color = (albedo.rgb * ndotl * visibility + emissive);

   if(albedo.a < 0.5) discard;
   
   out_color = vec4(color, albedo.a);
#endif
}
