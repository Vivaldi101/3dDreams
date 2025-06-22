
#if !defined(_GRAPHICS_H)
#define _GRAPHICS_H

#include "common.h"

#include <math.h>
#include <assert.h>

//#define USE_SIMD

#if defined(USE_SIMD)
#include <immintrin.h>
#endif

enum { G_PLANE_FRONT, G_PLANE_BACK, G_PLANE_ON, G_PLANE_SPLIT };

#define M_PI       3.14159265358979323846f   // pi

#define DEG2RAD(a) (((a) * M_PI) / 180.0F)
#define RAD2DEG(a) (((a) * 180.0f) / M_PI)

align_union
{ 
#if defined(USE_SIMD)
   __declspec(align(16)) __m128 data;
#else
   __declspec(align(16)) f32 data[4];
#endif
   struct
   {
      union { f32 x, r, s; };
      union { f32 y, g, t; };
      union { f32 z, b, p; };
      union { f32 w, a, q; };
   };
} vec4;

align_union
{ 
   f32 data[3];
   struct
   {
      union { f32 x, r, s, u; };
      union { f32 y, g, t, v; };
      union { f32 z, b, p, w; };
   };
} vec3;

align_struct
{ 
   vec3 vertex;
} vertex3;

align_union
{ 
   f32 data[2];
   struct
   {
      union { f32 x, r, s, u; };
      union { f32 y, g, t, v; };
   };
} vec2;

typedef vec4 quat;

// Should move the vector math to common math file

#define vec3_dot(a, b) ((a).x*(b).x + (a).y*(b).y + (a).z*(b).z)

static vec3 vec3_scale(const vec3* a, f32 s) 
{
   vec3 v = {a->x*s, a->y*s, a->z*s};
   return v;
}

static vec3 vec3_neg(const vec3* a) 
{
   vec3 v = {-a->x, -a->y, -a->z};
   return v;
}

static vec3 vec3_sub(const vec3* a, const vec3* b) 
{
   vec3 v = {b->x - a->x, b->y - a->y, b->z - a->z};
   return v;
}

static vec3 vec3_add(const vec3* a, const vec3* b) 
{
   vec3 v = {b->x + a->x, b->y + a->y, b->z + a->z};
   return v;
}

#define vec3_len2(a) vec3_dot((a), (a))
#define vec3_len(a) sqrtf(vec3_len2((a)))

#define vec3_normalize(a) { f32 l = vec3_len((a)); (a).x /= l; (a).y /= l; (a).z /= l;}

#pragma pack(push, 1)
typedef union
{ 
#if defined(USE_SIMD)
   __declspec(align(16)) vec4 rows[4];
#else
   __declspec(align(16)) f32 data[16];
#endif
} mat4;
#pragma pack(pop)

static inline mat4 mat4_identity()
{
   mat4 result = {};

#if defined(USE_SIMD)
   result.rows[0].data.m128_f32[0] = 1.0f;
   result.rows[1].data.m128_f32[1] = 1.0f;
   result.rows[2].data.m128_f32[2] = 1.0f;
   result.rows[3].data.m128_f32[3] = 1.0f;
#else
   result.data[0] = 1.0f;
   result.data[5] = 1.0f;
   result.data[10] = 1.0f;
   result.data[15] = 1.0f;
#endif

   return result;
}

static inline mat4 mat4_scale(mat4 m, f32 s)
{
   mat4 result = m;

   result.data[0] *= s;
   result.data[5] *= s;
   result.data[10] *= s;

   return result;
}

static inline mat4 mat4_translate(vec3 t)
{
   mat4 result = mat4_identity();

   result.data[12] = t.x;
   result.data[13] = t.y;
   result.data[14] = t.z;

   return result;
}

// transform a by b, post-multiplication
static inline mat4 mat4_mul(mat4 b, mat4 a)
{
   mat4 result = mat4_identity();

   f32* pa = a.data;
   f32* pb = b.data;

   f32* dst = result.data;

   for(i32 i = 0; i < 4; ++i)
   {
      for(i32 j = 0; j < 4; ++j)
      {
         *dst = pa[0] * pb[0 + j]      // rows of b
              + pa[1] * pb[4 + j]
              + pa[2] * pb[8 + j]
              + pa[3] * pb[12 + j];
         dst++;
      }
      pa += 4; // advance to second column of a
   }

   return result;
}

