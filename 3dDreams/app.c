#include "hw.h"
#include "app.h"
#include "graphics.h"
#include "vulkan_ng.h"
#include "arena.h"

//#define FULLSCREEN

typedef struct app_some_type
{
   bool isvalid;
   int arr[100];
   char* name;
} app_some_type;


static void app_frame_draw_all(arena scratch)
{
   // take all the scratch space left
   size scratch_count = scratch_left(scratch, app_some_type);

   for(size i = 0; i < scratch_count; ++i)
   {
      app_some_type* p = arena_offset(i, scratch, app_some_type);
      p->isvalid = true;
   }
}

static void app_frame(arena scratch)
{
   // first draw 12 types
   //app_frame_draw(arena_alloc(scratch, sizeof(app_some_type), 12));

   // then draw 2412312313 types(will fail)
   //app_frame_draw(arena_alloc(scratch, sizeof(app_some_type), 2412312313));

   // will draw all possible types remaining
   //app_frame_draw_all(scratch);
}

// do input handling
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

   g_frustum frustum = {0};

#ifdef FULLSCREEN
	int w = 1920, h = 1080;
	int x = 0, y = 0;
#else
	int w = 800, h = 600;
	int x = 100, y = 100;
#endif

   hw_window_open(hw, "App window1", x, y, w, h);

   if(!vk_initialize(hw))
   {
      hw_message("Could not initialize Vulkan");
      return;
   }

   hw_event_loop_start(hw, app_frame, app_input_handle);
   hw_window_close(hw);

	if(!vk_uninitialize(hw))
      hw_message("Could not uninitialize Vulkan");
}
