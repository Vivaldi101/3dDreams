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
   static f32 radius = 1.0f;
   if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_UP)
   {
      radius -= 0.1f;
      state->input.mouse_wheel_state = 0;
   }
   else if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_DOWN)
   {
      radius += 0.1f;
      state->input.mouse_wheel_state = 0;
   }

   if(state->input.mouse_dragged)
   {
      f32 speed = 0.005f;

      f32 delta_x = (f32)state->input.mouse_pos[0] - (f32)state->input.mouse_prev_pos[0];
      f32 delta_y = (f32)state->input.mouse_pos[1] - (f32)state->input.mouse_prev_pos[1];

      // Update azimuth and altitude
      state->camera.azimuth += delta_x * speed;
      state->camera.altitude += delta_y * speed;
      state->input.mouse_dragged = false;
   }

   f32 azimuth = state->camera.azimuth;
   f32 altitude = state->camera.altitude;

   f32 x = radius * cosf(altitude) * cosf(azimuth);    // right
   f32 z = -radius * cosf(altitude) * sinf(azimuth);   // forward = -z
   f32 y = -radius * sinf(altitude);                   // up

   vec3 eye = {x, y, z};
   state->camera.pos = eye;

   state->input.mouse_prev_pos[0] = state->input.mouse_pos[0];
   state->input.mouse_prev_pos[1] = state->input.mouse_pos[1];
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
