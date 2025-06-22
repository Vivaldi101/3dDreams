#if !defined(_MESH_H)
#define _MESH_H

struct vertex
{
   float vx, vy, vz;   // pos
   uint8_t nx, ny, nz;   // normal
   float tu, tv;       // texture
};

struct meshlet
{
   uint32_t vertex_index_buffer[64];  // unique indices into the mesh vertex buffer
   uint8_t primitive_indices[127*3];
   uint8_t triangle_count;
   uint8_t vertex_count;
};

// TODO: add the transforms

#endif
