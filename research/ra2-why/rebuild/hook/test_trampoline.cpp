#include "trampoline.h"
#include <cassert>
#include <cstdio>

using namespace ra2hook;

int main() {
    // A site with push ebp; mov ebp,esp (stolen 5 with a nop).
    PatchSite site{};
    site.va = 0x0055D360;
    site.original[0] = 0x55;          // push ebp
    site.original[1] = 0x8B;          // mov ebp,esp
    site.original[2] = 0xEC;
    site.original[3] = 0x90;          // nop
    site.original[4] = 0x90;          // nop
    site.stolen = 5;

    assert(site_is_patchable(site.original, site.stolen));

    const auto tramp = build_trampoline(site);
    assert(tramp.size() == 10);
    assert(tramp[0] == 0x55 && tramp[1] == 0x8B && tramp[2] == 0xEC);
    assert(tramp[5] == 0xE9);

    // rel32 from the trampoline's jmp back to va+stolen must decode.
    const uint32_t tramp_jmp_at = site.va + 0; // placeholder install va
    const int32_t rel = rel32(tramp_jmp_at, site.va + site.stolen);
    assert(rel == 0);

    // A site with a call inside the stolen range must be rejected.
    PatchSite bad{};
    bad.original[0] = 0xE8;
    bad.stolen = 5;
    assert(!site_is_patchable(bad.original, bad.stolen));

    std::printf("trampoline tests passed\n");
    return 0;
}
