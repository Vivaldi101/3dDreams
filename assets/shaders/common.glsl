#if !defined(_COMMON_H)
#define _COMMON_H

layout(push_constant) uniform push_constants_block
{
    mat4 projection;
    mat4 view;
    float near;
    float far;
    float ar;
    bool draw_ground_plane;
} globals;

#define RAYTRACE 0
#define DEBUG 0

#endif
