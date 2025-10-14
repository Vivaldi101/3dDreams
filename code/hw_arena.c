#include "hw_arena.h"

hw_memory_buffer HW_memory_buffer_create(size_t num_bytes) 
{
    hw_memory_buffer result = {0};
    void *ptr = VirtualAlloc(0, num_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    
    result.base = (byte *)ptr;
    result.max_size = num_bytes;
    result.bytes_used = 0;
    
    return result;
}

#if 0
static void* HW_memory_buffer_top(hw_memory_buffer *buffer) 
{
    void *ptr = buffer->base + buffer->bytes_used;
    return ptr;
}

// FIXME: pass alignment
static void* HW_memory_buffer_push(hw_memory_buffer *buffer, size_t num_bytes) 
{
    void *result = buffer->base + buffer->bytes_used;
    buffer->bytes_used += num_bytes;
    
    return result;
}

static void* HW_memory_buffer_pop(hw_memory_buffer *buffer, size_t num_bytes) 
{
    void *result;
    buffer->bytes_used -= num_bytes;
    result = buffer->base + buffer->bytes_used;
    
    return result;
}


static size_t isMemoryStackEmpty(MemoryStack *ms)
{
    return(!(ms->bytes_used > 0));
}

static void FreeMemoryStack(MemoryStack *ms)
{
    if(ms->base)
    {
        VirtualFree(ms->base, 0, MEM_RELEASE);
        ms->max_size = 0;
        ms->bytes_used = 0;
    }
}

static u32
GetNumMemoryStackElements(MemoryStack *ms, size_t element_size)
{
    u32 result = (u32)(ms->bytes_used / element_size);
    return result;
}

static void *
_GetLastElement_(MemoryStack *ms, size_t element_size)
{
    void *result = nullptr;
    u32 elementCount = ms->elementCount-1;
    result = ms->base + elementCount*element_size;
    
    return result;
}

static void *
_GetAt_(MemoryStack *ms, size_t element_size, u32 index)
{
    void *result = nullptr;
    result = ms->base + index*element_size;
    
    return result;
}
#endif
