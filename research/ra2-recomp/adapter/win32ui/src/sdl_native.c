/* sdl_native.c: the native window handle of an SDL window (Windows build only). */
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdint.h>
unsigned ui_sdl_native_handle(void *sdl)
{
#if defined(_WIN32)
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo((SDL_Window *)sdl, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        return (unsigned)(uintptr_t)info.info.win.window;
#else
    (void)sdl;
#endif
    return 0;
}
