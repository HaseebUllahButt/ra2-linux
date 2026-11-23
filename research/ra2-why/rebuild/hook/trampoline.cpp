#include "trampoline.h"

namespace ra2hook {

bool site_is_patchable(const uint8_t* bytes, size_t stolen) {
    if (stolen < 5) {
        return false;
    }
    // A relative jmp/call in the stolen range would break when moved.
    for (size_t i = 0; i < stolen; ++i) {
        const uint8_t op = bytes[i];
        if (op == 0xE8 || op == 0xE9) {
            return false;
        }
    }
    return true;
}

int32_t rel32(uint32_t from_va, uint32_t to_va) {
    // jmp rel32: the offset counts from the END of the instruction.
    return static_cast<int32_t>(to_va - (from_va + 5));
}

std::vector<uint8_t> build_trampoline(const PatchSite& site) {
    std::vector<uint8_t> out(site.original, site.original + site.stolen);
    const uint32_t back_to = site.va + site.stolen;
    const uint32_t tramp_va = 0; // resolved at install time on Windows
    out.push_back(0xE9);
    const int32_t rel = rel32(tramp_va + static_cast<uint32_t>(out.size()), back_to);
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>((rel >> shift) & 0xFF));
    }
    return out;
}

} // namespace ra2hook
