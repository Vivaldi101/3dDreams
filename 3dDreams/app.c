#define _CRT_SECURE_NO_WARNINGS 1

#include "hw.h"
#include "app.h"
#include "graphics.h"
#include "arena.h"

#include <volk.c>
#include "vulkan_ng.c"

static void app_frame(arena scratch, app_state* state)
{
   // per app frame drawing
}

static void app_camera_update(app_state* state)
{
   const f32 smoothing_factor = 1.0f - powf(0.08f, state->frame_delta_in_seconds);

   // half turn across view plane extents (in azimuth)
   f32 rotation_speed_x = PI / state->camera.viewplane_width;
   f32 rotation_speed_y = PI / state->camera.viewplane_height;

   // delta in pixels
   f32 delta_x = (f32)state->input.mouse_pos[0] - (f32)state->input.mouse_prev_pos[0];
   f32 delta_y = (f32)state->input.mouse_pos[1] - (f32)state->input.mouse_prev_pos[1];

   f32 movement_speed = 0.5f;

   if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_UP)
   {
      // Closer radius
      state->camera.target_radius -= movement_speed;
      state->input.mouse_wheel_state = 0;
      state->camera.target_radius = max(state->camera.target_radius, 1.0f);
   }
   else if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_DOWN)
   {
      // Further radius
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

   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_RIGHT)
   {
      // TODO: Restore the previous camera state after FPS view
      // Fixed eye
      eye = state->camera.eye;
      // Rotate direction vector
      origin = (vec3){-x, -y, -z};

      // Stop smoothing in FPS view
      state->camera.smoothed_altitude = state->camera.target_altitude;
      state->camera.smoothed_azimuth = state->camera.target_azimuth;
   }

   state->camera.eye = eye;
   vec3 orbit_dir = vec3_sub(&eye, &origin);
   vec3_normalize(orbit_dir);

   state->camera.dir = orbit_dir;

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
      // TODO: Raymond chen like fullscreen toggle
      state->input.key_state = 0;
      state->is_fullscreen = !state->is_fullscreen;
   }
}

void app_start(int argc, const char** argv, hw* hw)
{
   assert(implies(argc > 0, argv[argc - 1]));
   assert(hw);

	int w = 800, h = 600;
	int x = 100, y = 100;

   hw_window_open(hw, "Vulkan App", x, y, w, h);

   hw->state.gltf_file = s8("damagedhelmet/damagedhelmet.gltf");
   //hw->state.gltf_file = s8("glamvelvetsofa/glamvelvetsofa.gltf");
   //hw->state.gltf_file = s8("lantern/lantern.gltf");
   //hw->state.gltf_file = s8("sponza/sponza.gltf");

   vk_initialize(hw);

   hw_event_loop_start(hw, app_frame, app_input_handle);
	vk_uninitialize(hw);

   hw_window_close(hw);
}
