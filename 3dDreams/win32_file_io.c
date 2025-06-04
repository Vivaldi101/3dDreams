#include "common.h"
#include "arena.h"
#include <stdio.h>

static arena win32_file_read(arena* file_arena, const char* path)
{
   arena result = {};

   HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
   if(file == INVALID_HANDLE_VALUE)
   {
      hw_message("No file with such name");
      return (arena) {};
   }

   LARGE_INTEGER file_size;
   if(!GetFileSizeEx(file, &file_size))
      return (arena) {};

   u32 file_size_32 = (u32)(file_size.QuadPart);
   if(file_size_32 == 0)
      return (arena) {};

   byte* buffer = push_size(file_arena, file_size_32);

   DWORD bytes_read = 0;
   if(!(ReadFile(file, buffer, file_size_32, &bytes_read, 0) && (file_size_32 == bytes_read)))
      return (arena) {};

   CloseHandle(file);

   result.beg = buffer;
   result.end = buffer + bytes_read;

   return result;
}

