#if !defined(_COMMON_H)
#define _COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hw.h"

typedef uint8_t         u8;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef uint64_t        u64;
typedef float           f32;
typedef double          f64;
typedef uintptr_t       uptr;
typedef unsigned char   byte;
typedef ptrdiff_t       size;
typedef size_t          usize;

#define s8(s) (s8){(u8 *)s, lengthof(s)}
typedef struct
{
   u8* data;
   size len;
} s8;

#ifdef _DEBUG
#define pre(p)  {if(!(p))hw_message(p)}
#define post(p) {if(!(p))hw_message(p)}
#define inv(p)  {if(!(p))hw_message(p)}
#else
#define pre(p)
#define post(p)
#define inv(p)
#endif

#define iff(p, q) (p) == (q)
#define implies(p, q) (!(p) || (q))

#define custom_alignment 64
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

static const u64 default_arena_size = KB(4096*100);

#define clamp(t, min, max) ((t) <= (min) ? (min) : (t) >= (max) ? (max) : (t))

#define EPSILON 1e-6  // Adjust this as needed

#endif
