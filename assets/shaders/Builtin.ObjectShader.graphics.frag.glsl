#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 out_color;
layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world_frag_pos;
layout(location = 2) in vec2 in_frag_uv;

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

float ndc_to_linear_z(float ndc_z, float near, float far)
{
   float u = far * near;
   float l = (near - far)*ndc_z + far;

   float linear_z = u / l;

   return linear_z;
}

vec3 hsv_to_rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 0.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main()
{
   vec3 area_light_center = vec3(0.0, 2.0, 1.0);   // Center of the area light
   vec2 area_size = vec2(1.0, 0.5);                // Width and height of the area
   vec3 area_light_color = vec3(1.0, 0.9, 0.7);    // Warm light
   
   vec3 normal = normalize(in_normal);
   vec3 frag_pos = in_world_frag_pos;
   
   float ambient_strength = 0.8;
   vec3 ambient = ambient_strength * vec3(0.6, 0.2, 0.1);
   
   vec3 total_diffuse = vec3(0.0);
   
   // Simulate area light with 4 sample points in a 2x2 grid
   for (int x = -1; x <= 1; x += 2) {
       for (int y = -1; y <= 1; y += 2) {
           vec3 offset = vec3(x * area_size.x * 0.5, 0.0, y * area_size.y * 0.5);
           vec3 sample_pos = area_light_center + offset;
           
           vec3 light_dir = normalize(sample_pos - frag_pos);
           float diff = max(dot(normal, light_dir), 0.0);
           total_diffuse += diff * area_light_color;
       }
   }
   
   total_diffuse /= 4.0;  // Average the contribution
   
   vec3 dragon_color = ambient + total_diffuse;
   
   out_color = vec4(dragon_color, 1.0);
}
