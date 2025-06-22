#version 460

#extension GL_EXT_mesh_shader : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require

#define DEBUG 1

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
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 127) out;

const uint max_vertices = 64;
const uint max_primitives = 127;

struct Vertex
{
   float vx, vy, vz;   // pos
   uint8_t nx, ny, nz;   // normal
   float tu, tv;       // texture
};

struct Meshlet
{
   uint32_t vertex_index_buffer[64];  // unique indices into the mesh vertex buffer
   uint8_t primitive_indices[127*3];
   uint8_t triangle_count;
   uint8_t vertex_count;
};

layout(set = 0, binding = 1) readonly buffer Meshlets
{
   Meshlet meshlets[];
};

layout(set = 0, binding = 0) readonly buffer Verts
{
   Vertex verts[];
};

layout(location = 0) out vec4 out_color[];

vec3 renormalize_normal(vec3 n)
{
   //float a = 2.f/255.f;
   float a = 255.f/2.f;
   float b = -1.f;

   return n/a + b;
}

uint32_t hash_index(uint32_t a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);

   return a;
}

void main()
{
    uint mi = gl_WorkGroupID.x;
    uint ti = gl_LocalInvocationID.x;

    uint vertex_count = meshlets[mi].vertex_count;
    uint triangle_count = meshlets[mi].triangle_count;

#if DEBUG
    uint h = hash_index(mi);
    vec3 meshlet_color = vec3(float(h & 255), float((h >> 8) & 255), float((h >> 16) & 255)) / 255.f;
#endif

    // write output counts once
    if(ti == 0)
      SetMeshOutputsEXT(vertex_count, triangle_count);

    for(uint i = ti; i < vertex_count; i += 64)
    {
      uint vi = meshlets[mi].vertex_index_buffer[i];

      Vertex v = verts[vi];
      vec4 vo = transform.projection * transform.view * transform.model * vec4(vec3(v.vx, v.vy, v.vz), 1.0f);

      gl_MeshVerticesEXT[i].gl_Position = vo;

      //vec3 normal = (vec3(v.nx, v.ny, v.nz) - 127.5) / 127.5;
      vec3 normal = renormalize_normal(vec3(v.nx, v.ny, v.nz));

#if DEBUG
      out_color[i] = vec4(meshlet_color, 1.0);
#else
      out_color[i] = vec4(vec3(normal), 1.0);
#endif
    }

    for(uint i = ti; i < triangle_count; i += 64)
    {
      // one triangle - 3 primitive indices
      uint i0 = meshlets[mi].primitive_indices[3*i + 0];
      uint i1 = meshlets[mi].primitive_indices[3*i + 1];
      uint i2 = meshlets[mi].primitive_indices[3*i + 2];

      gl_PrimitiveTriangleIndicesEXT[i] = uvec3(i0, i1, i2);
    }
}
