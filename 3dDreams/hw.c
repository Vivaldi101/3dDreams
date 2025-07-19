#include "hw.h"
#include "common.h"
#include "app.h"
#include "graphics.h"
#include "vulkan_ng.h"  // todo: should move this out

#define MAX_ARGV 32

static VkSurfaceKHR window_surface_create(void* instance, void* window_handle)
{
   PFN_vkCreateWin32SurfaceKHR vk_surface_function = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");

   if(!vk_surface_function)
      return 0;

   VkWin32SurfaceCreateInfoKHR surface_info = {0};
   surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
   surface_info.hinstance = GetModuleHandleA(0);
   surface_info.hwnd = window_handle;

   VkSurfaceKHR surface = 0;
   vk_surface_function(instance, &surface_info, 0, &surface);

   return surface;
}

void hw_window_open(hw* hw, const char *title, int x, int y, int w, int h)
{
   hw->renderer.window.handle = hw->renderer.window.open(title, x, y, w, h);
   hw->renderer.window.width = w;
   hw->renderer.window.height = h;

   inv(hw->renderer.window.handle);
   SetWindowLongPtr(hw->renderer.window.handle, GWLP_USERDATA, (LONG_PTR)hw);
}

void hw_window_close(hw* hw)
{
	pre(hw->renderer.window.handle);
   hw->renderer.window.close(hw->renderer.window);
}

#define MSEC_PER_SIM ((1.0/60)*1000)

static f64 global_game_time_residual;
static int global_game_frame;

static LARGE_INTEGER GetWallClock()
{
	LARGE_INTEGER result;
   QueryPerformanceCounter(&result);

   return result;
}

static u64 global_perf_counter_frequency;

static f32 GetSecondsElapsed(LARGE_INTEGER start, LARGE_INTEGER end)
{
   return ((f32)end.QuadPart - start.QuadPart) / (f32)global_perf_counter_frequency;
}

#if 0
static void hw_frame_sync2(hw* hw)
{
   LARGE_INTEGER work_counter = GetWallClock();
   f32 work_seconds_elapsed = GetSecondsElapsed(last_counter, work_counter);
   f32 seconds_elapsed_for_frame = work_seconds_elapsed;
   if(seconds_elapsed_for_frame < 0) // todo: replace 0
   {
      if(1) {
         DWORD sleep_ms = (DWORD)(1000.0f * (target_seconds_per_frame - seconds_elapsed_for_frame));
         if(sleep_ms > 0) {
            Sleep(sleep_ms - 1);
         }
      }
      f32 test_spf = GetSecondsElapsed(last_counter, GetWallClock());
      while(seconds_elapsed_for_frame < target_seconds_per_frame) {
         seconds_elapsed_for_frame = GetSecondsElapsed(last_counter, GetWallClock());
      }
   }
   else {
      //miss frame rate
   }
}
#endif

// TODO: Use perf counters for better granularity
static void hw_frame_sync(hw* hw)
{
	int num_frames_to_run = 0;
   for (;;) 
   {
      const int current_frame_time = hw->timer.time();
      static int last_frame_time = 0;
      if (last_frame_time == 0) 
         last_frame_time = current_frame_time;

      int delta_milli_seconds = current_frame_time - last_frame_time;
      last_frame_time = current_frame_time;

      global_game_time_residual += delta_milli_seconds;

      for (;;) 
      {
         // how much to wait before running the next frame
         if (global_game_time_residual < MSEC_PER_SIM)
            break;
         global_game_time_residual -= MSEC_PER_SIM;
         global_game_frame++;
         num_frames_to_run++;
      }
      if (num_frames_to_run > 0)
         break;

      hw->timer.sleep(0);
   }
}

static void hw_frame_render(hw* hw)
{
   void** renderers = hw->renderer.backends;
   const u32 renderer_index = hw->renderer.renderer_index;

   if(!hw->renderer.frame_present)
      return;

   if(hw->renderer.window.width == 0 || hw->renderer.window.height == 0)
      return;

   pre(renderer_index < RENDERER_COUNT);
   hw->renderer.frame_present(hw, renderers[renderer_index], &hw->state);
}

static void hw_log(hw* hw, s8 message, ...)
{
   hw->window_title(hw, message);
}

void hw_event_loop_start(hw* hw, void (*app_frame_function)(arena scratch, app_state* state), void (*app_input_function)(struct app_state* state))
{
   // start the timer
   u32 t = hw->timer.time();
   u32 s = hw->timer.time();

   f32 radius = hw->state.camera.radius = 2.0f;
   vec3 origin = {0.f, 0.f, 0.f};
   f32 azimuth = hw->state.camera.azimuth = -(PI / 2.f); // 1/4 turn to watch align +z
   f32 altitude = 0.f;
   f32 x = radius * cosf(altitude) * cosf(azimuth);
   f32 z = radius * cosf(altitude) * sinf(azimuth);
   f32 y = radius * sinf(altitude);
   vec3 eye = {x, y, z};

   hw->state.camera.pos = eye;
   hw->state.camera.dir = vec3_sub(&eye, &origin);

   for (;;)
   {
      if (!hw->platform_loop()) 
         break;

      hw->state.camera.viewplane_width = hw->renderer.window.width;
      hw->state.camera.viewplane_height = hw->renderer.window.height;

      app_input_function(&hw->state);
      app_frame_function(hw->vk_storage, &hw->state);

      // TODO: Use perf counters for better granularity
      hw_frame_sync(hw);
      hw_frame_render(hw);
   }
}

static int cmd_get_arg_count(char* cmd)
{
	int count = *cmd ? 1 : 0;
   char* arg_start = cmd;
   char* arg_end;

   while((arg_end = strchr(arg_start, ' ')))
   {
      if(count >= MAX_ARGV)                   // exceeds our max number of arguments
         break;
      arg_start = arg_end + 1;
      count++;
   }

   return count;
}

#if 0
static char** cmd_parse(arena* storage, char* cmd, int* argc)
{
	*argc = cmd_get_arg_count(cmd);
   char* arg_start = cmd;

   arena_result result = arena_alloc(*storage, sizeof(cmd), *argc);
   for(size i = 0; i < result.count; ++i)
   {
      char* arg_end = strchr(arg_start, ' ');
		((char**)result.data)[i] = arg_start;

		if(!arg_end)
			break;
		*arg_end = 0;	// cut it
      arg_start = arg_end + 1;
   }

   return (char**)result.data;
}
#endif
