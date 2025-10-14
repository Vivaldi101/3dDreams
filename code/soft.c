#include "common.h"

void soft_rasterizer_initialize(hw* hw)
{
   // TODO: Move these into soft.c for software rasterizing
#if 0
   PAINTSTRUCT ps;

   int i, remap;
   COLORREF cr, cr2;
   BYTE r, g, b;
   hw_mem = CreateCompatibleDC(BeginPaint(hw_wnd, &ps));
   if ((GetDeviceCaps(ps.hdc, PLANES)) != 1)
      hw_error("(Hardware) Direct RGB color expected.");

   hw_pixel_size = GetDeviceCaps(ps.hdc, BITSPIXEL) / 8;
   if ((hw_pixel_size != 2) && (hw_pixel_size != 3) && (hw_pixel_size != 4))
      hw_error("(Hardware) 16bpp, 24bpp or 32bpp expected.");

   G_init_graphics();

   hw_bmp = CreateCompatibleBitmap(ps.hdc, hw_screen_x_size, hw_screen_y_size);
   SelectObject(hw_mem, hw_bmp);

   cr2 = 0;                                     /* ugly way of doing something */
   hw_red_mask = hw_green_mask = hw_blue_mask = 0;  /* trivial. Windows don't */
   for (i = 1; i < 256; i++)                        /* have any service to report */
   {                                          /* pixel bit layout */
      cr = SetPixel(hw_mem, 0, 0, RGB(i, i, i));
      if (GetRValue(cr) != GetRValue(cr2)) hw_red_mask++;
      if (GetGValue(cr) != GetGValue(cr2)) hw_green_mask++;
      if (GetBValue(cr) != GetBValue(cr2)) hw_blue_mask++;
      cr2 = cr;                                   /* masks - which bits are masked */
   }                                          /* by every color */
   hw_red_size = hw_green_size = hw_blue_size = 0;
   for (i = 0; i < 8; i++)                           /* finding how many bits */
   {
      if (hw_red_mask >> i) hw_red_size++;
      if (hw_green_mask >> i) hw_green_size++;
      if (hw_blue_mask >> i) hw_blue_size++;
   }
   hw_red_shift = hw_green_size + hw_blue_size;   /* finding how to pack colors */
   hw_green_shift = hw_blue_size;               /* into a pixel */
   hw_blue_shift = 0;

   EndPaint(hw_wnd, &ps);

   hw_rect.left = hw_rect.top = 0;
   hw_rect.right = hw_screen_x_size;
   hw_rect.bottom = hw_screen_y_size;

#endif
}

// TODO: Used by the software renderer
#if 0
static void hw_blit(hw* hw)
{
   PAINTSTRUCT ps;

   BeginPaint(hw->renderer.window.handle, &ps);                    /* store into a bitmap */
   SetMapMode(ps.hdc, MM_TEXT);                             /* blit a bitmap */
   //SetBitmapBits(hw_bmp,hw_image_size*hw_pixel_size,(void*)G_c_buffer);
   //BitBlt(ps.hdc,0,0,hw_screen_x_size,hw_screen_y_size,hw_mem,0,0,SRCCOPY);
   EndPaint(hw->renderer.window.handle, &ps);
}
#endif
