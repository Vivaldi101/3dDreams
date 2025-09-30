#if !defined(_COMMON_H)
#define _COMMON_H

layout(push_constant) uniform push_constants_block
{
    mat4 projection;
    mat4 view;
    float near;
    float far;
    float ar;
    uint meshlet_offset;
    bool draw_ground_plane;
} globals;

#endif
