#include "hw.h"
#include "app.h"
#include "graphics.h"
#include "vulkan_ng.h"
#include "arena.h"

#define FULLSCREEN 0

static void app_frame(arena scratch, app_state* state)
{
   // ...
}

static void app_camera_update(app_state* state)
{
   // Spherical camera
   f32 radius = state->camera.radius;

   // half turn across view plane extents (in azimuth)
   f32 speed_x = PI / state->camera.viewplane_width;
   f32 speed_y = PI / state->camera.viewplane_height;

   // delta in pixels
   f32 delta_x = (f32)state->input.mouse_pos[0] - (f32)state->input.mouse_prev_pos[0];
   f32 delta_y = (f32)state->input.mouse_pos[1] - (f32)state->input.mouse_prev_pos[1];

   if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_UP)
   {
      radius -= 0.25f;
      state->input.mouse_wheel_state = 0;
   }
   else if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_DOWN)
   {
      radius += 0.25f;
      state->input.mouse_wheel_state = 0;
   }

   // rads = rads/pixels * pixels
   if(state->input.mouse_buttons & (MOUSE_BUTTON_STATE_LEFT | MOUSE_BUTTON_STATE_RIGHT))
   {
      const f32 max_altitude = PI / 2.0f - 0.01f;

      state->camera.azimuth += speed_x * delta_x;
      state->camera.altitude += speed_y * delta_y;

      state->camera.altitude > max_altitude ? state->camera.altitude = max_altitude : state->camera.altitude;
      state->camera.altitude < -max_altitude ? state->camera.altitude = -max_altitude : state->camera.altitude;
   }

   f32 azimuth = state->camera.azimuth;
   f32 altitude = state->camera.altitude;

   f32 x = radius * cosf(altitude) * cosf(azimuth);
   f32 z = radius * cosf(altitude) * sinf(azimuth);
   f32 y = radius * sinf(altitude);
   vec3 eye = {x, y, z};
   vec3 origin = {0.f, 0.f, 0.f};

   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_MIDDLE)
   {
      // TODO: base on scene size
      f32 move_speed = 0.01f;
      vec3 dir = state->camera.dir;

      vec3_normalize(dir);
      dir = vec3_scale(&dir, move_speed);

      state->camera.pos = vec3_add(&state->camera.pos, &dir);
   }
   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_LEFT)
   {
      state->camera.pos = eye;
      state->camera.dir = vec3_sub(&eye, &origin);

      vec3_normalize(state->camera.dir);
   }
   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_RIGHT)
   {
      state->camera.dir.x += -x;
      state->camera.dir.y += -y;
      state->camera.dir.z += -z;

      vec3_normalize(state->camera.dir);
   }

   state->input.mouse_prev_pos[0] = state->input.mouse_pos[0];
   state->input.mouse_prev_pos[1] = state->input.mouse_pos[1];
   state->camera.radius = radius;

}

static void app_input_handle(app_state* state)
{
   app_camera_update(state);

   if(state->input.key == 'R' && state->input.key_state == KEY_STATE_UP)
   {
      state->input.key_state = 0;
      state->rtx_enabled = !state->rtx_enabled;
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
      hw_message_box("Could not initialize Vulkan");
   }

   hw_event_loop_start(hw, app_frame, app_input_handle);
   hw_window_close(hw);

	if(!vk_uninitialize(hw))
      hw_message_box("Could not uninitialize Vulkan");
}
