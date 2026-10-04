# DL Calc — Pure Standalone C++17 Win32 Application

Native Windows C++17 desktop application built exclusively with the Win32 API, GDI+, and DWM (`dwmapi`), with zero external runtime dependencies.

## Project Structure

- `cpp/DLCalcCore.hpp` — Header-only C++17 bidirectional download time, speed, and file size calculation engine, `+` expression evaluator, and thousands-separator formatter.
- `cpp/DLCalcWin32.cpp` — Standalone Win32 GUI application (`wWinMain`, double-buffered GDI+ custom drawing, subclassed `EDIT` controls, DWM dark/light title bar theming, and automatic state persistence in `%APPDATA%\DLCalc\settings.ini`).
- `cpp/resource.rc` & `cpp/dlcalc.ico` — Windows resource script embedding the multi-resolution application icon (`IDI_APP_ICON`) and `VS_VERSION_INFO`.
- `cpp/test_dlcalc.cpp` — C++17 unit test suite verifying all calculation modes, unit scaling, and expression parsing.
- `DLCalc.exe` — Pre-compiled standalone 64-bit Windows PE executable (`PE32+ executable (GUI) x86-64`).
- `Makefile` / `CMakeLists.txt` / `build.bat` — Native build scripts for MinGW-w64, MSVC, and CMake.

## Building from Source

### Option 1: MinGW-w64 (Windows or Linux Cross-Compilation)
```bash
make build
```
Or directly:
```bash
x86_64-w64-mingw32-windres cpp/resource.rc -O coff -o resource.res
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -mwindows -municode -static -static-libgcc -static-libstdc++ \
    cpp/DLCalcWin32.cpp resource.res -o DLCalc.exe -lgdiplus -ldwmapi -lcomctl32 -lgdi32 -luser32 -lshell32
```

### Option 2: Windows Batch Script (MinGW-w64 or MSVC `cl.exe`)
```bat
build.bat
```

### Option 3: CMake
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
