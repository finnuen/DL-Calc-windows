# Makefile for DL Calc — Pure Standalone C++17 Win32 Application
CXX_MINGW := x86_64-w64-mingw32-g++
WINDRES   := x86_64-w64-mingw32-windres
CXX_HOST  := g++

WIN32_CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -s -mwindows -municode -static -static-libgcc -static-libstdc++
WIN32_LDFLAGS  := -lgdiplus -ldwmapi -lcomctl32 -lgdi32 -luser32 -lshell32
HOST_CXXFLAGS  := -std=c++17 -Wall -Wextra -O2

TARGET_EXE  := DLCalc.exe
RES_OBJ     := /tmp/dlcalc_resource.res
TEST_BIN    := /tmp/test_dlcalc
HOST_BIN    := /tmp/dlcalc_host

.PHONY: all build test check serve clean

all: build

build: $(TARGET_EXE) test $(HOST_BIN)

$(RES_OBJ): cpp/resource.rc cpp/dlcalc.ico
	$(WINDRES) cpp/resource.rc -O coff -o $(RES_OBJ)

$(TARGET_EXE): cpp/DLCalcWin32.cpp cpp/DLCalcCore.hpp $(RES_OBJ)
	$(CXX_MINGW) $(WIN32_CXXFLAGS) cpp/DLCalcWin32.cpp $(RES_OBJ) -o $(TARGET_EXE) $(WIN32_LDFLAGS)

test: cpp/test_dlcalc.cpp cpp/DLCalcCore.hpp
	$(CXX_HOST) $(HOST_CXXFLAGS) cpp/test_dlcalc.cpp -o $(TEST_BIN)
	$(TEST_BIN)

$(HOST_BIN): cpp/DLCalcHost.cpp cpp/DLCalcCore.hpp
	$(CXX_HOST) $(HOST_CXXFLAGS) cpp/DLCalcHost.cpp -o $(HOST_BIN)

check:
	$(CXX_MINGW) -std=c++17 -Wall -Wextra -fsyntax-only cpp/DLCalcWin32.cpp
	$(CXX_HOST) -std=c++17 -Wall -Wextra -fsyntax-only cpp/test_dlcalc.cpp
	$(CXX_HOST) -std=c++17 -Wall -Wextra -fsyntax-only cpp/DLCalcHost.cpp

serve: $(TARGET_EXE) $(HOST_BIN)
	exec $(HOST_BIN)

clean:
	rm -f $(RES_OBJ) $(TEST_BIN) $(HOST_BIN)
