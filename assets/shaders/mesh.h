#if !defined(_MESH_H)
#define _MESH_H

#include "../../code/math.h"

struct vertex
{
   float vx, vy, vz;    // pos
   uint8_t nx, ny, nz;  // normal
   float tu, tv;        // texture
};

struct meshlet
{
   uint32_t vertex_index_buffer[64];  // unique indices into the mesh vertex buffer
   uint8_t primitive_indices[127*3];
   uint8_t triangle_count;
   uint8_t vertex_count;
};

struct mesh_draw
{
   uint32_t albedo;      // indices into texture descriptors
   uint32_t normal;
   uint32_t metal;
   uint32_t emissive;
   uint32_t ao;
   mat4 world;           // world transform
};

#endif
