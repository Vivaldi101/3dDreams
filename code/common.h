#if !defined(_COMMON_H)
#define _COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

typedef uint8_t         u8;
typedef uint16_t        u16;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef uint64_t        u64;
typedef int64_t         i64;
typedef float           f32;
typedef double          f64;
typedef uintptr_t       uptr;
typedef unsigned char   byte;
typedef size_t          usize;
typedef usize           size;

#define s8(s) (s8){(u8 *)s, strlen(s)}
#define s8_data(s) (const char*)(s).data

#ifdef _DEBUG
#define pre(p)  {if(!(p))hw_message_box(p)}
#define post(p) {if(!(p))hw_message_box(p)}
#define inv(p)  {if(!(p))hw_message_box(p)}
#else
#define pre(p)
#define post(p)
#define inv(p)
#endif

#define fault(p)  {hw_message_box(p); __debugbreak();}

//#undef assert
//#define assert(cond) do { if (DoYouWantToBreak(cond, __FILE__, __LINE__)) __debugbreak(); } while (0)

#define iff(p, q) (p) == (q)
#define implies(p, q) (!(p) || (q))

#define custom_alignment 64
static_assert(custom_alignment == 64, "");

#define align_struct __declspec(align(custom_alignment)) typedef struct
#define align_union __declspec(align(custom_alignment)) typedef union

#define array_clear(a) memset((a), 0, array_count(a)*sizeof(*(a)))
#define array_count(a) sizeof((a)) / sizeof((a)[0])

#define defer(start, end) \
    for (int _defer = ((start), 0); !_defer; (_defer = 1), (end))

#define defer_frame(main, sub, frame) defer((sub) = sub_arena_create((main)), sub_arena_release(&(sub))) frame

#define arena_is_stub(s) !(s.base) || (s.max_size == 0)

#define KB(k) (1024ull)*k
#define MB(m) (1024ull)*KB((m))
#define GB(g) (1024ull)*MB((g))

//static const size default_arena_size = KB(4096);

#define clamp(t, min, max) ((t) <= (min) ? (min) : (t) >= (max) ? (max) : (t))

#define EPSILON 1e-6  // Adjust this as needed

#define PI 3.14159265358979323846f

#define struct_clear(s) memset(&(s), 0, sizeof(s))

#define page_size (4096)
#define align_page_size (4096 -1)

typedef struct s8
{
   u8* data;
   size len;
} s8;

static int s8_compare(s8 a, s8 b)
{
   return strcmp(s8_data(a), s8_data(b));
}

static bool s8_equals(s8 a, s8 b)
{
   return s8_compare(a, b) == 0;
}

static bool s8_is_substr(s8 str, s8 sub)
{
   for(size i = 0; i < str.len; ++i)
      if (!strcmp((char*)str.data + i, (char*)sub.data))
         return true;

   return false;
}

static size s8_is_substr_count(s8 str, s8 sub)
{
   for(size i = 0; i < str.len; ++i)
      if (!strncmp(s8_data(str) + i, s8_data(sub), sub.len))
         return i;

   return (size)-1;
}

static s8 s8_slice(s8 str, size beg, size end)
{
   assert(0 <= end - beg);
   assert(end - beg <= str.len);

   return (s8){str.data + beg, end - beg};
}

#endif