static inline mat4 mat4_flip_yz()
{
   mat4 result = mat4_identity();

   result.data[5] = -1.0f;
   result.data[10] = -1.0f;

   return result;
}

#define vec3_cross(a, b, c) (c).x = (a).y*(b).z - (a).z*(b).y; (c).y = (a).z*(b).x - (a).x*(b).z; (c).z = (a).x*(b).y - (a).y*(b).x;

#if 0
static vec3 vec3_cross(const vec3* a, const vec3* b)
{
   vec3 v = {(a)->y * (b)->z - (a)->z * (b)->y, (a)->z * (b)->x - (a)->x * (b)->z, (a)->x * (b)->y - (a)->y * (b)->x};
   return v;
}
#endif

static inline mat4 mat4_view(vec3 origin, vec3 dir)
{
   // compute the uvn vectors in view space with z going into the screen
   vec3 n = dir;
   // up
   vec3 v = {0.0f, 1.0f, 0.0f};

   vec3 u;
   vec3_cross(n, v, u);

   // recompute v
   vec3_cross(u, n, v);

   vec3_normalize(u);
   vec3_normalize(v);
   vec3_normalize(n);

   mat4 result = 
   {
      u.data[0],            v.data[0],             n.data[0],           0.0f,
      u.data[1],            v.data[1],             n.data[1],           0.0f,
      u.data[2],            v.data[2],             n.data[2],           0.0f,
      -vec3_dot(u, origin), -vec3_dot(v, origin), -vec3_dot(n, origin), 1.0f,
   };

   return result;
}

static inline mat4 mat4_perspective(f32 ar, f32 fov_y, f32 n, f32 f)
{
   //mat4 result = mat4_identity();

   f32 fov_half_y = fov_y/2.0f;
   f32 tan = tanf(DEG2RAD(fov_half_y));

   f32 t = n*tan;
   f32 r = t * ar;
   if(ar < 1.0f)
   {
      r = t;      // flip the aspect
      t = r / ar;   // re-establish invariant in push aspect

      assert(r == n*tan);
      assert(t > r);
   }

   assert(fabs(r - t * ar) < EPSILON);   // invariant

   f32 l = -r;
   f32 b = -t;

   f32 ax = (2 * n) / (r - l);
   f32 bx = -(r + l) / (r - l);

   f32 ay = (2 * n) / (t - b);
   f32 by = -(t + b) / (t - b);

   f32 z0 = -f / (n - f);
   f32 z1 = (f * n) / (n - f);

   mat4 result = 
   {
      ax,   0,   0,    0,
      0,    ay,  0,    0,
      bx,   by,  z0,   1.0f,
      0,    0,   z1,   0,
   };

   return result;
}

// normal and othogonal distance to the origin
align_struct g_plane { vec3 n; f32 d; } g_plane;

align_struct g_frustum { g_plane l,r,t,b,n,f; } g_frustum;

static int g_plane_classify_vertex_side(g_plane* plane, f32 vertex[3])
{
   // normal plane equation
   f32 p = plane->n.x*vertex[0] + plane->n.y*vertex[1] + plane->n.z*vertex[2] + plane->d;

   if(p > 0.0f) return G_PLANE_FRONT;
   if(p < 0.0f) return G_PLANE_BACK;
   return G_PLANE_ON;
}

static int g_plane_classify_face_side(g_plane* plane, f32 face[3*3])
{
   int fn = 0, bn = 0, i;
   for(i = 0; i < 3; ++i) // per vertex of face
   {
      switch(g_plane_classify_vertex_side(plane, face + (i*3)))
      {
      case G_PLANE_FRONT:
         ++fn; 
         break;
      case G_PLANE_BACK:
         ++bn; 
         break;
      case G_PLANE_ON:
         ++fn; ++bn; 
         break;
      }
   }
   if(fn == 3) return G_PLANE_FRONT;
   if(bn == 3) return G_PLANE_BACK;
   return G_PLANE_SPLIT;
}

static void g_plane_create(g_plane* plane, const vec3* a, const vec3* b, const vec3* c)
{
   vec3 ab = vec3_sub(a, b);
   vec3 ac = vec3_sub(a, c);
   vec3 n;

   vec3_cross(ab, ac, n);
   vec3_normalize(n);

   plane->n = n;
   plane->d = -vec3_dot(*a, n);
}

