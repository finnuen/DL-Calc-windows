@echo off
setlocal

echo Building DLCalc.exe (Pure C++17 Win32 API + GDI+)...

where x86_64-w64-mingw32-g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Using MinGW-w64 (x86_64-w64-mingw32-g++)...
    x86_64-w64-mingw32-windres cpp\resource.rc -O coff -o resource.res
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -mwindows -municode -static -static-libgcc -static-libstdc++ cpp\DLCalcWin32.cpp resource.res -o DLCalc.exe -lgdiplus -ldwmapi -lcomctl32 -lgdi32 -luser32 -lshell32
    del /q resource.res 2>nul
    echo Built DLCalc.exe successfully.
    exit /b 0
)

where g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Using MinGW g++...
    windres cpp\resource.rc -O coff -o resource.res
    g++ -std=c++17 -O2 -s -mwindows -municode -static -static-libgcc -static-libstdc++ cpp\DLCalcWin32.cpp resource.res -o DLCalc.exe -lgdiplus -ldwmapi -lcomctl32 -lgdi32 -luser32 -lshell32
    del /q resource.res 2>nul
    echo Built DLCalc.exe successfully.
    exit /b 0
)

where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Using Microsoft Visual C++ (cl.exe)...
    rc.exe /nologo /fo resource.res cpp\resource.rc
    cl.exe /nologo /EHsc /std:c++17 /O2 /MT /DUNICODE /D_UNICODE cpp\DLCalcWin32.cpp resource.res /Fe:DLCalc.exe /link /SUBSYSTEM:WINDOWS gdiplus.lib dwmapi.lib comctl32.lib gdi32.lib user32.lib shell32.lib
    del /q resource.res DLCalcWin32.obj 2>nul
    echo Built DLCalc.exe successfully.
    exit /b 0
)

echo Error: No C++17 compiler (MinGW-w64 g++ or MSVC cl.exe) found in PATH.
exit /b 1
