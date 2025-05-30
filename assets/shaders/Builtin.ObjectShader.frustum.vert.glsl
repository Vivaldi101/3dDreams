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

void main()
{
   float fov_y = 90.0;
   float fov_half_y = fov_y/2.0;
   float tan = tan(radians(fov_half_y));
   float ar = transform.ar;

   float t = transform.near*tan;
   float r = t * transform.ar;

   if(ar < 1.0f)
   {
      r = t;      // flip the aspect
      t = r / ar;   // re-establish invariant in push aspect
   }

   // near plane
   float n = -transform.near;
   float f = -transform.far;

   vec3 ntl = vec3(-r, t, n);
   vec3 ntr = vec3(r, t, n);
   vec3 nbl = vec3(-r, -t, n);
   vec3 nbr = vec3(r, -t, n);

   // far plane
   float fr = r*f/n;
   float ft = t*f/n;

   vec3 ftl = vec3(-fr, ft, f);
   vec3 ftr = vec3(fr, ft, f);
   vec3 fbl = vec3(-fr, -ft, f);
   vec3 fbr = vec3(fr, -ft, f);

   // fixed origin for now
   // TODO: pass the camera origin
   vec3 origin = vec3(0.0);

   const vec3 positions[] = 
   {
      // right
      origin, fbr, ftr,
      // left
      origin, ftl, fbl,
      // top
      origin, ftr, ftl,
      // bottom
      origin, fbl, fbr
   };

   gl_Position = transform.projection * transform.view * vec4(positions[gl_VertexIndex], 1.0);
}
