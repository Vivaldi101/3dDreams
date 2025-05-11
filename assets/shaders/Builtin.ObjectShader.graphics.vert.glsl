#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require

layout(push_constant) uniform Transform
{
    mat4 projection;
    mat4 view;
    mat4 model;
   float near;
   float far;
   float ar;
} transform;

struct Vertex
{
   float vx, vy, vz;   // pos
   uint8_t nx, ny, nz;   // normal
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

   vec3 normal = (vec3(v.nx, v.ny, v.nz) - 127.5) / 127.5;

   out_color = vec4(vec3(normal), 1.0);
}
