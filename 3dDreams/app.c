#include "hw.h"
#include "app.h"
#include "graphics.h"
#include "vulkan_ng.h"
#include "arena.h"

#define FULLSCREEN 0

static void app_frame(arena scratch, app_state* state)
{
}

static void app_input_handle(app_state* state)
{
   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_LEFT)
   {
      f32 speed = 1.0f;
      f32 radius = 1.0f;

      // TODO: normalize into pi and pi/2
      f32 azimuth = (f32)state->input.mouse_pos[0];
      f32 altitude = (f32)state->input.mouse_pos[1];

      // TODO: get the client area
      f32 w = 800.0f;   // TODO: for testing
      f32 h = 600.0f;   // TODO: for testing

      azimuth = clamp(azimuth, 0.f, w-1);
      altitude = clamp(altitude, 0.f, h-1);

      assert(0.0f <= azimuth && azimuth < w);
      assert(0.0f <= altitude && altitude < h);

      f32 aw = 2.f / (w - 1);
      f32 ah = 2.f / (h - 1);
      f32 b = -1.f;

      azimuth = aw * azimuth + b;
      altitude = ah * altitude + b;

      assert(-1.f <= azimuth && azimuth <= 1.f);
      assert(-1.f <= altitude && altitude <= 1.f);

      f32 x = radius * cosf(altitude) * cosf(azimuth);    // right
      f32 z = -radius * cosf(altitude) * sinf(azimuth);   // forward = -z
      f32 y = -radius * sinf(altitude);                   // up

      vec3 eye = {x, y, z};
      state->camera.pos = eye;
   }
}

void app_start(int argc, const char** argv, hw* hw)
{
   pre(implies(argc > 0, argv[argc - 1]));

   // TODO: key press to toggle
#if FULLSCREEN
	int w = 1920, h = 1080;
	int x = 0, y = 0;
#else
	int w = 800, h = 600;
	int x = 100, y = 100;
#endif

   hw_window_open(hw, "Vulkan App", x, y, w, h);

   if(!vk_initialize(hw))
   {
      hw_window_close(hw);
      hw_message("Could not initialize Vulkan");
   }

   hw_event_loop_start(hw, app_frame, app_input_handle);
   hw_window_close(hw);

	if(!vk_uninitialize(hw))
      hw_message("Could not uninitialize Vulkan");
}
