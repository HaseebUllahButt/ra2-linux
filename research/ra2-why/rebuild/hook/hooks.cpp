// Hook table: original gamemd.exe address -> rebuilt function.
// Seeded with the verified landmarks; every entry needs evidence from
// analysis/ (community cross-check or decompile). Grows per layer.
#include "trampoline.h"
#include <cstdint>
#include <cstdio>

namespace ra2hook {

struct HookEntry {
    uint32_t original_va;   // address inside gamemd.exe (image base 0x400000)
    void* replacement;      // rebuilt function (nullptr = log only)
    const char* name;       // community-verified name
};

// Verified landmarks (see analysis/community-crosscheck.md):
static HookEntry kHooks[] = {
    // MixFileClass::Bootstrap - mounts RA2/RA2MD + EXPANDMD%02d loop.
    // Replacement pending: rebuild/src/fs mix_bootstrap is verified
    // against real archives; wiring needs the C++ ABI of the game.
    {0x005301A0, nullptr, "MixFileClass::Bootstrap"},
    // MixFileClass(const char*) ctor - MIX open primitive.
    {0x005B3C20, nullptr, "MixFileClass::MixFileClass"},
    // RulesClass::Init(CCINIClass*) - rules loading.
    {0x006686C0, nullptr, "RulesClass::Init"},
    // RulesClass::Read_File(CCINIClass*).
    {0x00668BF0, nullptr, "RulesClass::Read_File"},
    // Frame step (0x55D360) - the loop layer models this.
    {0x0055D360, nullptr, "FrameStep"},
    // WinMain equivalent (0x6BB9A0).
    {0x006BB9A0, nullptr, "WinMain"},
};

constexpr uint32_t kImageBase = 0x00400000;

int install_hooks(uint8_t* image_base) {
    int installed = 0;
    for (const HookEntry& hook : kHooks) {
        const uint32_t rva = hook.original_va - kImageBase;
        uint8_t* site = image_base + rva;
        if (!site_is_patchable(site, 5)) {
            std::printf("skip %-28s @ %08X (not patchable)\n",
                        hook.name, hook.original_va);
            continue;
        }
        // Replacement wiring lands per layer; for now every site is
        // validated and reported. The trampoline builder is unit-
        // tested in test_trampoline.cpp.
        std::printf("hook %-28s @ %08X ready\n", hook.name, hook.original_va);
        ++installed;
    }
    return installed;
}

} // namespace ra2hook
