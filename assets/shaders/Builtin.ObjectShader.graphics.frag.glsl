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
vec3 area_light_center = vec3(0.0, 2.0, 1.0);
vec2 area_size = vec2(1.0, 0.5);
vec3 area_light_color = vec3(1.3, 1.1, 0.9);  // warm white

vec3 normal = normalize(in_normal);
vec3 frag_pos = in_world_frag_pos;
// TODO: pass the view pos
vec3 view_pos = vec3(0.0, 0.0, -1.0);
vec3 view_dir = normalize(view_pos - frag_pos);

// Warm bronze-like base
vec3 ambient = vec3(0.25, 0.15, 0.05);

// Bronze-like diffuse
vec3 diffuse_bronze = vec3(0.5, 0.3, 0.1);
vec3 total_diffuse = vec3(0.0);

// Shiny bronze specular
vec3 specular_bronze = vec3(1.2, 0.8, 0.4);
vec3 total_specular = vec3(0.0);

float shininess = 192.0;
float fresnel_strength = 0.4;
float fresnel = pow(1.0 - max(dot(normal, view_dir), 0.0), 5.0) * fresnel_strength;

for (int x = -1; x <= 1; x += 2) {
    for (int y = -1; y <= 1; y += 2) {
        vec3 offset = vec3(x * area_size.x * 0.5, 0.0, y * area_size.y * 0.5);
        vec3 sample_pos = area_light_center + offset;

        vec3 light_dir = normalize(sample_pos - frag_pos);
        
        float diff = max(dot(normal, light_dir), 0.0);
        total_diffuse += diff * diffuse_bronze * area_light_color;

        vec3 halfway_dir = normalize(light_dir + view_dir);
        float spec = pow(max(dot(normal, halfway_dir), 0.0), shininess);
        total_specular += spec * specular_bronze * area_light_color;
    }
}

total_diffuse /= 4.0;
total_specular /= 4.0;

vec3 dragon_color = ambient + total_diffuse + total_specular + fresnel * specular_bronze;

out_color = vec4(dragon_color, 1.0);

}
