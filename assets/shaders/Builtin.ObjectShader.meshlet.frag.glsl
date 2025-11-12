#version 460
#extension GL_ARB_separate_shader_objects : enable

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

layout(set = 1, binding = 0)
uniform sampler2D textures[];

void main()
{
   vec3 sun_dir = normalize(vec3(1, 1, 0));
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
   
   // Sample albedo
   vec3 albedo = texture(textures[0], in_uv).rgb;
   
   // Apply Lambertian lighting and shadow
   vec3 color = albedo * ndotl * visibility + 0.05; // small ambient
   
   out_color = vec4(color, 1.0);
}
