// Hook table: original gamemd.exe address -> rebuilt function.
// Seeded with the verified landmarks; every entry needs evidence from
// analysis/ (community cross-check or decompile). Grows per layer.
#include "trampoline.h"
#include "bootstrap_hook.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ra2hook {

struct HookEntry {
    uint32_t original_va;   // address inside gamemd.exe (image base 0x400000)
    void* replacement;      // rebuilt function (nullptr = log only)
    const char* name;       // community-verified name
};

// Verified landmarks (see analysis/community-crosscheck.md):
static HookEntry kHooks[] = {
    // MixFileClass::Bootstrap - mounts RA2/RA2MD + EXPANDMD%02d loop.
    {0x005301A0, reinterpret_cast<void*>(&bootstrap_hook), "MixFileClass::Bootstrap"},
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
#ifdef _WIN32
    // Only patch the supported fixed-base PE32 executable. Do not try to
    // interpret another process image or an already installed detour.
    if (!image_base || reinterpret_cast<std::uintptr_t>(image_base) != kImageBase) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return 0;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image_base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0;
    configure_bootstrap_hook(image_base);
#else
    // A Linux code buffer is not a running PE32 process. The bridge is tested
    // separately; never claim an installation without writing executable code.
    (void)image_base;
#endif
    int installed = 0;
    for (const HookEntry& hook : kHooks) {
#ifdef _WIN32
        if (!hook.replacement) continue;
        const uint32_t rva = hook.original_va - kImageBase;
        if (rva > pe->OptionalHeader.SizeOfImage || pe->OptionalHeader.SizeOfImage - rva < 5) continue;
        uint8_t* site = image_base + rva;
        if (site[0] == 0xE9 || site[0] == 0xEB || site[0] == 0xCC) continue;
        DWORD previous = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) continue;
        const auto offset = rel32(hook.original_va, reinterpret_cast<uint32_t>(hook.replacement));
        // Full function replacement: no stolen instructions are executed and
        // no trampoline is needed. Relative calls in the first five bytes are
        // consequently safe. Install before the EXE's entry point runs.
        site[0] = 0xE9;
        std::memcpy(site + 1, &offset, sizeof(offset));
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        DWORD ignored = 0;
        VirtualProtect(site, 5, previous, &ignored);
        char line[160];
        std::snprintf(line, sizeof(line), "[ra2hook] installed %s @ %08X\n", hook.name, hook.original_va);
        OutputDebugStringA(line); // DllMain: no file I/O or archive work here.
        ++installed;
#else
        (void)hook;
#endif
    }
    return installed;
}

} // namespace ra2hook
