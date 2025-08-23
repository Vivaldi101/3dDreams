#define _CRT_SECURE_NO_WARNINGS 1

#include "hw.h"
#include "app.h"
#include "graphics.h"
#include "arena.h"

#include <volk.c>
#include "vulkan_ng.c"

static void app_frame(arena scratch, app_state* state)
{
   // ...
}

static void app_camera_update(app_state* state)
{
   const f32 smoothing_factor = 1.0f - powf(0.001f, state->frame_delta_in_seconds);

   // half turn across view plane extents (in azimuth)
   f32 rotation_speed_x = PI / state->camera.viewplane_width;
   f32 rotation_speed_y = PI / state->camera.viewplane_height;

   // delta in pixels
   f32 delta_x = (f32)state->input.mouse_pos[0] - (f32)state->input.mouse_prev_pos[0];
   f32 delta_y = (f32)state->input.mouse_pos[1] - (f32)state->input.mouse_prev_pos[1];

   f32 movement_speed = 0.5f;

   // Zooming input affects target radius
   if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_UP)
   {
      state->camera.target_radius -= movement_speed;
      state->input.mouse_wheel_state = 0;
   }
   else if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_DOWN)
   {
      state->camera.target_radius += movement_speed;
      state->input.mouse_wheel_state = 0;
   }

   // Rotating input affects target azimuth and altitude
   if(state->input.mouse_buttons & (MOUSE_BUTTON_STATE_LEFT | MOUSE_BUTTON_STATE_RIGHT))
   {
      state->camera.target_azimuth += rotation_speed_x * delta_x;
      state->camera.target_altitude += rotation_speed_y * delta_y;

      const f32 max_altitude = PI / 2.0f - 0.01f;
      if(state->camera.target_altitude > max_altitude) state->camera.target_altitude = max_altitude;
      if(state->camera.target_altitude < -max_altitude) state->camera.target_altitude = -max_altitude;
   }

   // Smooth damping
   state->camera.smoothed_azimuth += (state->camera.target_azimuth - state->camera.smoothed_azimuth) * smoothing_factor;
   state->camera.smoothed_altitude += (state->camera.target_altitude - state->camera.smoothed_altitude) * smoothing_factor;
   state->camera.smoothed_radius += (state->camera.target_radius - state->camera.smoothed_radius) * smoothing_factor;

   // Use smoothed values for position
   f32 azimuth = state->camera.smoothed_azimuth;
   f32 altitude = state->camera.smoothed_altitude;
   f32 radius = state->camera.smoothed_radius;

   f32 x = radius * cosf(altitude) * cosf(azimuth);
   f32 z = radius * cosf(altitude) * sinf(azimuth);
   f32 y = radius * sinf(altitude);
   vec3 eye = {x, y, z};
   vec3 origin = {0.f, 0.f, 0.f};

   // Left button updates position and direction (orbit behavior)
   state->camera.pos = eye;
   state->camera.dir = vec3_sub(&eye, &origin);
   vec3_normalize(state->camera.dir);

   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_RIGHT)
   {
      state->camera.dir.x += -x;
      state->camera.dir.y += -y;
      state->camera.dir.z += -z;
      vec3_normalize(state->camera.dir);
   }

   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_MIDDLE)
   {
      vec3 dir = state->camera.dir;
      vec3_normalize(dir);
      dir = vec3_scale(&dir, movement_speed);
      state->camera.pos = vec3_add(&state->camera.pos, &dir);
   }

   // Update previous mouse position and store latest radius
   state->input.mouse_prev_pos[0] = state->input.mouse_pos[0];
   state->input.mouse_prev_pos[1] = state->input.mouse_pos[1];
   state->camera.smoothed_radius = radius;  // Optional: only needed if something else uses this
}

static void app_input_handle(app_state* state)
{
   app_camera_update(state);

   if(state->input.key == 'R' && state->input.key_state == KEY_STATE_UP)
   {
      state->input.key_state = 0;
      state->rtx_enabled = !state->rtx_enabled;
   }
   if(state->input.key == 'F' && state->input.key_state == KEY_STATE_UP)
   {
      state->input.key_state = 0;
      state->is_fullscreen = !state->is_fullscreen;
   }
}

void app_start(int argc, const char** argv, hw* hw)
{
   pre(implies(argc > 0, argv[argc - 1]));

	int w = 800, h = 600;
	int x = 100, y = 100;

   hw_window_open(hw, "Vulkan App", x, y, w, h);

   hw->state.gltf_file = s8("glamvelvetsofa.gltf");

   // TODO: narrower init
   vk_initialize(hw);

   hw_event_loop_start(hw, app_frame, app_input_handle);
	vk_uninitialize(hw);

   hw_window_close(hw);
}
