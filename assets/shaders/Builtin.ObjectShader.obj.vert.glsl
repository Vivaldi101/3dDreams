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

struct Vertex
{
   float vx, vy, vz;   // pos
   float nx, ny, nz;   // normal TODO: Use 8 bit normals in future
   float tu, tv;       // texture
};

layout(set = 0, binding = 0) readonly buffer Verts
{
   Vertex verts[];
};

layout(location = 0) out vec4 out_color;

void main()
{
   Vertex v = verts[gl_VertexIndex];

   gl_Position = transform.projection * transform.view * transform.model * vec4(vec3(v.vx, v.vy, v.vz), 1.0f);

   out_color = vec4(vec3(v.nx, v.ny, v.nz) * 0.75, 1.0);
}