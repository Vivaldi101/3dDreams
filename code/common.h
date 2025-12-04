#if !defined(_COMMON_H)
#define _COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h> // TODO: this goes away soon
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

static const size invalid_index = (size)-1;

#define s8(s) (s8){(u8 *)s, strlen(s)}
#define s8_data(s) (const char*)(s).data

#define fault(p)  {hw_message_box(p); __debugbreak();}

#define implies(p, q) (!(p) || (q))
#define iff(p, q) implies((p), (q)) && implies((q), (p))

#define custom_alignment 64
static_assert(custom_alignment == 64, "");

#define align_struct __declspec(align(custom_alignment)) typedef struct
#define align_union __declspec(align(custom_alignment)) typedef union

#define array_count(a) sizeof((a)) / sizeof((a)[0])

#define defer(start, end) \
    for (int _defer = ((start), 0); !_defer; (_defer = 1), (end))

#define defer_frame(main, sub, frame) defer((sub) = sub_arena_create((main)), sub_arena_release(&(sub))) frame

#define arena_is_stub(s) !(s.base) || (s.max_size == 0)

#define KB(k) (1024ull)*k
#define MB(m) (1024ull)*KB((m))
#define GB(g) (1024ull)*MB((g))

#define clamp(t, min, max) ((t) <= (min) ? (min) : (t) >= (max) ? (max) : (t))

#define EPSILON 1e-6  // Adjust this as needed

#define PI 3.14159265358979323846f

#define pointer_clear_to(p, v, s) memset((p), (v), (s))
#define pointer_clear(p, s) pointer_clear_to((p), 0, (s))
#define struct_clear(s) {static_assert(sizeof(*(s)) != sizeof(void*)); pointer_clear((s), sizeof(*s));}

#define PAGE_SIZE KB(4)
#define ALIGN_PAGE_SIZE (PAGE_SIZE - 1)

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

   return invalid_index;
}

static s8 s8_slice(s8 str, size beg, size end)
{
   assert(0 <= end - beg);
   assert(end - beg <= str.len);

   return (s8){str.data + beg, end - beg};
}

#endif
