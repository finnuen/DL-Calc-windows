cmake_minimum_required(VERSION 3.16)
project(DLCalc VERSION 1.3.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_executable(DLCalc WIN32
        cpp/DLCalcWin32.cpp
        cpp/DLCalcCore.hpp
        cpp/resource.rc
    )

    target_compile_definitions(DLCalc PRIVATE UNICODE _UNICODE)

    if(MINGW)
        target_link_options(DLCalc PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
    endif()

    target_link_libraries(DLCalc PRIVATE
        gdiplus
        dwmapi
        comctl32
        gdi32
        user32
        shell32
    )
endif()

add_executable(test_dlcalc
    cpp/test_dlcalc.cpp
    cpp/DLCalcCore.hpp
)

enable_testing()
add_test(NAME DLCalcCoreTests COMMAND test_dlcalc)
