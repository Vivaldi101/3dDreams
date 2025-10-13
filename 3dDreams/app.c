#define _CRT_SECURE_NO_WARNINGS 1

#include "hw.h"
#include "app.h"
#include "math.h"
#include "arena.h"

#include <volk.c>
#include "vulkan_ng.c"

static void app_frame(arena scratch, app_state* state)
{
   // per app frame drawing
}

static void app_camera_update(app_state* state)
{
   const f32 smoothing_factor = 1.0f - powf(0.08f, (f32)state->frame_delta_in_seconds);

   // half turn across view plane extents (in azimuth)
   f32 rotation_speed_x = (2.f*PI) / state->camera.viewplane_width;
   f32 rotation_speed_y = (2.f*PI) / state->camera.viewplane_height;

   // delta in pixels
   f32 delta_x = (f32)state->input.mouse_pos[0] - (f32)state->input.mouse_prev_pos[0];
   f32 delta_y = (f32)state->input.mouse_pos[1] - (f32)state->input.mouse_prev_pos[1];

   f32 zoom_speed = 5.f;

   if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_UP)
   {
      // closer radius
      state->camera.target_radius -= zoom_speed;
      state->input.mouse_wheel_state = 0;
      // prevent flipping
      state->camera.target_radius = max(state->camera.target_radius, 1.0f);
   }
   else if(state->input.mouse_wheel_state & MOUSE_WHEEL_STATE_DOWN)
   {
      // further radius
      state->camera.target_radius += zoom_speed;
      state->input.mouse_wheel_state = 0;
   }

   // rotating input affects target azimuth and altitude
   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_LEFT)
   {
      state->camera.target_azimuth += rotation_speed_x * delta_x;
      state->camera.target_altitude += rotation_speed_y * delta_y;

      const f32 max_altitude = PI / 2.0f - 0.01f;
      if(state->camera.target_altitude > max_altitude) state->camera.target_altitude = max_altitude;
      if(state->camera.target_altitude < -max_altitude) state->camera.target_altitude = -max_altitude;

      // dont go below the ground plane
      //state->camera.target_altitude = max(state->camera.target_altitude, deg2rad(5.f));
   }

   // smooth damping
   state->camera.smoothed_azimuth += (state->camera.target_azimuth - state->camera.smoothed_azimuth) * smoothing_factor;
   state->camera.smoothed_altitude += (state->camera.target_altitude - state->camera.smoothed_altitude) * smoothing_factor;
   state->camera.smoothed_radius += (state->camera.target_radius - state->camera.smoothed_radius) * smoothing_factor;

   // use smoothed values for position
   f32 azimuth = state->camera.smoothed_azimuth;
   f32 altitude = state->camera.smoothed_altitude;
   f32 radius = state->camera.smoothed_radius;

   vec3 origin = state->camera.origin;

   f32 x = radius * cosf(altitude) * cosf(azimuth) + origin.x;
   f32 z = radius * cosf(altitude) * sinf(azimuth) + origin.z;
   f32 y = radius * sinf(altitude) + origin.y;

   vec3 eye = {x, y, z};

   state->camera.eye = eye;

   vec3 orbit_dir = vec3_sub(&eye, &origin);
   vec3_normalize(orbit_dir);

   if(state->input.mouse_buttons & MOUSE_BUTTON_STATE_RIGHT)
   {
      vec3 xz = {};
      vec3 up = {0, 1, 0};

      vec3_cross(orbit_dir, up, xz);
      vec3_normalize(xz);

      xz = vec3_scale(&xz, delta_x);
      up = vec3_scale(&up, delta_y);

      xz = vec3_scale(&xz, smoothing_factor);
      up = vec3_scale(&up, smoothing_factor);

      state->camera.origin = vec3_sub(&xz, &state->camera.origin);
      state->camera.origin = vec3_add(&up, &state->camera.origin);
   }

   state->camera.dir = orbit_dir;

   // update previous mouse position and store latest radius
   state->input.mouse_prev_pos[0] = state->input.mouse_pos[0];
   state->input.mouse_prev_pos[1] = state->input.mouse_pos[1];
   state->camera.smoothed_radius = radius;
}

void app_camera_reset(app_camera* camera, vec3 origin, f32 radius, f32 altitude, f32 azimuth)
{
   f32 x = radius * cosf(altitude) * cosf(azimuth) + origin.x;
   f32 z = radius * cosf(altitude) * sinf(azimuth) + origin.z;
   f32 y = radius * sinf(altitude) + origin.y;
   vec3 eye = {x, y, z};

   memset(camera, 0, sizeof(app_camera));

   camera->origin = origin;
   camera->eye = eye;
   camera->dir = vec3_sub(&eye, &origin);
   camera->smoothed_radius = radius;
   camera->target_radius = radius;

   camera->smoothed_azimuth = azimuth;
   camera->target_azimuth = azimuth;
   camera->smoothed_altitude = altitude;
   camera->target_altitude = altitude;
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
   if(state->input.key == 'S' && state->input.key_state == KEY_STATE_UP)
   {
      state->input.key_state = 0;
      f32 altitude = PI / 8.f;
      f32 azimuth = PI / 2.f; // 1/4 turn to align camera in -z
      vec3 origin = {};
      app_camera_reset(&state->camera, origin, 50.f, altitude, azimuth);
   }
}

void app_start(int argc, const char** argv, hw* hw)
{
   assert(implies(argc > 0, argv[argc - 1]));
   assert(hw);

	int w = 800, h = 600;
	int x = 100, y = 100;

   hw_window_open(hw, "Vulkan App", x, y, w, h);

   hw->state.asset_file = s8("lantern/lantern.gltf");
   //hw->state.asset_file = s8("damagedhelmet/damagedhelmet.gltf");
   //hw->state.asset_file = s8("glamvelvetsofa/glamvelvetsofa.gltf");

   if(!vk_initialize(hw))
   {
      printf("Could not initialize all the required subsystems for Vulkan backend\n");
      return;
   }

   hw_event_loop_start(hw, app_frame, app_input_handle);
   vk_uninitialize(hw);

   hw_window_close(hw);
}
