#version 460

#extension GL_EXT_mesh_shader : require
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

// number of threads inside the work group
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 42) out;

const uint max_vertices = 64;
const uint max_primitives = 42;

struct Vertex
{
   float vx, vy, vz;   // pos
   float nx, ny, nz;   // normal TODO: Use 8 bit normals in future
   float tu, tv;       // texture
};

struct Meshlet
{
   uint32_t vertex_index_buffer[64];  // unique indices into the mesh vertex buffer
   uint8_t primitive_indices[126];    // 42 triangles (primitives)
   uint8_t triangle_count;
   uint8_t vertex_count;
   uint8_t index_count;
};

layout(set = 0, binding = 0) readonly buffer Verts
{
   Vertex verts[];
};

layout(set = 0, binding = 1) readonly buffer Meshlets
{
   Meshlet meshlets[];
};

layout(location = 0) out vec4 out_color[];

void main()
{
    uint mi = gl_WorkGroupID.x;
    if(mi >= meshlets.length())
    {
      return;
    }
    if(meshlets[mi].vertex_count > max_vertices)
    {
      return;
    }
    if(meshlets[mi].triangle_count > max_primitives)
    {
      return;
    }

    SetMeshOutputsEXT(meshlets[mi].vertex_count, meshlets[mi].triangle_count);

    for(uint i = 0; i < meshlets[mi].vertex_count; ++i)
    {
      uint vi = meshlets[mi].vertex_index_buffer[i];

      Vertex v = verts[vi];
      vec4 vo = transform.projection * transform.view * transform.model * vec4(vec3(v.vx, v.vy, v.vz), 1.0f);
      //vec4 vo = vec4(vec3(v.vx, v.vy, v.vz) + vec3(0, 0, 0.5), 1.0f);

      gl_MeshVerticesEXT[i].gl_Position = vo;

      out_color[i] = vec4(vec3(v.nx*1.0, v.ny*1.0, v.nz*1.0) * 0.90, 1.0);
    }

    for(uint i = 0; i < meshlets[mi].triangle_count; ++i)
    {
      uint i0 = meshlets[mi].primitive_indices[3*i + 0];
      uint i1 = meshlets[mi].primitive_indices[3*i + 1];
      uint i2 = meshlets[mi].primitive_indices[3*i + 2];

      gl_PrimitiveTriangleIndicesEXT[i] = uvec3(i0, i1, i2);
    }

}
