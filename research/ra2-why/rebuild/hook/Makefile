CXX ?= g++
WIN_CXX ?= i686-w64-mingw32-g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
BUILD := build
export TMPDIR := $(abspath $(BUILD))
FS := ../src/fs
FS_SOURCES := $(FS)/filename_hash.cpp $(FS)/mix_archive.cpp $(FS)/mix_bootstrap.cpp
FS_HEADERS := $(wildcard $(FS)/*.h) $(FS)/blowfish_constants.inc

.PHONY: all test windows
all: test

$(BUILD):
	mkdir -p $@

$(BUILD)/test_bridge: test_bridge.cpp bootstrap_hook.cpp bootstrap_hook.h $(FS_SOURCES) $(FS_HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_bridge.cpp bootstrap_hook.cpp $(FS_SOURCES) -o $@

$(BUILD)/test_trampoline: test_trampoline.cpp trampoline.cpp trampoline.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_trampoline.cpp trampoline.cpp -o $@

test: $(BUILD)/test_bridge $(BUILD)/test_trampoline
	./$(BUILD)/test_bridge ./$(BUILD)/bridge-fixtures
	./$(BUILD)/test_trampoline

windows: $(BUILD)/winmm.dll

$(BUILD)/winmm.dll: dll_main.cpp hooks.cpp trampoline.cpp bootstrap_hook.cpp bootstrap_hook.h trampoline.h exports.def $(FS_SOURCES) $(FS_HEADERS) | $(BUILD)
	$(WIN_CXX) $(CXXFLAGS) -shared -static -static-libgcc -static-libstdc++ \
	  dll_main.cpp hooks.cpp trampoline.cpp bootstrap_hook.cpp $(FS_SOURCES) exports.def -o $@
