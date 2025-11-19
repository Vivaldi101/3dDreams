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
	assert(hw->renderer.window.handle);
   hw->renderer.window.close(hw->renderer.window);
}

static f64 global_game_time_residual;
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
   const f64 counter_delta = clock_time_to_counter(frame_delta);

   for(;;)
   {
      const i64 current_counter = clock_query_counter();
      static i64 last_counter = 0;
      if(last_counter == 0)
         last_counter = current_counter;

      i64 delta_counter = current_counter - last_counter;
      last_counter = current_counter;

      global_game_time_residual += delta_counter;

      assert(global_game_time_residual >= 0);

      for(;;)
      {
         // how much to wait before running the next frame
         if(global_game_time_residual < counter_delta)
            break;
         global_game_time_residual -= counter_delta;
         num_frames_to_run++;
      }
      if(num_frames_to_run > 0)
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

   assert(renderer_index < RENDERER_COUNT);
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

   assert(renderer_index < RENDERER_COUNT);
   hw->renderer.frame_render(hw, renderers[renderer_index], &hw->state);
}

static void hw_log(hw* hw, s8 message, ...)
{
   hw->window_title(hw, message);
}

void hw_event_loop_start(hw* hw, void (*app_frame_function)(arena scratch, app_state* state), void (*app_input_function)(struct app_state* state))
{
   clock_query_frequency();

   f32 altitude = PI / 10.f;
   f32 azimuth = PI * 2.f;
   vec3 origin = {0, 0, 0};
   app_camera_reset(&hw->state.camera, origin, 1.0f, altitude, azimuth);

   f64 log_delta = clock_time_to_counter(.5f);

   i64 begin = clock_query_counter();
   i64 fps_counter = begin;
   for (;;)
   {
      if (!hw->platform_loop())
         break;

      hw->state.camera.viewplane_width = hw->renderer.window.width;
      hw->state.camera.viewplane_height = hw->renderer.window.height;

      app_input_function(&hw->state);
      app_frame_function(hw->scratch, &hw->state);

      hw_frame_render(hw);
      // sync to defined frame rate
      hw_frame_sync(hw, 0.01666666666666666666666666666667);

      i64 end = clock_query_counter();

      hw_frame_present(hw);

      hw->state.frame_delta_in_seconds = clock_seconds_elapsed(begin, end);

      fps_counter += (end - begin);

      begin = end;

      if(fps_counter >= log_delta)
      {
         // TODO: log gpu timings here aswell
         #if 0
         u64 query_results[2] = {0};
         vkGetQueryPoolResults(context->devices.logical, context->query_pool, 0, array_count(query_results), sizeof(query_results), query_results, sizeof(query_results[0]), VK_QUERY_RESULT_64_BIT);

         f64 gpu_begin = (f64)(query_results[0]) * context->time_period;
         f64 gpu_end = (f64)(query_results[1]) * context->time_period;

         static i64 cpu_begin = 0;
         static i64 cpu_counter = 0;
         static f64 log_time = 0;
         if(cpu_begin == 0)
            cpu_begin = hw->timer.time();

         if(cpu_counter == 0)
            cpu_counter = hw->timer.time();

         if(log_time == 0)
            log_time = hw->timer.time_to_counter(.5f);

         i64 cpu_end = hw->timer.time();

         f64 frame_delta_ms = hw->timer.seconds_elapsed(cpu_begin, cpu_end) * 1000.f;

         cpu_begin = cpu_end;

         if(cpu_end - cpu_counter > log_time)
         {
            const f64 ms = 1e6;
            cpu_counter = cpu_end;
            const f64 gpu_delta = max(gpu_end - gpu_begin, 0.f);
            // frame logs
            // TODO: this should really be in app.c
            if(hw->state.is_mesh_shading)
               hw->window_title(hw, s8("cpu: %.2f ms; gpu: %.2f ms; #Meshlets: %u; Hold 'a' to show world axis; Press 'm' to toggle RTX; RTX ON"), frame_delta_ms, gpu_delta / ms, context->meshlets.count);
            else
               hw->window_title(hw, s8("cpu: %.2f ms; gpu: %.2f ms; #Meshlets: 0; Hold 'a' to show world axis; Press 'm' to toggle RTX; RTX OFF"), frame_delta_ms, gpu_delta / ms);
         }
         #endif

         hw->window_title(hw, s8("cpu: %.2f ms"), hw->state.frame_delta_in_seconds * 1000.f);

         fps_counter = 0;
      }
   }
}
