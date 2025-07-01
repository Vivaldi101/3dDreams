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

// TODO: Move this to general renderer.h
// TODO: all caps
enum
{
   vk_renderer_index, 
	d3d12_renderer_index, 
   soft_renderer_index,
   renderer_count
};

typedef struct hw_window
{
   void*(*open)(const char* title, int x, int y, int width, int height);
   void (*close)(struct hw_window window);
   u32 width, height;
   void* handle;
} hw_window;

typedef struct hw_renderer
{
   void* backends[renderer_count];
   void(*frame_present)(struct hw* hw, void* context, app_state* state);
   void(*frame_resize)(struct hw* hw, u32 width, u32 height);
   void(*frame_wait)(void* renderer);
   void* (*window_surface_create)(void* instance, void* window_handle);
   hw_window window;

   // should be inside app.c
   mvp_transform mvp;
   // should be inside app.c

   u32 renderer_index;
} hw_renderer;

typedef struct hw_timer
{
   void(*sleep)(u32 ms);
   u32(*time)();
} hw_timer;

typedef struct hw
{
   hw_renderer renderer;
   arena vk_storage;
   hw_timer timer;
   app_state state;
   void (*log)(struct hw* hw, s8 message, ...);
   bool(*platform_loop)();
   bool finished;
} hw;

void hw_window_open(hw* hw, const char *title, int x, int y, int width, int height);
void hw_window_close(hw* hw);

void hw_event_loop_start(hw* hw, void (*app_frame_function)(arena scratch, struct app_state* state), void (*app_input_function)(struct app_state* state));
#endif
