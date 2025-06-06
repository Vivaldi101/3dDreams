#if !defined(_APP_H)
#define _APP_H

#include "graphics.h"

align_struct app_input
{
   hw_input_type input_type;
   union { i32 pos[2]; u64 key; } ;
} app_input;

align_struct app_state
{
   mat4 proj;
   mat4 view;
   vec3 camera_pos;
   app_input input;  // add pointer if the structures get too large
} app_state;

void app_start(int argc, const char** argv, hw* hw);

#endif
