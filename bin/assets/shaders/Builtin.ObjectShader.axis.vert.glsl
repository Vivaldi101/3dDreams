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

layout(location = 0) out vec3 axis_color;

mat4 translate(vec3 t)
{
    return mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        t.x, t.y, t.z, 1.0
    );
}

void main() {
   
    const int verts_count = 18;
    const float arrow_head_scale = 0.10;
    const float axis_scale = 1.0;
    const float axis_down_scale = axis_scale - 0.2;
vec3 positions[verts_count] = vec3[](
    // X axis
    vec3(0, 0, 0), vec3(axis_scale, 0, 0),              // Main axis
    vec3(axis_scale, 0, 0), vec3(axis_down_scale,  arrow_head_scale, 0.0),
    vec3(axis_scale, 0, 0), vec3(axis_down_scale, -arrow_head_scale, 0.0),

    // Y axis
    vec3(0, 0, 0), vec3(0, axis_scale, 0),              // Main axis
    vec3(0, axis_scale, 0), vec3(arrow_head_scale, axis_down_scale, 0.0),
    vec3(0, axis_scale, 0), vec3(-arrow_head_scale, axis_down_scale, 0.0),

    // Z axis
    vec3(0, 0, 0), vec3(0, 0, axis_scale),              // Main axis
    vec3(0, 0, axis_scale), vec3(0.0, arrow_head_scale, axis_down_scale),
    vec3(0, 0, axis_scale), vec3(0.0, -arrow_head_scale, axis_down_scale) 
);

vec3 colors[verts_count] = vec3[](
    // X axis (red)
    vec3(1, 0, 0), vec3(1, 0, 0),
    vec3(1, 0, 0), vec3(1, 0, 0),
    vec3(1, 0, 0), vec3(1, 0, 0),

    // Y axis (green)
    vec3(0, 1, 0), vec3(0, 1, 0),
    vec3(0, 1, 0), vec3(0, 1, 0),
    vec3(0, 1, 0), vec3(0, 1, 0),

    // Z axis (blue)
    vec3(0, 0, 1), vec3(0, 0, 1),
    vec3(0, 0, 1), vec3(0, 0, 1),
    vec3(0, 0, 1), vec3(0, 0, 1)
);
    mat4 t = translate(vec3(-0.42, 5.0, 0.0));
    gl_Position = transform.projection * transform.view * t * vec4(positions[gl_VertexIndex], 1.0);

    axis_color = colors[gl_VertexIndex];
}
