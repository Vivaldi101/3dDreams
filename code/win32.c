#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000L
#   error "This code requires C23 or later"
#endif

#include <Windows.h>

#include "common.h"
#include "arena.h"
#include "stdio.h"

#include "hw.c"

static void win32_sleep(u32 ms)
{
   Sleep(ms);
}

static i64 win32_query_counter()
{
   return clock_query_counter();
}

static f64 win32_time_to_counter(f64 time)
{
   return clock_time_to_counter(time);
}

static f64 win32_seconds_elapsed(i64 begin, i64 end)
{
   return clock_seconds_elapsed(begin, end);
}

static void win32_window_title(hw* hw, s8 message, ...)
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
   while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
   {
      if(msg.message == WM_QUIT) return false;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }

   return true;
}

static WINDOWPLACEMENT global_window_placement = {sizeof(global_window_placement)};

static LRESULT CALLBACK win32_win_proc(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam) {
   hw* win32_hw = (hw*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

   switch(umsg)
   {
      case WM_CREATE:
      {
         CREATESTRUCT* pcreate = (CREATESTRUCT*)lparam;
         int width = pcreate->cx;
         int height = pcreate->cy;

         if(win32_hw)
         {
            win32_hw->renderer.window.width = width;
            win32_hw->renderer.window.height = height;
            win32_hw->renderer.frame_resize(&win32_hw->renderer, win32_hw->renderer.window.width, win32_hw->renderer.window.height);
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
            win32_hw->renderer.frame_resize(&win32_hw->renderer, win32_hw->renderer.window.width, win32_hw->renderer.window.height);
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
            win32_hw->renderer.frame_resize(&win32_hw->renderer, width, height);

         return 0;
      }

      case WM_MOUSEWHEEL:
      {
         i32 delta = GET_WHEEL_DELTA_WPARAM(wparam);
         if(delta > 0)
            win32_hw->state.input.mouse_wheel_state = MOUSE_WHEEL_STATE_UP;
         else if(delta < 0)
            win32_hw->state.input.mouse_wheel_state = MOUSE_WHEEL_STATE_DOWN;
      }
      break;

      case WM_MOUSEMOVE:
      {
         POINT pt;
         GetCursorPos(&pt);
         ScreenToClient(hwnd, &pt);

         win32_hw->state.input.mouse_pos[0] = pt.x;
         win32_hw->state.input.mouse_pos[1] = pt.y;
      }
      break;

      case WM_LBUTTONDOWN:
         win32_hw->state.input.mouse_buttons |= MOUSE_BUTTON_STATE_LEFT;
         break;

      case WM_LBUTTONUP:
         win32_hw->state.input.mouse_buttons &= ~MOUSE_BUTTON_STATE_LEFT;
         break;

      case WM_RBUTTONDOWN:
         win32_hw->state.input.mouse_buttons |= MOUSE_BUTTON_STATE_RIGHT;
         break;

      case WM_RBUTTONUP:
         win32_hw->state.input.mouse_buttons &= ~MOUSE_BUTTON_STATE_RIGHT;
         break;

      case WM_MBUTTONDOWN:
         win32_hw->state.input.mouse_buttons |= MOUSE_BUTTON_STATE_MIDDLE;
         break;

      case WM_MBUTTONUP:
         win32_hw->state.input.mouse_buttons &= ~MOUSE_BUTTON_STATE_MIDDLE;
         break;

      // TODO: hash table for keys
      case WM_SYSKEYDOWN:
      case WM_KEYDOWN:
      {
         u32 vkcode = (u32)wparam;
         bool is_repeat = (lparam & (1 << 30)) != 0;

         win32_hw->state.input.key = vkcode;
         win32_hw->state.input.key_state = is_repeat ? KEY_STATE_REPEATING : KEY_STATE_DOWN;

         return 0;
      }
      case WM_SYSKEYUP:
      case WM_KEYUP:
      {
         u32 vkcode = (u32)wparam;

         win32_hw->state.input.key = vkcode;
         win32_hw->state.input.key_state = KEY_STATE_UP;

         if(win32_hw->state.input.key == 'F')
         {
            DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);
            if(dwStyle & WS_OVERLAPPEDWINDOW)
            {
               MONITORINFO mi = {sizeof(mi)};

               if(GetWindowPlacement(hwnd, &global_window_placement)
                  && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi))
               {
                  SetWindowLong(hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
                  SetWindowPos(hwnd, HWND_TOP,
                               mi.rcMonitor.left, mi.rcMonitor.top,
                               mi.rcMonitor.right - mi.rcMonitor.left,
                               mi.rcMonitor.bottom - mi.rcMonitor.top,
                               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
               }
            }
            else
            {
               SetWindowLong(hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
               SetWindowPlacement(hwnd, &global_window_placement);
               SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            }
         }

         return 0;
      }
      break;
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
   wc.lpszClassName = "classname";

   RegisterClass(&wc);

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
	assert(hw_is_virtual_memory_reserved((byte*)address+size-1));
	assert(!hw_is_virtual_memory_commited((byte*)address+size-1));

	// commit the reserved address range
   global_allocate(address, size, MEM_COMMIT, PAGE_READWRITE);
}

static void hw_virtual_memory_release(void* address)
{
	global_free(address, 0, MEM_RELEASE);
}

static void hw_virtual_memory_decommit(void* address, usize size)
{
	assert(hw_is_virtual_memory_commited((byte*)address+size-1));
	assert(hw_is_virtual_memory_commited((byte*)address));

   u32 error = GetLastError();
   global_free(address, size, MEM_DECOMMIT);
   error = GetLastError();
}

static void hw_virtual_memory_init()
{
   typedef LPVOID(*VirtualAllocPtr)(LPVOID, usize, DWORD, DWORD);

   HMODULE hkernel32 = GetModuleHandleA("kernel32.dll");

   assert(hkernel32);

   global_allocate = (VirtualAllocPtr)(GetProcAddress(hkernel32, "VirtualAlloc"));
   global_free = (VirtualFreePtr)(GetProcAddress(hkernel32, "VirtualFree"));

   assert(global_allocate);
   assert(global_free);
}

static void arena_free(arena* a)
{
   hw_virtual_memory_release(a->beg);
}

align_struct scratch_foo
{
   i64 i, j;
} scratch_foo;

align_struct arena_foo
{
   i64 k;
} arena_foo;

static arena_foo** arena_test_compute1(arena* a, size sz)
{
   arena_foo** result = push(a, arena_foo*);
   scratch_foo* foo = push(a, scratch_foo, sz);

   for(size i = 0; i < sz; ++i)
   {
      foo[i].i = i;
      foo[i].j = i+1;

      result[i] = push(a, arena_foo);
      //result[i]->k = foo->i - 442 + foo->j*2 + foo->i;
      result[i]->k = foo[i].i + foo[i].j;
   }

   return result;
}

static void arena_test_compute2(arena* a, arena_foo** result, size sz)
{
   for(size i = 0; i < sz; ++i)
   {
      scratch_foo* foo = push(a, scratch_foo);
      foo->i = i;
      foo->j = i+1;

      foo->i = -(i64)result[i]->k;
      foo->j = -(i64)result[i]->k;
      result[i]->k += foo->i + foo->j;
      assert(result[i]->k * 2 == foo->i + foo->j);

      // wp(S, result[i].k*2 == foo->i + foo->j);
      // wp(result[i].k = result[i].k + foo->i + foo->j, result[i].k*2 == foo->i + foo->j);

      // wp((result[i].k + foo->i + foo->j)*2 == foo->i + foo->j);

      // (result[i].k*2 + foo->i + foo->j == 0)
   }
}

static bool arena_test_bool(arena* a, arena_foo** result, size sz)
{
   *result = push(a, arena_foo, sz);

   arena_test_compute1(a, sz);
   arena_test_compute2(a, result, sz);

   return true;
}

typedef array(arena_foo) array_foo;

static void array_test_free(array_foo* foos)
{
   //hw_virtual_memory_decommit(foos->data, foos->count * sizeof(typeof(*(foos->data))));
   memset(foos->data, 0, foos->count * sizeof(typeof(*(foos->data))));
   foos->count = 0;
   foos->data = 0;
}

static void array_test_result(array_foo* foos, size array_size)
{
   //if(foos->arena->beg <= foos->old_arena.end)
   if(foos->old_arena.beg < foos->arena->beg)
   {
      const size s = foos->count * sizeof(typeof(*foos->data));

      assert(hw_is_virtual_memory_commited((byte*)foos->arena->beg + s - 1));
      assert(hw_is_virtual_memory_commited((byte*)foos->arena->beg));
      memmove(foos->arena->beg, foos->data, s);
      foos->data = foos->arena->beg;

      foos->arena->beg = (byte*)foos->arena->beg + s;
   }

   for(size i = 0; i < array_size; ++i)
      arrayp_push(foos) = (arena_foo){.k = i};

   foos->old_arena = *foos->arena;
}

static void arena_test_result(arena* a, size sz)
{
   arena_foo** result = 0;

   result = arena_test_compute1(a, sz);

   for(size i = 0; i < sz; ++i)
      printf("arena_test_compute1: %lld\n", result[i]->k);

   arena_test_compute2(a, result, sz);

   printf("\n");

   for(size i = 0; i < sz; ++i)
      printf("arena_test_compute2: %lld\n", result[i]->k);
}

int main(int argc, char** argv)
{
   hw hw = {0};

   hw_virtual_memory_init();

   const size arena_max_commit_size = 1ull << 46;
   const size arena_part_size = arena_max_commit_size/4;

   // max virtual limit
   void* program_memory = global_allocate(0, arena_max_commit_size, MEM_RESERVE, PAGE_READWRITE);
   assert(program_memory);

   arena app_arena = {0};
   app_arena.end = program_memory;

   arena vulkan_arena = {0};
   vulkan_arena.end = (byte*)app_arena.end + arena_part_size;

   arena scratch_arena = {0};
   scratch_arena.end = (byte*)vulkan_arena.end + arena_part_size;

   const size initial_arena_size = PAGE_SIZE;

   arena* app_storage = arena_new(&app_arena, initial_arena_size, arena_persistent_kind);
   assert(arena_left(app_storage) == initial_arena_size);

   arena* vulkan_storage = arena_new(&vulkan_arena, initial_arena_size, arena_persistent_kind);
   assert(arena_left(vulkan_storage) == initial_arena_size);

   arena* scratch_storage = arena_new(&scratch_arena, initial_arena_size, arena_scratch_kind);
   assert(arena_left(scratch_storage) == initial_arena_size);

   hw.app_storage = app_storage;
   hw.vulkan_storage = vulkan_storage;
   hw.scratch = *scratch_storage;

   hw.renderer.window.open = win32_window_open;
   hw.renderer.window.close = win32_window_close;
   hw.renderer.window_surface_create = window_surface_create;

   hw.timer.sleep = win32_sleep;
   hw.timer.time = win32_query_counter;
   hw.timer.seconds_elapsed = win32_seconds_elapsed;
   hw.timer.time_to_counter = win32_time_to_counter;

   hw.platform_loop = win32_platform_loop;

   hw.window_title_set = win32_window_title;

   if(argc < 2)
   {
      printf("Place the gltf asset in assets/gltf directory and ");
      printf("use like so: program_name.exe <gltf-dir/gltf-name.gltf>");
      return 0;
   }

   // arena tests
   #if 1
   size sz = 10;

   array_foo first = {app_storage};
   array_test_result(&first, sz);

   array_foo second = {app_storage};
   array_test_result(&second, sz);

   array_test_result(&first, 5);

   for(size i = 0; i < first.count; ++i)
      printf("First: %d\n", (int)first.data[i].k);

   array_test_free(&first);

   for(size i = 0; i < first.count; ++i)
      printf("First: %d\n", (int)first.data[i].k);

   array_test_result(&second, 5);

   for(size i = 0; i < second.count; ++i)
      printf("Second: %d\n", (int)second.data[i].k);

   for(size i = 0; i < first.count; ++i)
      printf("First: %d\n", (int)first.data[i].k);

   #else
   app_start(&hw, s8(argv[1]));
   #endif

   global_free(program_memory, 0, MEM_RELEASE);

   return 0;
}
