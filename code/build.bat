@echo off
setlocal enabledelayedexpansion

set WIN32_LIBS=kernel32.lib user32.lib gdi32.lib
set VULKAN_LIB=vulkan-1.lib
set VULKAN_INC=%VULKAN_SDK%\Include
set EXTERNAL_INC=..\extern\volk
set VULKAN_LIBPATH=%VULKAN_SDK%\Lib
set IGNORE_WARNINGS=-wd4127 -wd4706 -wd4100 -wd4996 -wd4505 -wd4201

IF NOT EXIST ..\build mkdir ..\build
pushd ..\build
del /Q/F/S *.* > nul

cl -MT -nologo -Od -Oi -Zi -FC -W3 /std:clatest %IGNORE_WARNINGS%  -I "%VULKAN_INC%" -I "%EXTERNAL_INC%" ..\code\app.c ..\code\win32.c /link %WIN32_LIBS% /DEBUG -incremental:no /LIBPATH:"%VULKAN_LIBPATH%" /out:vulkan_3d.exe

IF %ERRORLEVEL% EQU 0 (
    echo Success!
) ELSE (
    echo Build failed with error %ERRORLEVEL%
)

popd
endlocal
