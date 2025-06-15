#if !defined(_APP_H)
#define _APP_H

#include "graphics.h"

enum 
{
   MOUSE_BUTTON_STATE_LEFT = 1 << 0,
   MOUSE_BUTTON_STATE_RIGHT = 1 << 1,
   MOUSE_BUTTON_STATE_MIDDLE = 1 << 2,
};

enum
{
   MOUSE_WHEEL_STATE_DOWN = 1 << 0,
   MOUSE_WHEEL_STATE_UP = 1 << 1,
};

align_struct app_input
{
   hw_input_type input_type;
   union { u32 mouse_pos[2]; u64 key; };
   u32 mouse_prev_pos[2];
   // TODO: add mouse buttons
   u32 mouse_buttons;   // bit flags
   u32 mouse_wheel_state;     // bit flags
   bool mouse_dragged;
} app_input;

align_struct app_camera
{
   vec3 pos;
   f32 azimuth;
   f32 altitude;
   f32 radius;
   u32 viewplane_width;
   u32 viewplane_height;
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
