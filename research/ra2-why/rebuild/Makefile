# RA2 engine rebuild - one command builds and tests every layer.
# Layers land one at a time; each must compile clean and pass its
# tests against the real game data in game/ (read-only).

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

.PHONY: all test clean fs rules hook

all: test

test: fs rules hook
	@echo "== all layer tests passed =="

fs: run-fs

rules: run-rules

hook: run-hook

run-fs:
	$(CXX) $(CXXFLAGS) -I src/fs src/fs/mix_archive.cpp \
		src/fs/mix_bootstrap.cpp src/fs/filename_hash.cpp \
		src/fs/test_main.cpp -o src/fs/mix_test
	src/fs/mix_test --self-test

run-rules:
	$(CXX) $(CXXFLAGS) -I src/rules src/rules/ini_file.cpp \
		src/rules/rules_reader.cpp src/rules/test_main.cpp \
		-o src/rules/rules_test
	cd .. && rebuild/src/rules/rules_test

run-hook:
	$(CXX) $(CXXFLAGS) -I hook hook/test_trampoline.cpp \
		hook/trampoline.cpp -o hook/test_trampoline
	hook/test_trampoline

clean:
	rm -f src/fs/mix_test src/rules/rules_test hook/test_trampoline \
		src/fs/crypto_test