static void g_frustum_create(g_frustum* frustum, f32 w, f32 h, f32 hfov)
{
   f32 xr, xl, yb, yt;
   f32 z = w / (2.0f*tanf(DEG2RAD(hfov/2.0f)));
   const vec3 origin = {0.0f, 0.0f, 0.0f};
   vec3 vlb, vlt, vrb, vrt, vtl, vtr, vbl, vbr;

   xl = -w / (2*z);
   xr = w / (2*z);
   yb = -h / (2*z);
   yt = h / (2*z);

   // The above were used with an implicit z-viewspace distance of one. We need to negate it for right-handed camera space for the frustum plane normals 
   z = -1.0f;

   vlb.x = xl;    vlt.x = xl; 
   vlb.y = yb;    vlt.y = yt;
   vlb.z = z;     vlt.z = z;

   vrb.x = xr;    vrt.x = xr;
   vrb.y = yb;    vrt.y = yt;
   vrb.z = z;     vrt.z = z;

   vtl.x = xl;    vtr.x = xr;
   vtl.y = yt;    vtr.y = yt;
   vtl.z = z;     vtr.z = z;

   vbl.x = xl;    vbr.x = xr;
   vbl.y = yb;    vbr.y = yb;
   vbl.z = z;     vbr.z = z;

   g_plane_create(&frustum->l, &origin, &vlt, &vlb);
   g_plane_create(&frustum->r, &origin, &vrb, &vrt);
   g_plane_create(&frustum->t, &origin, &vtr, &vtl);
   g_plane_create(&frustum->b, &origin, &vbl, &vbr);
}

static bool g_plane_intersect_segment(g_plane* plane, f32 v0[3], f32 v1[3], f32 vi[3])
{
   f32 a,b,t;

   // othogonal distance to the plane for two points of the segment
   a = plane->n.x*v0[0] + plane->n.y*v0[1] + plane->n.z*v0[2] + plane->d;
   b = plane->n.x*v1[0] + plane->n.y*v1[1] + plane->n.z*v1[2] + plane->d;

   // no intersection to line parallel to the plane
   if (a == b)
      return false;

   // ratio from similar triangles of projection of the points onto the plane
   t = a / (a - b);

   // must be inside the segment from v0 to v1
   if (0.0f > t || t > 1.0f)
      return false;

   vi[0] = v0[0] + t*(v1[0] - v0[0]);
   vi[1] = v0[1] + t*(v1[1] - v0[1]);
   vi[2] = v0[2] + t*(v1[2] - v0[2]);

   return true;
}

static mat4 mat4_rotation_x(f32 rotx)
{
   rotx = DEG2RAD(rotx);
   mat4 result = 
   {
      1.0f, 0.0f,       0.0f,       0.0f,
      0.0f, cosf(rotx), sinf(rotx), 0.0f,
      0.0f, -sinf(rotx), cosf(rotx), 0.0f,
      0.0f, 0.0f,       0.0f,       1.0f,
   };

   return result;
}

static mat4 mat4_rotation_y(f32 roty)
{
   roty = DEG2RAD(roty);
   mat4 result = 
   {
      cosf(roty), 0.0f, -sinf(roty), 0.0f,
      0.0f,       1.0f, 0.0f,       0.0f,
      sinf(roty),0.0f, cosf(roty), 0.0f,
      0.0f,       0.0f, 0.0f,       1.0f,
   };

   return result;
}

static mat4 mat4_rotation_z(f32 rotz)
{
   rotz = DEG2RAD(rotz);
   mat4 result = 
   {
      cosf(rotz), sinf(rotz), 0.0f,      0.0f,     // column 1
      -sinf(rotz), cosf(rotz),  0.0f,      0.0f,   // column 2
      0.0f,       0.0f,        1.0f,       0.0f,   // column 3
      0.0f,       0.0f,        0.0f,       1.0f,   // column 4
   };

   return result;
}

#pragma pack(push, 1)
// TODO: #include in shaders
typedef struct mvp_transform
{
    mat4 projection;
    mat4 view;
    mat4 model;
    f32 n;
    f32 f;
    f32 ar;
} mvp_transform;
#pragma pack(pop)

#endif
