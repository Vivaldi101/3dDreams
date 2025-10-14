@echo off

set "found="

rem Define standard Program Files paths
set "pf1=C:\Program Files"
set "pf2=C:\Program Files (x86)"

for %%D in ("%pf1%" "%pf2%") do (
    echo Searching in: %%~D
    for /f "usebackq delims=" %%F in (`dir /s /b "%%~D\vcvars64.bat" 2^>nul`) do (
        set "vcvars64=%%F"
        set "found=1"
        goto :found
    )
)

if not defined found (
    echo vcvars64.bat not found under Program Files.
    goto :eof
)

:found
echo Found: %vcvars64%
call "%vcvars64%" x64

rem Now that the environment is set, test cl
where cl >nul 2>nul
if errorlevel 1 (
    echo cl still not recognized
) else (
    echo cl is now available. Try running: cl /?
)
