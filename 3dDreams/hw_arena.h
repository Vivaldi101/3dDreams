#if !defined(_HW_MEMORY_H)
#define _HW_MEMORY_H

#include <windows.h>

#include "common.h"

#if defined(_WIN32)
typedef LPVOID(*VirtualAllocPtr)(LPVOID, SIZE_T, DWORD, DWORD);
typedef VOID(*VirtualReleasePtr)(LPVOID, SIZE_T);
static VirtualAllocPtr global_allocate;
static VirtualReleasePtr global_release;
#elif
// TODO: other plats have their own global allocators
#endif

#define arena_push_struct(arena, type) (hw_buffer_push(arena, sizeof(type), sizeof(type)))  
#define arena_push_count(arena, count, type) (hw_buffer_push(arena, (count)*sizeof(type), sizeof(type))) 
#define arena_push_size(arena, count, type) (hw_buffer_push(arena, (count)*sizeof(byte), sizeof(type)))  
#define arena_push_pointer_strings(arena, count) hw_buffer_push(arena, (count)*sizeof(const char*), sizeof(const char*))

#define arena_pop_struct(arena, type) ((type *)_pop_(arena, sizeof(type)))  
#define arena_pop_count(arena, count, type) ((type *)hw_buffer_pop(arena, (count)*sizeof(type)))  
#define arena_pop_string(arena, count) arena_pop_count(arena, count, char)

#define sub_arena_release(arena) hw_sub_buffer_release(arena)
#define sub_arena_create(arena) hw_sub_memory_buffer_create(arena)

#define arena_create(size) hw_buffer_create(size)
#define arena_release(arena) hw_buffer_release(arena)

#define arena_is_empty(arena) hw_buffer_is_empty(arena)
#define arena_is_full(arena) hw_buffer_is_full(arena)
#define arena_is_set(arena, elements) hw_buffer_is_set(arena, elements)

#define arena_base(arena, type) ((type*)(arena)->base)
#define arena_index(arena, type, index) ((type*)(arena)->base + index)

static void hw_global_reserve_available()
{
   MEMORYSTATUSEX memory_status;
   memory_status.dwLength = sizeof(memory_status);

   GlobalMemoryStatusEx(&memory_status);

   global_allocate(0, memory_status.ullAvailPhys, MEM_RESERVE, PAGE_READWRITE);
}

static bool hw_is_virtual_memory_reserved(void* address)
{
   MEMORY_BASIC_INFORMATION mbi;
   if(VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
      return false;

   return mbi.State == MEM_RESERVE;
}

static bool hw_is_virtual_memory_commited(void* address)
{
   MEMORY_BASIC_INFORMATION mbi;
   if(VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
      return false;

   return mbi.State == MEM_COMMIT;
}

static void* hw_virtual_memory_reserve(usize size)
{
	// let the os decide into what address to place the reserve
   return global_allocate(0, size, MEM_RESERVE, PAGE_NOACCESS);
}

static void hw_virtual_memory_commit(void* address, usize size)
{
	pre(hw_is_virtual_memory_reserved((byte*)address+size-1));
	pre(!hw_is_virtual_memory_commited((byte*)address+size-1));

	// commit the reserved address range
   global_allocate(address, size, MEM_COMMIT, PAGE_READWRITE);
}

static void hw_virtual_memory_release(void* address, usize size)
{
	pre(hw_is_virtual_memory_commited((byte*)address+size-1));

	global_release(address, size);
}

static void hw_virtual_memory_decommit(void* address, usize size)
{
	pre(hw_is_virtual_memory_commited((byte*)address+size-1));

	global_allocate(address, size, MEM_DECOMMIT, PAGE_READWRITE);
}

static void hw_virtual_memory_init()
{
   typedef LPVOID(*VirtualAllocPtr)(LPVOID, usize, DWORD, DWORD);

   HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
   if (hKernel32)
   {
      global_allocate = (VirtualAllocPtr)(GetProcAddress(hKernel32, "VirtualAlloc"));
      global_release = (VirtualReleasePtr)(GetProcAddress(hKernel32, "VirtualFree"));
   }

	post(global_allocate);
	post(global_release);
}

static hw_arena hw_buffer_create(usize byte_count) 
{
   hw_arena result = {0};
   pre(global_allocate);

   void* base = hw_virtual_memory_reserve(byte_count);
   hw_virtual_memory_commit(base, byte_count);

   result.base = base;
   result.max_size = byte_count;
   result.bytes_used = 0;

   return result;
}

static void hw_buffer_release(hw_arena *buffer)
{
	hw_virtual_memory_release(buffer->base, buffer->max_size);
}

static void hw_buffer_decommit(hw_arena *buffer)
{
	hw_virtual_memory_decommit(buffer->base, buffer->max_size);
}

static hw_frame_arena hw_sub_memory_buffer_create(const hw_arena *buffer)
{
   hw_frame_arena result = {0};
   pre(buffer->max_size >= buffer->bytes_used);

   result.bytes_used = 0;
   result.max_size = buffer->max_size - buffer->bytes_used;
   result.base = (byte*)buffer->base + buffer->bytes_used;

   post(result.bytes_used == 0);
   post(result.max_size == buffer->max_size - buffer->bytes_used);
   post(result.base == (byte*)buffer->base + buffer->bytes_used);

   return result;
}

static void* hw_buffer_top(hw_arena *buffer) 
{
   return (byte*)buffer->base + buffer->bytes_used;
}

// TODO: element_size and count?
static hw_arena hw_buffer_push(hw_arena *buffer, usize bytes, usize element_size) 
{
   hw_arena result = {0};
   if(buffer->bytes_used + bytes > buffer->max_size)
      return (hw_arena){0};

   result.base = (byte*)buffer->base + buffer->bytes_used;
   result.max_size = bytes;
   result.count += bytes / element_size;
   buffer->bytes_used += bytes;

   return result;
}

static void* hw_buffer_pop(hw_arena *buffer, usize bytes) 
{
   void *result;
   pre(buffer->bytes_used >= bytes);

   buffer->bytes_used -= bytes;
   result = (byte*)buffer->base + buffer->bytes_used;

   return result;
}

static void hw_sub_buffer_release(hw_frame_arena* buffer) 
{
	memset(buffer, 0, sizeof(hw_frame_arena));
}

static bool hw_buffer_is_empty(const hw_arena *buffer) 
{
	return buffer->count == 0;
}

static bool hw_buffer_is_full(hw_arena *buffer) 
{
	return buffer->bytes_used == buffer->max_size;
}

static bool hw_buffer_is_set(hw_arena *buffer, usize count) 
{
	return buffer->count == count; 
}

#endif
