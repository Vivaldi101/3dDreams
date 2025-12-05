#include "common.h"
#include "arena.h"

#include <Windows.h>
#include <stdio.h>

static arena win32_file_read(arena* a, const char* path)
{
   arena result = {0};

   HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
   if(file == INVALID_HANDLE_VALUE)
   {
      printf("No file with such name");
      return (arena) {0};
   }

   LARGE_INTEGER file_size;
   if(!GetFileSizeEx(file, &file_size))
      return (arena) {0};

   u32 file_size_32 = (u32)(file_size.QuadPart);
   if(file_size_32 == 0)
      return (arena) {0};

   byte* buffer = push(a, byte, file_size_32);

   DWORD bytes_read = 0;
   if(!(ReadFile(file, buffer, file_size_32, &bytes_read, 0) && (file_size_32 == bytes_read)))
      return (arena) {};

   CloseHandle(file);

   result.beg = buffer;
   result.end = buffer + bytes_read;

   return result;
}

