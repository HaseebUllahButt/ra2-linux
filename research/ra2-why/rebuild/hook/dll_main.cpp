// Windows-only DLL entry for the proxy winmm.dll.
// Compile with mingw-w64 (not available on this ARM REFERENCE_SYSTEM yet):
//   x86_64-w64-mingw32-g++ -shared -m32 dll_main.cpp hooks.cpp
//     trampoline.cpp -o winmm.dll -static-libgcc -static-libstdc++
// The .def file forwards every real winmm export to the system DLL.
#include <cstdint>
#include <cstdio>
#include <windows.h>

namespace ra2hook {
int install_hooks(uint8_t* image_base);
}

static uint8_t* game_image_base()
{
    // gamemd.exe is the main module; its preferred base is 0x400000.
    return reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // The loader lock is held; keep work minimal. Real hook
        // installation runs on a thread spawned here once the exe's
        // import table is fully bound.
        DisableThreadLibraryCalls(static_cast<HINSTANCE>(nullptr));
        ra2hook::install_hooks(game_image_base());
    }
    return TRUE;
}
