#include "hw.h"
#include "common.h"
#include "app.h"
#include "math.h"
#include "vulkan_ng.h"

static VkSurfaceKHR window_surface_create(void* instance, void* window_handle)
{
   assert(instance);
   assert(window_handle);

   VkSurfaceKHR surface = 0;

#ifdef WIN32
   PFN_vkCreateWin32SurfaceKHR vk_surface_function = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");

   VkWin32SurfaceCreateInfoKHR surface_info = {0};
   surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
   surface_info.hinstance = GetModuleHandleA(0);
   surface_info.hwnd = window_handle;

   vk_assert(vk_surface_function(instance, &surface_info, 0, &surface));
#endif

   assert(vk_valid_handle(surface));

   return surface;
}

bool hw_window_open(hw* hw, const char *title, int x, int y, int w, int h)
{
   hw->renderer.window.handle = hw->renderer.window.open(title, x, y, w, h);
   hw->renderer.window.width = w;
   hw->renderer.window.height = h;

   if(!hw->renderer.window.handle)
      return false;

   SetLastError(0);
   if(!SetWindowLongPtrA(hw->renderer.window.handle, GWLP_USERDATA, (LONG_PTR)hw))
      return GetLastError() == 0;

   return true;
}

void hw_window_close(hw* hw)
{
	pre(hw->renderer.window.handle);
   hw->renderer.window.close(hw->renderer.window);
}

static i64 global_game_time_residual;
static i64 global_perf_counter_frequency;

static i64 clock_query_counter()
{
	LARGE_INTEGER result;
   QueryPerformanceCounter(&result);

   return result.QuadPart;
}

static LARGE_INTEGER clock_query_frequency()
{
	LARGE_INTEGER result;
   QueryPerformanceFrequency(&result);

   global_perf_counter_frequency = result.QuadPart;

   return result;
}

static f64 clock_seconds_elapsed(i64 start, i64 end)
{
   return ((f64)end - (f64)start) / (f64)global_perf_counter_frequency;
}

static f64 clock_time_to_counter(f64 time)
{
   return (f64)global_perf_counter_frequency * time;
}

static void hw_frame_sync(hw* hw, f64 frame_delta)
{
	int num_frames_to_run = 0;
   const i64 counter_delta = (i64)(clock_time_to_counter(frame_delta) + .5f);

   for (;;)
   {
      const i64 current_counter = clock_query_counter();
      static i64 last_counter = 0;
      if (last_counter == 0)
         last_counter = current_counter;

      i64 delta_counter = current_counter - last_counter;
      last_counter = current_counter;

      global_game_time_residual += delta_counter;

      for (;;)
      {
         // how much to wait before running the next frame
         if (global_game_time_residual < counter_delta)
            break;
         global_game_time_residual -= counter_delta;
         num_frames_to_run++;
      }
      if (num_frames_to_run > 0)
         break;

      hw->timer.sleep(0);
   }
}

static void hw_frame_present(hw* hw)
{
   void** renderers = hw->renderer.backends;
   const u32 renderer_index = hw->renderer.renderer_index;

   if(!hw->renderer.frame_present)
      return;

   if(hw->renderer.window.width == 0 || hw->renderer.window.height == 0)
      return;

   pre(renderer_index < RENDERER_COUNT);
   hw->renderer.frame_present(hw, renderers[renderer_index]);

}

static void hw_frame_render(hw* hw)
{
   void** renderers = hw->renderer.backends;
   const u32 renderer_index = hw->renderer.renderer_index;

   if(!hw->renderer.frame_render)
      return;

   if(hw->renderer.window.width == 0 || hw->renderer.window.height == 0)
      return;

   pre(renderer_index < RENDERER_COUNT);
   hw->renderer.frame_render(hw, renderers[renderer_index], &hw->state);
}

static void hw_log(hw* hw, s8 message, ...)
{
   hw->window_title(hw, message);
}

void hw_event_loop_start(hw* hw, void (*app_frame_function)(arena scratch, app_state* state), void (*app_input_function)(struct app_state* state))
{
   clock_query_frequency();

   f32 altitude = PI / 8.f;
   //f32 altitude = 0;
   f32 azimuth = PI / 2.f; // 1/4 turn to align camera in -z
   //f32 azimuth = 0;
   vec3 origin = {0, 0, 0};
   app_camera_reset(&hw->state.camera, origin, 50.f, altitude, azimuth);

   i64 fps_log_counter = (i64)(clock_time_to_counter(.5f) + .5f);

   i64 begin = clock_query_counter();
   i64 fps_counter = begin;
   for (;;)
   {
      if (!hw->platform_loop())
         break;

      hw->state.camera.viewplane_width = hw->renderer.window.width;
      hw->state.camera.viewplane_height = hw->renderer.window.height;

      app_input_function(&hw->state);
      app_frame_function(hw->vk_storage, &hw->state);

      hw_frame_render(hw);
      // sync to defined frame rate
      hw_frame_sync(hw, 0.01666666666666666666666666666667 / 1);

      i64 end = clock_query_counter();

      hw_frame_present(hw);

      hw->state.frame_delta_in_seconds = clock_seconds_elapsed(begin, end);

      fps_counter += (end - begin);

      begin = end;

      if(fps_counter >= fps_log_counter)
      {
         //hw->window_title(hw, s8("FPS: %u; 'A': world axis"), (u32)((1.f / hw->state.frame_delta_in_seconds) + .5f));
         fps_counter= 0;
      }
   }
}

#if 0

#define MAX_ARGV 32
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
