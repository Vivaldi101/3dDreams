@echo off

IF "%1"=="" (
    echo "Usage: %0 d|r"
    exit /b 1
)

SET ROOT=%~dp0

set WIN32_LIBS=kernel32.lib user32.lib gdi32.lib
set VULKAN_LIB=vulkan-1.lib
set VULKAN_INC=%VULKAN_SDK%\Include
set EXTERNAL_INC=%ROOT%..\extern\volk
set VULKAN_LIBPATH=%VULKAN_SDK%\Lib
set IGNORE_WARNINGS=-wd4127 -wd4706 -wd4100 -wd4996 -wd4505 -wd4201

IF NOT EXIST %ROOT%..\build mkdir %ROOT%..\build
pushd %ROOT%..\build

IF /I "%1"=="d" (
    echo Building DEBUG version...
    cl -MT -nologo -Od -Oi -Zi -FC -W3 /std:clatest /D_DEBUG %IGNORE_WARNINGS% ^
        -I "%VULKAN_INC%" -I "%EXTERNAL_INC%" "%ROOT%\app.c" "%ROOT%\win32.c" ^
        /link %WIN32_LIBS% /DEBUG -incremental:no /LIBPATH:"%VULKAN_LIBPATH%" /out:vulkan_3d_debug.exe

    popd
)

IF /I "%1"=="r" (
    echo Building RELEASE version...
    cl -MT -nologo -O2 -Oi -Zi -FC -W3 /std:clatest %IGNORE_WARNINGS% ^
        -I "%VULKAN_INC%" -I "%EXTERNAL_INC%" "%ROOT%\app.c" "%ROOT%\win32.c" ^
        /link %WIN32_LIBS% -incremental:no /LIBPATH:"%VULKAN_LIBPATH%" /out:vulkan_3d_release.exe

    popd
)

IF %ERRORLEVEL% EQU 0 (
    echo Build succeeded!

    pushd %ROOT%
    popd
) ELSE (
    echo Build failed with error %ERRORLEVEL%
)
