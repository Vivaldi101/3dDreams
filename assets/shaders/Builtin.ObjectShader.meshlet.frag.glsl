#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 out_color;
layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_uv;

layout(set = 1, binding = 0)
uniform sampler2D textures[];

void main()
{
   //out_color = texture(textures[0], in_uv);
   out_color = vec4(1,0,0,1);
}
