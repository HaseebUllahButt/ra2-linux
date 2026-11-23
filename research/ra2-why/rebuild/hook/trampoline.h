// x86 trampoline patcher - portable logic, unit-tested on Linux.
// A hook site gets a 5-byte E9 jmp to the replacement. The replaced
// bytes move to a trampoline that ends with another jmp back, so the
// original function stays callable.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace ra2hook {

struct PatchSite {
    uint32_t va;          // virtual address of the first replaced byte
    uint8_t original[16]; // bytes currently at va
    uint8_t stolen;       // how many bytes the jmp replaces (>= 5)
};

// True if the bytes at the site can host a 5-byte relative jmp.
bool site_is_patchable(const uint8_t* bytes, size_t stolen);

// Build trampoline bytes: stolen original bytes + jmp back to va+stolen.
std::vector<uint8_t> build_trampoline(const PatchSite& site);

// Compute the 4-byte rel32 for a jmp at `from_va` targeting `to_va`.
int32_t rel32(uint32_t from_va, uint32_t to_va);

} // namespace ra2hook
