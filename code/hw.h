#if !defined(_HW_H)
#define _HW_H

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define hw_message_box(p) { MessageBoxA(0, #p, "Assertion", MB_OK); __debugbreak(); }
#pragma comment(lib,	"winmm.lib") // timers etc.
#elif
// other plats like linux, osx and ios
#endif

// Every platform should define hw_message
#define hw_assert(p) if(!(p)) hw_message_box(p)

#include "app.h"
#include "arena.h"
#include "common.h"

#include "vulkan_ng.h"

enum
{
   VULKAN_RENDERER_INDEX,
   D3D12_RENDERER_INDEX,
   SOFT_RENDERER_INDEX,
   RENDERER_COUNT
};

align_struct hw_window
{
   void*(*open)(const char* title, int x, int y, int width, int height);
   void (*close)(struct hw_window window);
   u32 width;
   u32 height;
   void* handle;
   bool resize;
} hw_window;

typedef union hw_result
{
   void* h;
   size i;
   u8 b;
   static_assert(sizeof(void*) != sizeof(u8));
} hw_result;

align_struct hw_renderer
{
   void* backends[RENDERER_COUNT];
   void(*frame_render)(struct hw_renderer* renderer, void* context, app_state* state);
   void(*frame_present)(struct hw_renderer* renderer, void* context);
   void(*frame_resize)(struct hw_renderer* renderer, u32 width, u32 height);
   void(*gpu_log)(hw* hw);

   hw_result (*window_surface_create)(struct vk_allocator* allocator, void* instance, void* window_handle);
   vec2 (*window_size)(hw_window* window);
   hw_window window;

   u32 renderer_index;
} hw_renderer;

align_struct hw_timer
{
   void(*sleep)(u32 ms);
   i64(*time)();
   f64(*seconds_elapsed)(i64 begin, i64 end);
   f64(*time_to_counter)(f64 time);
} hw_timer;

align_struct hw
{
   hw_renderer renderer;
   arena* app_storage;
   arena* vulkan_storage;
   arena scratch;
   hw_timer timer;
   app_state state;
   void* main_fiber;
   void* message_fiber;

   bool quit;
   
   void (*window_title_set)(struct hw* hw, s8 message, ...);
   #ifdef _WIN32
   void (CALLBACK *platform_loop)(struct hw* hw);
   #endif
} hw;

bool hw_window_open(hw* hw, const char *title, int x, int y, int width, int height);
void hw_window_close(hw* hw);

void hw_event_loop_start(hw* hw, void (*app_frame_function)(arena scratch, struct app_state* state), void (*app_input_function)(struct app_state* state));
#endif
