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
   u64 key;
   i32 pos[2], i;
   // TODO: proper rotation here
   f32 speed = 0.5f;
   if(state->input.key == 'W')
      state->camera_pos.z -= speed;
   else if(state->input.key == 'S')
      state->camera_pos.z += speed;
   if(state->input.key == 'A')
      state->camera_pos.x -= speed;
   else if(state->input.key == 'D')
      state->camera_pos.x += speed;
   else if(state->input.key == 'Z')
      state->camera_pos.y -= speed;
   else if(state->input.key == 'C')
      state->camera_pos.y += speed;

   state->input.key = 0;
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
