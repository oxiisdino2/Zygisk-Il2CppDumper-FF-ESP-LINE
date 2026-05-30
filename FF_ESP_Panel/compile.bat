@echo off
setlocal enabledelayedexpansion

echo === FF ESP Panel - Direct Compile ===
echo.

REM Set up VS 2022 Build Tools environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86

if errorlevel 1 (
    echo [!] Failed to set up VS environment
    pause
    exit /b 1
)

REM Create output directory
if not exist "bin" mkdir bin

set SRC=src
set OUT=bin
set CFLAGS=/nologo /Oi /O2 /GS- /GL- /MT /W3 /EHsc /std:c++17 /I%SRC%
set LDFLAGS=/nologo /OUT:%OUT%\FF_ESP_Panel.exe /SUBSYSTEM:WINDOWS /LTCG-
set LIBS=gdi32.lib user32.lib d2d1.lib dwrite.lib ws2_32.lib dwmapi.lib ole32.lib oleaut32.lib uuid.lib

echo [*] Compiling...

cl.exe %CFLAGS% /c %SRC%\main.cpp %SRC%\overlay.cpp %SRC%\esp.cpp %SRC%\memory.cpp %SRC%\w2s.cpp
if errorlevel 1 (
    echo [!] Compilation failed
    pause
    exit /b 1
)

echo [*] Linking...
link.exe %LDFLAGS% main.obj overlay.obj esp.obj memory.obj w2s.obj %LIBS%
if errorlevel 1 (
    echo [!] Linking failed
    pause
    exit /b 1
)

del *.obj 2>nul

echo.
echo [+] SUCCESS! Output: %OUT%\FF_ESP_Panel.exe
pause
