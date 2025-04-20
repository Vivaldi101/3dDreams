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
   static char temp[1024*4];
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
   hw_renderer* renderer = (hw_renderer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

   switch(umsg)
   {
      case WM_CREATE:
      {
         CREATESTRUCT* pCreate = (CREATESTRUCT*)(lparam);
         int width = pCreate->cx;
         int height = pCreate->cy;

         if(renderer)
         {
            renderer->window.width = width;
            renderer->window.height = height;
            renderer->frame_resize(renderer->backends[renderer->renderer_index], renderer->window.width, renderer->window.height);
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
         if(renderer)
         {
            renderer->window.width = LOWORD(lparam);
            renderer->window.height = HIWORD(lparam);
            renderer->frame_resize(renderer->backends[renderer->renderer_index], renderer->window.width, renderer->window.height);
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
         if(renderer)
            renderer->frame_resize(renderer->backends[renderer->renderer_index], width, height);

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
typedef VOID(*VirtualReleasePtr)(LPVOID, SIZE_T);
static VirtualAllocPtr global_allocate = 0;
static VirtualReleasePtr global_release = 0;

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

static bool hw_is_virtual_memory_commited(void* address)
{
   MEMORY_BASIC_INFORMATION mbi;
   if(VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
      return false;

   return mbi.State == MEM_COMMIT;
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

	global_release(address, size);
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
   global_release = (VirtualReleasePtr)(GetProcAddress(hkernel32, "VirtualFree"));

   post(global_allocate);
   post(global_release);
}

static arena arena_new(size cap)
{
   arena a = {}; // stub arena
   if(cap <= 0)
      return a;

   void* base = hw_virtual_memory_reserve(cap);
   hw_virtual_memory_commit(base, cap);

   // set the base pointer and size on success
   a.beg = base;
   a.end = a.beg ? a.beg + cap : 0;

   return a;
}

static void arena_free(arena* a)
{
   hw_virtual_memory_release(a->beg, arena_size(a));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow)
{
   const size virtual_memory_amount = default_arena_size;
   const char** argv = 0;
   int argc = 0;
   hw hw = {0};

   hw_virtual_memory_init();

   // TODO: Pool these
   arena base_storage = hw.vk_storage = arena_new(virtual_memory_amount);
   hw.vk_scratch = arena_new(virtual_memory_amount);
   hw.misc_storage = arena_new(virtual_memory_amount);
   argv = cmd_parse(&hw.misc_storage, lpszCmdLine, &argc);

   hw.renderer.window.open = win32_window_open;
   hw.renderer.window.close = win32_window_close;
   hw.renderer.window_surface_create = window_surface_create;

   hw.timer.sleep = win32_sleep;
   hw.timer.time = win32_time;

   hw.platform_loop = win32_platform_loop;

   timeBeginPeriod(1);
   app_start(argc, argv, &hw);
   timeEndPeriod(1);

   arena_free(&base_storage);
   arena_free(&hw.vk_scratch);
   arena_free(&hw.misc_storage);

   return 0;
}

