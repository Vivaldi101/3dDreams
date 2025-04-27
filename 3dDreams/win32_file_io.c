#include "common.h"
#include "arena.h"
#include <stdio.h>

// Remove to just use an arena
align_struct file_result
{
   arena data;
	size file_size;
} file_result;

static file_result win32_file_read(arena* file_arena, const char* path)
{
   file_result result = {};

   HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
   if(file == INVALID_HANDLE_VALUE)
   {
      hw_message("No file with such name");
      return (file_result) {};
   }

   LARGE_INTEGER file_size;
   if(!GetFileSizeEx(file, &file_size))
      return (file_result) {};

   u32 file_size_32 = (u32)(file_size.QuadPart);
   if(file_size_32 == 0)
      return (file_result) {};

   if(arena_left(file_arena, byte) < file_size_32)
      return (file_result) {};

   result.data = newsize(file_arena, file_size_32);

   DWORD bytes_read = 0;
   if(!(ReadFile(file, result.data.beg, file_size_32, &bytes_read, 0) && (file_size_32 == bytes_read)))
      return (file_result) {};

   result.file_size = bytes_read;

   CloseHandle(file);

   return result;
}

