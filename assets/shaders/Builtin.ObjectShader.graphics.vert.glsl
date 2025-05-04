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

   out_color = vec4(vec3(v.nx*1.0, v.ny*1.0, v.nz*1.0) * 0.90, 1.0);
}
