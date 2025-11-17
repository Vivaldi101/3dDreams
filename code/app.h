#if !defined(_APP_H)
#define _APP_H

#include "math.h"

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

enum
{
   KEY_STATE_DOWN = 1 << 0,
   KEY_STATE_UP = 1 << 1,
   KEY_STATE_RELEASED = 1 << 2,
   KEY_STATE_REPEATING = 1 << 4
};

typedef enum { HW_INPUT_TYPE_KEY, HW_INPUT_TYPE_MOUSE, HW_INPUT_TYPE_TOUCH } hw_input_type;

align_struct app_input
{
   hw_input_type input_type;
   u32 mouse_pos[2];
   u32 mouse_prev_pos[2];
   u32 mouse_buttons;
   u32 mouse_wheel_state;
   u64 key;
   u32 key_state;
} app_input;

align_struct app_camera
{
   vec3 eye;
   vec3 origin;
   vec3 dir;
   f32 target_azimuth;
   f32 target_altitude;
   f32 smoothed_azimuth;
   f32 smoothed_altitude;
   f32 target_radius;
   f32 smoothed_radius;
   u32 viewplane_width;
   u32 viewplane_height;
} app_camera;

align_struct app_state
{
   mat4 proj;
   mat4 view;
   app_input input;
   app_camera camera;
   s8 asset_file;  // TODO: for testing
   f64 frame_delta_in_seconds;
   bool is_fullscreen;
   bool is_raytracing; // TODO: replace with just rtx boolean
   bool is_mesh_shading;
   bool draw_axis;
} app_state;

typedef struct hw hw;
void app_start(hw* hw, s8 asset_file);
void app_camera_reset(app_camera* camera, vec3 origin, f32 radius, f32 altitude, f32 azimuth);

#endif
