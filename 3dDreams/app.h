#if !defined(_APP_H)
#define _APP_H

#include "graphics.h"

enum 
{
   MOUSE_BUTTON_STATE_LEFT = 1 << 0,
   MOUSE_BUTTON_STATE_RIGHT = 1 << 1,
   MOUSE_BUTTON_STATE_MIDDLE = 1 << 2,
};

align_struct app_input
{
   hw_input_type input_type;
   union { u32 mouse_pos[2]; u64 key; };
   // TODO: add mouse buttons
   u32 mouse_buttons;   // bit flags
} app_input;

align_struct app_camera
{
   vec3 pos;
   f32 azimuth;
   f32 altitude;
} app_camera;

align_struct app_state
{
   mat4 proj;
   mat4 view;
   app_input input;
   app_camera camera;
} app_state;

void app_start(int argc, const char** argv, hw* hw);

#endif
