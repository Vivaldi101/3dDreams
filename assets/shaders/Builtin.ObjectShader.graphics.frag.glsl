#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) out vec4 out_color;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world_frag_pos;
layout(location = 2) in vec2 in_uv;
layout(location = 3) flat in uint textureID;

layout(set = 1, binding = 0)
uniform sampler2D textures[];

#if 0
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
#endif

void main()
{
   float ndot = dot(in_normal, normalize(vec3(-1.0, 1.0, 1.0)));
   out_color = vec4(ndot/4.f, ndot/2.f, ndot/1.f, 1.f);

   out_color = texture(textures[0], in_uv);

   //vec4 texSample = texture(textures[0], in_uv);
   //out_color = result;
   //vec4 texcolor = texture(textures[0], in_uv);
   //float alpha = 0.1; // 50% translucent
   //out_color = vec4(texcolor.rgb, alpha);
}
