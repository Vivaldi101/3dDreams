#include <Windows.h>

#include "common.h"
#include "arena.h"

align_struct hw_window
{
   HWND(*open)(const char* title, int x, int y, int width, int height);
   void (*close)(struct hw_window window);
   u32 width, height;
   HWND handle;
} hw_window;

static void debug_message(const char* format, ...)
{
   static char temp[1 << 12];
   assert(strlen(format)+1 <= array_count(temp));

   va_list args;
   va_start(args, format);

   wvsprintfA(temp, format, args);

   va_end(args);
   OutputDebugStringA(temp);
}

#include "hw.c"

static void win32_sleep(u32 ms)
{
   Sleep(ms);
}

static u32 win32_time()
{
   static DWORD sys_time_base = 0;
   if(sys_time_base == 0) sys_time_base = timeGetTime();
   return timeGetTime() - sys_time_base;
}

#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

static void win32_log(hw* hw, s8 message, ...)
{
   static char buffer[512];

   va_list args;
   va_start(args, message);

   vsprintf_s(buffer, sizeof(buffer), (const char*)message.data, args);

   SetWindowText(hw->renderer.window.handle, buffer);

   va_end(args);
}

static bool win32_platform_loop()
{
   MSG msg;
   if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
   {
      if(msg.message == WM_QUIT)
         return false;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }

   return true;
}

