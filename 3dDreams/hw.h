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

// TODO: Move this to general renderer.h
enum
{
   VK_RENDERER_INDEX,
   D3D12_RENDERER_INDEX,
   SOFT_RENDERER_INDEX,
   RENDERER_COUNT
};

align_struct hw_window
{
   void*(*open)(const char* title, int x, int y, int width, int height);
   void (*close)(struct hw_window window);
   u32 width, height;
   void* handle;
} hw_window;

align_struct hw_renderer
{
   void* backends[RENDERER_COUNT];
   void(*frame_present)(struct hw* hw, void* context, app_state* state);
   void(*frame_resize)(struct hw* hw, u32 width, u32 height);
   void(*frame_wait)(void* renderer);
   void* (*window_surface_create)(void* instance, void* window_handle);
   hw_window window;

   // should be inside app.c
   app_mvp_transform mvp;
   // should be inside app.c

   u32 renderer_index;
} hw_renderer;

align_struct hw_timer
{
   void(*sleep)(u32 ms);
   u32(*time)();
} hw_timer;

align_struct hw
{
   hw_renderer renderer;
   arena vk_storage;
   hw_timer timer;
   app_state app;
   void (*window_title)(struct hw* hw, s8 message, ...);
   void (*log)(s8 message, ...);
   bool(*platform_loop)();
   bool finished;
} hw;

void hw_window_open(hw* hw, const char *title, int x, int y, int width, int height);
void hw_window_close(hw* hw);

void hw_event_loop_start(hw* hw, void (*app_frame_function)(arena scratch, struct app_state* state), void (*app_input_function)(struct app_state* state));
#endif
