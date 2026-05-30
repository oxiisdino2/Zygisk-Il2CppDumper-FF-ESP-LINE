@echo off
title FF ESP Panel Builder
echo === FF ESP Panel - Build ===
echo.

if not exist "bin" mkdir bin

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86

echo [*] Compiling...
cl.exe /nologo /Oi /O2 /GS- /GL- /MT /W3 /EHsc /std:c++17 /Isrc /c src\main.cpp src\overlay.cpp src\esp.cpp src\memory.cpp src\w2s.cpp
if errorlevel 1 goto err

echo [*] Linking...
link.exe /nologo /OUT:bin\FF_ESP_Panel.exe /SUBSYSTEM:WINDOWS main.obj overlay.obj esp.obj memory.obj w2s.obj gdi32.lib user32.lib d2d1.lib dwrite.lib ws2_32.lib dwmapi.lib ole32.lib oleaut32.lib uuid.lib
if errorlevel 1 goto err

del *.obj
echo.
echo [+] BUILD SUCCESS — bin\FF_ESP_Panel.exe
goto end

:err
echo [!] BUILD FAILED
pause
exit /b 1

:end
pause