static LRESULT CALLBACK win32_win_proc(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam) {
   hw* win32_hw = (hw*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

   switch(umsg)
   {
      case WM_CREATE:
      {
         CREATESTRUCT* pCreate = (CREATESTRUCT*)(lparam);
         int width = pCreate->cx;
         int height = pCreate->cy;

         if(win32_hw)
         {
            win32_hw->renderer.window.width = width;
            win32_hw->renderer.window.height = height;
            win32_hw->renderer.frame_resize(win32_hw, win32_hw->renderer.window.width, win32_hw->renderer.window.height);
         }

         return 0;
      }

      case WM_CLOSE:
         PostQuitMessage(0);
         return 0;

      case WM_ERASEBKGND:
         return 1; // prevent Windows from clearing the background with white

      case WM_PAINT:
      {
         PAINTSTRUCT ps;
         BeginPaint(hwnd, &ps);
         EndPaint(hwnd, &ps);
         return 0;
      }

      case WM_SIZE:
      {
         if(win32_hw)
         {
            win32_hw->renderer.window.width = LOWORD(lparam);
            win32_hw->renderer.window.height = HIWORD(lparam);
            win32_hw->renderer.frame_resize(win32_hw, win32_hw->renderer.window.width, win32_hw->renderer.window.height);
         }
         return 0;
      }

      case WM_DISPLAYCHANGE:
      {
         InvalidateRect(hwnd, NULL, FALSE);
         UpdateWindow(hwnd);

         RECT rect;
         GetClientRect(hwnd, &rect);
         int width = rect.right - rect.left;
         int height = rect.bottom - rect.top;
         if(win32_hw)
            win32_hw->renderer.frame_resize(win32_hw, width, height);

         return 0;
      }
   }

   return DefWindowProc(hwnd, umsg, wparam, lparam);
}

static HWND win32_window_open(const char* title, int x, int y, int width, int height)
{
   HWND result;

   RECT winrect;
   DWORD dwExStyle, dwStyle;
   WNDCLASS wc;

   wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
   wc.lpfnWndProc = win32_win_proc;
   wc.cbClsExtra = 0;
   wc.cbWndExtra = 0;
   wc.hInstance = GetModuleHandle(NULL);
   wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
   wc.hCursor = LoadCursor(NULL, IDC_ARROW);
   wc.hbrBackground = NULL;
   wc.lpszMenuName = NULL;
   wc.lpszClassName = title;

   if(!RegisterClass(&wc))
      return 0;

   dwStyle = WS_OVERLAPPEDWINDOW;
   dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

   winrect.left = x;
   winrect.right = x + width;
   winrect.top = y;
   winrect.bottom = y + height;

   AdjustWindowRectEx(&winrect, dwStyle, 0, dwExStyle);

   result = CreateWindowEx(dwExStyle,
      wc.lpszClassName, title, WS_CLIPSIBLINGS | WS_CLIPCHILDREN | dwStyle,
      winrect.left, winrect.top, winrect.right - winrect.left, winrect.bottom - winrect.top, NULL, NULL, wc.hInstance, NULL);

   if(!result)
      return 0;

   ShowWindow(result, SW_SHOW);
   SetForegroundWindow(result);
   SetFocus(result);
   UpdateWindow(result);

   return result;
}

static void win32_window_close(hw_window window)
{
   PostMessage(window.handle, WM_QUIT, 0, 0L);
}

static void win32_abort(u32 code)
{
   ExitProcess(code);
}

#define hw_error(m) MessageBox(NULL, (m), "Engine", MB_OK | MB_ICONSTOP | MB_SYSTEMMODAL);

typedef LPVOID(*VirtualAllocPtr)(LPVOID, SIZE_T, DWORD, DWORD);
typedef BOOL(*VirtualFreePtr)(LPVOID, SIZE_T, DWORD);
static VirtualAllocPtr global_allocate = 0;
static VirtualFreePtr global_free = 0;

static void hw_global_reserve_available()
{
   MEMORYSTATUSEX memory_status;
   memory_status.dwLength = sizeof(memory_status);

   GlobalMemoryStatusEx(&memory_status);

   global_allocate(0, memory_status.ullAvailPhys, MEM_RESERVE, PAGE_READWRITE);
}

static bool hw_is_virtual_memory_reserved(void* address)
{
   MEMORY_BASIC_INFORMATION mbi;
   if(VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
      return false;

   return mbi.State == MEM_RESERVE;
}

static void* hw_virtual_memory_reserve(usize size)
{
	// let the os decide into what address to place the reserve
   return global_allocate(0, size, MEM_RESERVE, PAGE_NOACCESS);
}

static void hw_virtual_memory_commit(void* address, usize size)
{
	pre(hw_is_virtual_memory_reserved((byte*)address+size-1));
	pre(!hw_is_virtual_memory_commited((byte*)address+size-1));

	// commit the reserved address range
   global_allocate(address, size, MEM_COMMIT, PAGE_READWRITE);
}

static void hw_virtual_memory_release(void* address, usize size)
{
	pre(hw_is_virtual_memory_commited((byte*)address+size-1));

	global_free(address, 0, MEM_RELEASE);
}

static void hw_virtual_memory_decommit(void* address, usize size)
{
	pre(hw_is_virtual_memory_commited((byte*)address+size-1));

	global_allocate(address, size, MEM_DECOMMIT, PAGE_READWRITE);
}

static void hw_virtual_memory_init()
{
   typedef LPVOID(*VirtualAllocPtr)(LPVOID, usize, DWORD, DWORD);

   HMODULE hkernel32 = GetModuleHandleA("kernel32.dll");

   inv(hkernel32);

   global_allocate = (VirtualAllocPtr)(GetProcAddress(hkernel32, "VirtualAlloc"));
   global_free = (VirtualFreePtr)(GetProcAddress(hkernel32, "VirtualFree"));

   post(global_allocate);
   post(global_free);
}

static void arena_free(arena* a)
{
   hw_virtual_memory_release(a->beg, arena_left(a));
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show)
{
   const char** argv = 0;
   int argc = 0;
   hw hw = {0};

   hw_virtual_memory_init();

   size arena_size = 1ull << 46;
   void* base = global_allocate(0, arena_size, MEM_RESERVE, PAGE_READWRITE);
   assert(base);

   arena base_arena = {};
   base_arena.end = base;

   //size total_arena_size = MB(2);
   size total_arena_size = KB(256);
   arena base_storage = arena_new(&base_arena, total_arena_size);
   assert(arena_left(&base_storage) == total_arena_size);

   hw.vk_storage = base_storage;

   hw.renderer.window.open = win32_window_open;
   hw.renderer.window.close = win32_window_close;
   hw.renderer.window_surface_create = window_surface_create;

   hw.timer.sleep = win32_sleep;
   hw.timer.time = win32_time;

   hw.platform_loop = win32_platform_loop;

   hw.log = win32_log;

   timeBeginPeriod(1);
   app_start(argc, argv, &hw);
   timeEndPeriod(1);

   bool gr = global_free(base, 0, MEM_RELEASE);
   assert(gr);

   return 0;
}

