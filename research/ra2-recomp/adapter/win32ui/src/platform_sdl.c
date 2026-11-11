/* platform_sdl.c: the one real window (SDL), SDL input events, time, lock. */
#include "ui.h"
#include "input.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_mutex *mtx;
static SDL_Window *main_sdl;
static ui_win *main_w;
void ui_lock(void) { if (!mtx) mtx = SDL_CreateMutex(); SDL_LockMutex(mtx); }
void ui_unlock(void) { SDL_UnlockMutex(mtx); }
unsigned ui_thread_id(void) { return (unsigned)SDL_ThreadID(); }
#if defined(_WIN32)
__declspec(dllimport) DWORD __stdcall GetTickCount(void);
__declspec(dllimport) void __stdcall ExitProcess(UINT);
DWORD ui_ticks(void) { return GetTickCount(); }
void ui_exit_process(int code) { ExitProcess((UINT)code); }
#else
DWORD ui_ticks(void) { return SDL_GetTicks(); }
void ui_exit_process(int code) { exit(code); }
#endif
void ui_platform_wait(DWORD ms) { SDL_WaitEventTimeout(NULL, (int)ms); }

void *ui_sdl_create(ui_win *w)
{
    if (main_sdl) return NULL;           /* one real window: the game window */
    if (getenv("RA2UI_NO_SDL_WINDOW")) return NULL;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) ui_fatal("win32ui: SDL video: %s", SDL_GetError());
    int cx = w->wr.right - w->wr.left, cy = w->wr.bottom - w->wr.top;
    if (cx <= 0) cx = 640;
    if (cy <= 0) cy = 480;
    main_sdl = SDL_CreateWindow(w->text && *w->text ? w->text : "Red Alert 2", w->wr.left, w->wr.top, cx, cy,
                                SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    if (!main_sdl) ui_fatal("win32ui: SDL_CreateWindow: %s", SDL_GetError());
    main_w = w;
    char id[32]; snprintf(id, sizeof id, "%u", SDL_GetWindowID(main_sdl));
    SDL_SetHint("RA2UI_SDL_WINDOW_ID", id);   /* the DirectDraw adapter presents into this window */
    SDL_EventState(SDL_TEXTINPUT, SDL_DISABLE);
    ui_log("win32ui: game window %08x is SDL window %s", w->h, id);
    return main_sdl;
}
unsigned ui_sdl_native_handle(void *sdl);   /* sdl_native.c: no ui.h there, it needs windows.h */
HWND ui_sdl_native(void *sdl) { return ui_sdl_native_handle(sdl); }
void ui_sdl_sync(ui_win *w)
{
    SDL_Window *s = w->sdl;
    if (!s) return;
    int x, y, cx, cy;
    SDL_GetWindowPosition(s, &x, &y); SDL_GetWindowSize(s, &cx, &cy);
    if (x != w->cr.left || y != w->cr.top) SDL_SetWindowPosition(s, w->cr.left, w->cr.top);
    if (cx != w->cr.right - w->cr.left || cy != w->cr.bottom - w->cr.top)
        SDL_SetWindowSize(s, UI_MAX(1, w->cr.right - w->cr.left), UI_MAX(1, w->cr.bottom - w->cr.top));
    int shown = (SDL_GetWindowFlags(s) & SDL_WINDOW_SHOWN) != 0;
    if ((w->style & WS_VISIBLE) && !shown) SDL_ShowWindow(s);
    if (!(w->style & WS_VISIBLE) && shown) SDL_HideWindow(s);
}
void ui_sdl_destroy(ui_win *w)
{
    if (w->sdl == main_sdl) { main_sdl = NULL; main_w = NULL; }
    SDL_DestroyWindow(w->sdl);
    w->sdl = NULL;
}
void ui_sdl_warp(INT x, INT y)
{
    if (main_sdl && main_w) SDL_WarpMouseInWindow(main_sdl, x - main_w->cr.left, y - main_w->cr.top);
}
void ui_sdl_show_cursor(int show) { if (main_sdl) SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE); }

