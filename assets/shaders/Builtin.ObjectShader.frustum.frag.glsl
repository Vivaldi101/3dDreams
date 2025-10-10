#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "common.glsl"

layout(location = 0) out vec4 out_color;

float ndc_to_linear_z(float ndc_z, float near, float far)
{
   float u = far * near;
   float l = (near - far)*ndc_z + far;

   float linear_z = u / l;

   return linear_z;
}

void main()
{
    float linear_z = ndc_to_linear_z(gl_FragCoord.z, globals.near, globals.far);

    out_color = vec4(0.125);
}
