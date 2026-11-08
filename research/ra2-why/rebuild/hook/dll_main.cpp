// Windows-only DLL entry for the proxy winmm.dll.
// Build with the hook-local Makefile; see HOOKING.md.
// The .def file forwards the listed winmm exports to winmm_real.dll.
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

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Patch synchronously before the EXE can call Bootstrap. Only page
        // protection, the E9 write, and debugger logging occur here; archive
        // loading waits until the game calls the replacement outside DllMain.
        DisableThreadLibraryCalls(module);
        if (ra2hook::install_hooks(game_image_base()) != 1)
            OutputDebugStringA("[ra2hook] Bootstrap hook installation failed\n");
    }
    return TRUE;
}