/* SDL scancode -> (virtual key, set-1 scan code, extended) */
static const struct { short sdl; unsigned char vk, scan, ext; } keymap[] = {
    { SDL_SCANCODE_ESCAPE, VK_ESCAPE, 0x01, 0 }, { SDL_SCANCODE_1, '1', 0x02, 0 }, { SDL_SCANCODE_2, '2', 0x03, 0 },
    { SDL_SCANCODE_3, '3', 0x04, 0 }, { SDL_SCANCODE_4, '4', 0x05, 0 }, { SDL_SCANCODE_5, '5', 0x06, 0 },
    { SDL_SCANCODE_6, '6', 0x07, 0 }, { SDL_SCANCODE_7, '7', 0x08, 0 }, { SDL_SCANCODE_8, '8', 0x09, 0 },
    { SDL_SCANCODE_9, '9', 0x0a, 0 }, { SDL_SCANCODE_0, '0', 0x0b, 0 }, { SDL_SCANCODE_MINUS, VK_OEM_MINUS, 0x0c, 0 },
    { SDL_SCANCODE_EQUALS, VK_OEM_PLUS, 0x0d, 0 }, { SDL_SCANCODE_BACKSPACE, VK_BACK, 0x0e, 0 },
    { SDL_SCANCODE_TAB, VK_TAB, 0x0f, 0 }, { SDL_SCANCODE_Q, 'Q', 0x10, 0 }, { SDL_SCANCODE_W, 'W', 0x11, 0 },
    { SDL_SCANCODE_E, 'E', 0x12, 0 }, { SDL_SCANCODE_R, 'R', 0x13, 0 }, { SDL_SCANCODE_T, 'T', 0x14, 0 },
    { SDL_SCANCODE_Y, 'Y', 0x15, 0 }, { SDL_SCANCODE_U, 'U', 0x16, 0 }, { SDL_SCANCODE_I, 'I', 0x17, 0 },
    { SDL_SCANCODE_O, 'O', 0x18, 0 }, { SDL_SCANCODE_P, 'P', 0x19, 0 }, { SDL_SCANCODE_LEFTBRACKET, VK_OEM_4, 0x1a, 0 },
    { SDL_SCANCODE_RIGHTBRACKET, VK_OEM_6, 0x1b, 0 }, { SDL_SCANCODE_RETURN, VK_RETURN, 0x1c, 0 },
    { SDL_SCANCODE_LCTRL, VK_LCONTROL, 0x1d, 0 }, { SDL_SCANCODE_A, 'A', 0x1e, 0 }, { SDL_SCANCODE_S, 'S', 0x1f, 0 },
    { SDL_SCANCODE_D, 'D', 0x20, 0 }, { SDL_SCANCODE_F, 'F', 0x21, 0 }, { SDL_SCANCODE_G, 'G', 0x22, 0 },
    { SDL_SCANCODE_H, 'H', 0x23, 0 }, { SDL_SCANCODE_J, 'J', 0x24, 0 }, { SDL_SCANCODE_K, 'K', 0x25, 0 },
    { SDL_SCANCODE_L, 'L', 0x26, 0 }, { SDL_SCANCODE_SEMICOLON, VK_OEM_1, 0x27, 0 },
    { SDL_SCANCODE_APOSTROPHE, VK_OEM_7, 0x28, 0 }, { SDL_SCANCODE_GRAVE, VK_OEM_3, 0x29, 0 },
    { SDL_SCANCODE_LSHIFT, VK_LSHIFT, 0x2a, 0 }, { SDL_SCANCODE_BACKSLASH, VK_OEM_5, 0x2b, 0 },
    { SDL_SCANCODE_Z, 'Z', 0x2c, 0 }, { SDL_SCANCODE_X, 'X', 0x2d, 0 }, { SDL_SCANCODE_C, 'C', 0x2e, 0 },
    { SDL_SCANCODE_V, 'V', 0x2f, 0 }, { SDL_SCANCODE_B, 'B', 0x30, 0 }, { SDL_SCANCODE_N, 'N', 0x31, 0 },
    { SDL_SCANCODE_M, 'M', 0x32, 0 }, { SDL_SCANCODE_COMMA, VK_OEM_COMMA, 0x33, 0 },
    { SDL_SCANCODE_PERIOD, VK_OEM_PERIOD, 0x34, 0 }, { SDL_SCANCODE_SLASH, VK_OEM_2, 0x35, 0 },
    { SDL_SCANCODE_RSHIFT, VK_RSHIFT, 0x36, 0 }, { SDL_SCANCODE_KP_MULTIPLY, VK_MULTIPLY, 0x37, 0 },
    { SDL_SCANCODE_LALT, VK_LMENU, 0x38, 0 }, { SDL_SCANCODE_SPACE, VK_SPACE, 0x39, 0 },
    { SDL_SCANCODE_CAPSLOCK, VK_CAPITAL, 0x3a, 0 },
    { SDL_SCANCODE_F1, 0x70, 0x3b, 0 }, { SDL_SCANCODE_F2, 0x71, 0x3c, 0 }, { SDL_SCANCODE_F3, 0x72, 0x3d, 0 },
    { SDL_SCANCODE_F4, 0x73, 0x3e, 0 }, { SDL_SCANCODE_F5, 0x74, 0x3f, 0 }, { SDL_SCANCODE_F6, 0x75, 0x40, 0 },
    { SDL_SCANCODE_F7, 0x76, 0x41, 0 }, { SDL_SCANCODE_F8, 0x77, 0x42, 0 }, { SDL_SCANCODE_F9, 0x78, 0x43, 0 },
    { SDL_SCANCODE_F10, 0x79, 0x44, 0 }, { SDL_SCANCODE_NUMLOCKCLEAR, VK_NUMLOCK, 0x45, 0 },
    { SDL_SCANCODE_SCROLLLOCK, VK_SCROLL, 0x46, 0 },
    { SDL_SCANCODE_KP_7, VK_NUMPAD0 + 7, 0x47, 0 }, { SDL_SCANCODE_KP_8, VK_NUMPAD0 + 8, 0x48, 0 },
    { SDL_SCANCODE_KP_9, VK_NUMPAD0 + 9, 0x49, 0 }, { SDL_SCANCODE_KP_MINUS, VK_SUBTRACT, 0x4a, 0 },
    { SDL_SCANCODE_KP_4, VK_NUMPAD0 + 4, 0x4b, 0 }, { SDL_SCANCODE_KP_5, VK_NUMPAD0 + 5, 0x4c, 0 },
    { SDL_SCANCODE_KP_6, VK_NUMPAD0 + 6, 0x4d, 0 }, { SDL_SCANCODE_KP_PLUS, VK_ADD, 0x4e, 0 },
    { SDL_SCANCODE_KP_1, VK_NUMPAD0 + 1, 0x4f, 0 }, { SDL_SCANCODE_KP_2, VK_NUMPAD0 + 2, 0x50, 0 },
    { SDL_SCANCODE_KP_3, VK_NUMPAD0 + 3, 0x51, 0 }, { SDL_SCANCODE_KP_0, VK_NUMPAD0, 0x52, 0 },
    { SDL_SCANCODE_KP_PERIOD, VK_DECIMAL, 0x53, 0 }, { SDL_SCANCODE_F11, 0x7a, 0x57, 0 },
    { SDL_SCANCODE_F12, 0x7b, 0x58, 0 }, { SDL_SCANCODE_KP_ENTER, VK_RETURN, 0x1c, 1 },
    { SDL_SCANCODE_RCTRL, VK_RCONTROL, 0x1d, 1 }, { SDL_SCANCODE_KP_DIVIDE, VK_DIVIDE, 0x35, 1 },
    { SDL_SCANCODE_PRINTSCREEN, VK_SNAPSHOT, 0x37, 1 }, { SDL_SCANCODE_RALT, VK_RMENU, 0x38, 1 },
    { SDL_SCANCODE_PAUSE, VK_PAUSE, 0x45, 0 }, { SDL_SCANCODE_HOME, VK_HOME, 0x47, 1 },
    { SDL_SCANCODE_UP, VK_UP, 0x48, 1 }, { SDL_SCANCODE_PAGEUP, VK_PRIOR, 0x49, 1 },
    { SDL_SCANCODE_LEFT, VK_LEFT, 0x4b, 1 }, { SDL_SCANCODE_RIGHT, VK_RIGHT, 0x4d, 1 },
    { SDL_SCANCODE_END, VK_END, 0x4f, 1 }, { SDL_SCANCODE_DOWN, VK_DOWN, 0x50, 1 },
    { SDL_SCANCODE_PAGEDOWN, VK_NEXT, 0x51, 1 }, { SDL_SCANCODE_INSERT, VK_INSERT, 0x52, 1 },
    { SDL_SCANCODE_DELETE, VK_DELETE, 0x53, 1 }, { SDL_SCANCODE_LGUI, VK_LWIN, 0x5b, 1 },
    { SDL_SCANCODE_RGUI, VK_RWIN, 0x5c, 1 }, { SDL_SCANCODE_APPLICATION, VK_APPS, 0x5d, 1 },
};
#define NKEYS (int)(sizeof keymap / sizeof keymap[0])
UINT ui_vk_to_scan(UINT vk)
{
    if (vk == VK_SHIFT) vk = VK_LSHIFT;
    if (vk == VK_CONTROL) vk = VK_LCONTROL;
    if (vk == VK_MENU) vk = VK_LMENU;
    for (int i = 0; i < NKEYS; i++) if (keymap[i].vk == vk) return keymap[i].scan;
    return 0;
}
UINT ui_scan_to_vk(UINT scan)
{
    for (int i = 0; i < NKEYS; i++) if (keymap[i].scan == scan && !keymap[i].ext) {
        UINT vk = keymap[i].vk;
        if (vk == VK_LSHIFT || vk == VK_RSHIFT) return VK_SHIFT;
        if (vk == VK_LCONTROL) return VK_CONTROL;
        if (vk == VK_LMENU) return VK_MENU;
        return vk;
    }
    return 0;
}
const char *ui_scan_name(int scan, int ext)
{
    for (int i = 0; i < NKEYS; i++) if (keymap[i].scan == scan && keymap[i].ext == ext) {
        const char *n = SDL_GetScancodeName((SDL_Scancode)keymap[i].sdl);
        return n ? n : "";
    }
    return "";
}
static int mouse_x, mouse_y;
void ui_pump_input(void)
{
    if (!main_sdl) return;
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) {
        ui_raw r = {0};
        r.time = ui_ticks();
        int ox = main_w ? main_w->cr.left : 0, oy = main_w ? main_w->cr.top : 0;
        switch (e.type) {
        case SDL_MOUSEMOTION:
            r.kind = UI_RAW_MOVE; r.x = mouse_x = e.motion.x + ox; r.y = mouse_y = e.motion.y + oy;
            ui_raw_push(&r); break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
            int b = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 :
                    e.button.button == SDL_BUTTON_MIDDLE ? 3 : 0;
            if (!b) break;
            r.kind = UI_RAW_BUTTON; r.button = b; r.down = e.type == SDL_MOUSEBUTTONDOWN;
            r.x = mouse_x = e.button.x + ox; r.y = mouse_y = e.button.y + oy;
            ui_raw_push(&r); break;
        }
        case SDL_MOUSEWHEEL:
            r.kind = UI_RAW_WHEEL; r.wheel = e.wheel.y; r.x = mouse_x; r.y = mouse_y;
            ui_raw_push(&r); break;
        case SDL_KEYDOWN: case SDL_KEYUP:
            for (int i = 0; i < NKEYS; i++) if (keymap[i].sdl == (short)e.key.keysym.scancode) {
                r.kind = UI_RAW_KEY; r.down = e.type == SDL_KEYDOWN; r.vk = keymap[i].vk; r.scan = keymap[i].scan;
                r.ext = keymap[i].ext; r.repeat = e.key.repeat;
                ui_raw_push(&r); break;
            }
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE && main_w) ui_post(main_w, 0, WM_CLOSE, 0, 0);
            break;
        case SDL_QUIT:
            if (main_w) ui_post(main_w, 0, WM_CLOSE, 0, 0);
            break;
        default: break;
        }
    }
}
