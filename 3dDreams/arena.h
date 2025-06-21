#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "common.h"

#define arena_iterate(a, s, type, var, code_block) \
do { \
    arena scratch_ = push(&(a), type, (s)); \
    type* (var) = scratch_.beg; \
    size_t count_ = scratch_size(scratch_) / sizeof(type); \
    for (size_t i = 0; i < count_; ++i) code_block \
} while (0)

#define arena_full(a)      ((a)->beg == (a)->end)   // or empty for stub arenas
#define arena_loop(i, a, p) for(size (i) = 0; (i) < scratch_left((a), *(p)); ++(i))
#define arena_offset(i, a, t) (t*)a.beg + (i)

#define scratch_left(a) (size)((char*)(a).end - (char*)(a).beg)
#define arena_left(a) (size)((char*)(a)->end - (char*)(a)->beg)

#define scratch_count_left(a,t)  (size)(scratch_left(a) / sizeof(t))
#define arena_count_left(a,t)    (size)(arena_left(a) / sizeof(t))

#define arena_stub(p, a) ((p) == (a)->end)
#define scratch_stub(p, a) arena_stub(p, &a)

#define arena_end_count(p, a, n) (void*)(p)!=(a)->end?(p)+(n):(p)
#define scratch_end_count(p, a, n) (void*)(p)!=(a).end?(p)+(n):(p)

#define scratch_clear_value(a, v) memset((a).beg, (v), scratch_left((a)))
#define scratch_clear(a) scratch_clear_value(a, 0)

#define scratch_invariant(s, a, t) assert((s) <= scratch_left((a), t))
#define scratch_shrink(a, s, t) (a).end = (char*)(a).beg + (s)*sizeof(t)

#define arena_invariant(s, a, t) assert((s) <= arena_left((a), t))
#define arena_shrink(a, s, t) (a)->end = (a)->beg + (s)*sizeof(t)

#define newx(a,b,c,d,e,...) e
#define push(...)            newx(__VA_ARGS__,new4,new3,new2)(__VA_ARGS__)
#define new2(a, t)          (t*)alloc(a, sizeof(t), __alignof(t), 1, 0)
#define new3(a, t, n)       (t*)alloc(a, sizeof(t), __alignof(t), n, 0)
#define new4(a, t, n, f)    (t*)alloc(a, sizeof(t), __alignof(t), n, f)

#define push_array(...)           newx(__VA_ARGS__,new4_array,new3_array,new2_array)(__VA_ARGS__)
#define new2_array(a, t)          (t*)array_alloc(a, sizeof(t), __alignof(t), 1, 0)
#define new3_array(a, t, n)       (t*)array_alloc(a, sizeof(t), __alignof(t), n, 0)
#define new4_array(a, t, n, f)    (t*)array_alloc(a, sizeof(t), __alignof(t), n, f)

#define newxsize(a,b,c,d,e,...) e
#define push_size(...)            newxsize(__VA_ARGS__,new4size,new3size,new2size)(__VA_ARGS__)
#define new2size(a, t)          alloc(a, t, __alignof(t), 1, 0)
#define new3size(a, t, n)       alloc(a, t, __alignof(t), n, 0)
#define new4size(a, t, n, f)    alloc(a, t, __alignof(t), n, f)

#define sizeof(x)       (size)sizeof(x)
#define countof(a)      (sizeof(a) / sizeof(*(a)))
#define lengthof(s)     (countof(s) - 1)
#define amountof(a, t)  ((a) * sizeof(t))

typedef struct arena
{
   void* beg;
   void* end;  // one past the end
} arena;

typedef struct scratch_array
{
   arena arena;
   size count;
   void* data;
} scratch_array;

static bool hw_is_virtual_memory_commited(void* address)
{
   MEMORY_BASIC_INFORMATION mbi;
   if(VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
      return false;

   return mbi.State == MEM_COMMIT;
}

// TODO: use GetSystemInfo
static arena arena_new(arena* base, size cap)
{
   assert(base->end && cap > 0);
   assert(cap >= page_size);

   arena result = {0};

   result.beg = VirtualAlloc(base->end, cap, MEM_COMMIT, PAGE_READWRITE);
   result.end = (byte*)result.beg + cap;

   assert(result.beg < result.end);

   return result;
}

static void arena_expand(arena* a, size new_cap)
{
   assert(new_cap > 0);
   assert((uintptr_t)a->end <= ((1ull << 48)-1) - page_size);
   arena new_arena = arena_new(a, new_cap);

   new_arena.beg = a->end;

   assert(new_arena.beg == a->end);
   assert(new_arena.end > a->end);

   a->end = (byte*)new_arena.beg + new_cap;

   // post
   assert(a->end == (byte*)new_arena.beg + new_cap);
}

static void* alloc(arena* a, size alloc_size, size align, size count, u32 flag)
{
   assert(align <= align_page_size);

   // align allocation to next aligned boundary
   void* p = (void*)(((uptr)a->beg + (align - 1)) & (-align));

   if(count <= 0 || count > ((byte*)a->end - (byte*)p) / alloc_size) // empty or overflow
   {
      arena_expand(a, ((count * alloc_size) + align_page_size) & ~align_page_size);
      p = a->beg;
   }

   a->beg = (byte*)p + (count * alloc_size);                         // advance arena 

   return p;
}

static void* array_alloc(scratch_array* a, size alloc_size, size align, size count, u32 flag)
{
   void* result = alloc(&a->arena, alloc_size, align, count, flag);

   a->count++;
   !a->data ? a->data = result : a->data;

   return result;
}

static bool arena_reset(arena* a)
{
   return VirtualAlloc(a->beg, (byte*)a->end - (byte*)a->beg, MEM_RESET, PAGE_READWRITE) != 0;
}

static bool arena_decommit(arena* a)
{
   return VirtualFree(a->beg, 0, MEM_DECOMMIT) != 0;
}
