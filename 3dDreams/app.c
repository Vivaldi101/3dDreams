#include "hw.h"
#include "app.h"
#include "graphics.h"
#include "vulkan_ng.h"
#include "arena.h"

#define FULLSCREEN 0

static void app_frame(arena scratch, app_state* state)
{
}

static void app_input_handle(app_input* input)
{
   u64 key;
   i32 pos[2], i;
   if(input->input_type == HW_INPUT_TYPE_MOUSE)
      if(input->pos[0] > 0 && input->pos[1] > 0)
         for(i = 0; i < 2; ++i)
            pos[i] = input->pos[i];
   if(input->input_type == HW_INPUT_TYPE_KEY)
      key = input->key;
}

void app_start(int argc, const char** argv, hw* hw)
{
   pre(implies(argc > 0, argv[argc - 1]));

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
