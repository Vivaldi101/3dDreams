@echo off

echo Running shader_build.bat > shader_build.log

cd /d "%~dp0"

IF NOT EXIST "%cd%\bin\assets\shaders" mkdir "%cd%\bin\assets\shaders"

echo "Compiling shaders..."

for %%F in (assets\shaders\*.vert.glsl) do (
    echo Compiling %%F
    %VULKAN_SDK%\bin\glslc.exe -fshader-stage=vert "%%F" -o "bin\assets\shaders\%%~nF.spv"
    IF %ERRORLEVEL% NEQ 0 (echo Error compiling %%F: %ERRORLEVEL%)
)

for %%F in (assets\shaders\*.frag.glsl) do (
    echo Compiling %%F
    %VULKAN_SDK%\bin\glslc.exe -fshader-stage=frag "%%F" -o "bin\assets\shaders\%%~nF.spv"
    IF %ERRORLEVEL% NEQ 0 (echo Error compiling %%F: %ERRORLEVEL%)
)

for %%F in (assets\shaders\*.mesh.glsl) do (
    echo Compiling %%F
	 %VULKAN_SDK%\bin\glslc.exe --target-env=vulkan1.2 -fshader-stage=mesh -o bin\assets\shaders\%%~nF.spv %%F
    IF %ERRORLEVEL% NEQ 0 (echo Error compiling %%F: %ERRORLEVEL%)
)

echo "Copying assets..."
echo xcopy "assets\shaders" "bin\assets" /h /i /c /k /e /r /y
xcopy "assets\shaders" "bin\assets" /h /i /c /k /e /r /y
