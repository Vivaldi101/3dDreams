#if !defined(_ARENA_H)
#define _ARENA_H

#include "common.h"

// TODO: make areanas platform agnostic
#include <Windows.h>
void hw_virtual_memory_commit(void* address, usize size);
void hw_virtual_memory_decommit(void* address, usize size);

typedef enum alloc_flags
{
   arena_persistent_kind = 0,
   arena_scratch_kind,
   array_persistent_kind,
   array_scratch_kind,
} alloc_flags;

#define arena_left(a) (size)((byte*)(a)->end - (byte*)(a)->beg)

#define newx(a,b,c,d,e,...) e
#define push(...)            newx(__VA_ARGS__,new4,new3,new2)(__VA_ARGS__)
#define new2(a, t)          (t*)alloc(a, sizeof(t), __alignof(t), 1, 0)
#define new3(a, t, n)       (t*)alloc(a, sizeof(t), __alignof(t), n, 0)
#define new4(a, t, n, f)    (t*)alloc(a, sizeof(t), __alignof(t), n, f)

// TODO: functions?
// TODO: cleanup array_push() and array_add()
// Pushes to non preallocated app_storage
#define array_push(a)          (a).count++, *(typeof(a.data))array_alloc((array*)&a, sizeof(typeof(*a.data)), __alignof(typeof(*a.data)), 1, 0)
#define arrayp_push(a)      (a)->count++, *(typeof(a->data))array_alloc((array*)a, sizeof(typeof(*a->data)), __alignof(typeof(*a->data)), 1, 0)

// Adds to preallocated app_storage
#define array_add(a, v)        *((a.data + a.count++)) = (v)
#define array_resize(a, s)  {(a).data = alloc(a.arena, sizeof(typeof(*a.data)), __alignof(typeof(*a.data)), (s), 0);};

#define array_set(arr, a)  (arr).arena = a
#define array_set_size(arr, s, a)  {array_set((arr), (a)); array_resize((arr), (s)); array_clear((arr), (s));}

#define countof(a)      (sizeof(a) / sizeof(*(a)))
#define lengthof(s)     (countof(s) - 1)
#define amountof(a, t)  ((a) * sizeof(t))

align_struct arena
{
   void* beg;
   void* end;         // one past the end
} arena;

align_struct array
{
   arena* arena;
   size count;
   void* data;  // base
   void* old_beg;
} array;

align_struct array_fixed
{
   arena arena;
   size count;
   void* data;   // base
   void* old_beg;
} array_fixed;

// This cannot be passed to functions as is - must be used as a typedef
#define array(T) __declspec(align(custom_alignment)) struct { arena* arena; size count; T* data; void* old_beg; }

static bool hw_is_virtual_memory_commited(void* address)
{
   MEMORY_BASIC_INFORMATION mbi;
   if(VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
      return false;

   return mbi.State == MEM_COMMIT;
}

// TODO: Use hw_virtual_memory_commit(void* address, usize size) on commits
static arena* arena_new(arena* base, size cap, alloc_flags flag)
{
   assert(base->end && cap > 0);
   assert(cap >= PAGE_SIZE);

   arena* a = base;

   if((flag == arena_scratch_kind || flag == array_scratch_kind) && hw_is_virtual_memory_commited((byte*)base->end + cap - 1))
   {
      a->beg = base->end;
      a->end = (byte*)base->end + cap;

      assert((byte*)a->beg + cap == a->end);

      assert(a->beg == base->beg);
      assert(a->end == base->end);

      return a;
   }

   // alloc arena + payload
   arena* p = VirtualAlloc(base->end, cap + sizeof(arena), MEM_COMMIT, PAGE_READWRITE);
   assert(p);

   p->beg = p + sizeof(arena);
   p->end = (byte*)p->beg + cap;

   assert((byte*)p->beg + cap == p->end);

   return p;
}

static void arena_expand(arena* a, size new_cap, alloc_flags flag)
{
   assert(new_cap > 0);
   assert((uptr)a->end <= ((1ull << 48)-1) - PAGE_SIZE);

   arena* new_arena = arena_new(a, new_cap, flag);
   assert(new_arena->end >= a->end);

   a->end = (byte*)new_arena->end;

   assert(a->end == (byte*)new_arena->beg + new_cap);
}

static void* alloc(arena* a, size alloc_size, size align, size count, alloc_flags flag)
{
   (void)flag;
   assert(a);
   assert(alloc_size > 0);
   assert(align > 0);
   assert(count > 0);

   // align allocation to next aligned boundary
   void* p = (void*)(((uptr)a->beg + (align - 1)) & ~(uptr)(align - 1));

   if(count <= 0 || count > ((byte*)a->end - (byte*)p) / alloc_size) // empty or overflow
   {
      // page align allocs
      arena_expand(a, ((count * alloc_size) + ALIGN_PAGE_SIZE) & ~ALIGN_PAGE_SIZE, flag);
      p = a->beg;
   }

   a->beg = (byte*)p + (count * alloc_size);                         // advance arena 

   pointer_clear(p, count * alloc_size);

   return p;
}

static void array_decommit(array* a, size array_size)
{
   hw_virtual_memory_decommit(a->data, array_size);
   a->count = 0;
   a->arena->beg = a->data;
   a->old_beg = 0;
}

static void array_realloc(array* a, size old_size)
{
   assert(a->old_beg);

   // realloc old size
   if((a->old_beg != a->arena->beg) && old_size > 0)
   {
      // align to next page for the next array
      a->arena->beg = (void*)(((uptr)a->arena->beg + ALIGN_PAGE_SIZE) & ~ALIGN_PAGE_SIZE);

      hw_virtual_memory_commit(a->arena->beg, old_size);

      memmove(a->arena->beg, a->data, old_size);
      a->data = a->arena->beg;

      a->arena->beg = (byte*)a->arena->beg + old_size;
      a->arena->end = (byte*)a->arena->end + old_size;
      a->arena->end = (void*)(((uptr)a->arena->end + ALIGN_PAGE_SIZE) & ~ALIGN_PAGE_SIZE);
   }
}

static void* array_alloc(array* a, size alloc_size, size align, size count, u32 flag)
{
   if(a->data)
      array_realloc(a, (byte*)a->old_beg - (byte*)a->data);

   void* result = alloc(a->arena, alloc_size, align, count, flag);

   a->data = a->data ? a->data : result;
   a->old_beg = a->arena->beg;

   return result;
}

#endif
