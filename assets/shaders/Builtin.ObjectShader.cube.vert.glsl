#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform Transform
{
    mat4 projection;
    mat4 view;
    mat4 model;
   float near;
   float far;
   float ar;
} transform;

const vec3 verts[] = 
{
   vec3(0.0, -1.0f, 1.0f),
   vec3(1.0f, 1.0f, 1.0f),
   vec3(-1.0f, 1.0f, 1.0f),
};

vec3 cube_strip[] = {
    vec3(-1.0f, -1.0f, +1.0f),
    vec3(+1.0f, -1.0f, +1.0f),
    vec3(-1.0f, +1.0f, +1.0f),
    vec3(+1.0f, +1.0f, +1.0f),
    vec3(+1.0f, -1.0f, -1.0f),
    vec3(-1.0f, -1.0f, -1.0f),
    vec3(+1.0f, +1.0f, -1.0f),
    vec3(-1.0f, +1.0f, -1.0f),
    vec3(+1.0f, -1.0f, +1.0f),
    vec3(+1.0f, -1.0f, -1.0f),
    vec3(+1.0f, +1.0f, +1.0f),
    vec3(+1.0f, +1.0f, -1.0f),
    vec3(-1.0f, -1.0f, -1.0f),
    vec3(-1.0f, -1.0f, +1.0f),
    vec3(-1.0f, +1.0f, -1.0f),
    vec3(-1.0f, +1.0f, +1.0f),
    vec3(-1.0f, +1.0f, +1.0f),
    vec3(+1.0f, +1.0f, +1.0f),
    vec3(-1.0f, +1.0f, -1.0f),
    vec3(+1.0f, +1.0f, -1.0f),
    vec3(-1.0f, -1.0f, -1.0f),
    vec3(+1.0f, -1.0f, -1.0f),
    vec3(-1.0f, -1.0f, +1.0f),
    vec3(+1.0f, -1.0f, +1.0f) 
};

// Teapot vertices (simplified version)
vec3 vertices[23] = {
    // Body vertices
    vec3( 1.0,  0.0,  0.0),
    vec3( 0.707,  0.707,  0.0),
    vec3( 0.0,  1.0,  0.0),
    vec3( -0.707,  0.707,  0.0),
    vec3( -1.0,  0.0,  0.0),
    vec3( -0.707, -0.707,  0.0),
    vec3( 0.0, -1.0,  0.0),
    vec3( 0.707, -0.707,  0.0),

    // Handle vertices (simplified)
    vec3( 0.25,  0.25,  0.5),
    vec3( -0.25,  0.25,  0.5),
    vec3( -0.5,  0.5,  0.25),
    vec3( 0.0,  0.0,  0.5),
    vec3( 0.5,  0.5,  0.25),
    vec3( 0.25, -0.25,  0.5),
    vec3( -0.25, -0.25,  0.5),
    vec3( 0.5, -0.5,  0.25),
    vec3( -0.5, -0.5,  0.25),

    // Spout vertices (simplified)
    vec3( 0.0,  0.0,  0.75),
    vec3( 0.0,  0.5,  0.75),
    vec3( 0.0,  0.75,  0.75),
    vec3( 0.25,  0.75,  0.75),
    vec3( 0.5,  0.5,  0.75),
    vec3( 0.5,  0.25,  0.75)
};

const vec3 foo[5] = {
    vec3( 0.0,  1.0,  0.0),   // Apex (top)
    vec3(-0.5, -0.5,  0.5),   // Base front-left
    vec3( 0.5, -0.5,  0.5),   // Base front-right
    vec3( 0.5, -0.5, -0.5),   // Base back-right
    vec3(-0.5, -0.5, -0.5)    // Base back-left
};

layout(location = 0) out vec4 out_color;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

void main()
{
   gl_Position = transform.projection * transform.view * transform.model * vec4(in_pos, 1.0f);

   out_color = vec4(in_normal * 0.5 + vec3(0.5), 1.0);
}