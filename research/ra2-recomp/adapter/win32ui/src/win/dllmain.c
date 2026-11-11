/* dllmain.c: ra2ui.dll for the original game.exe under Wine.
 * On load it points every USER32, GDI32, COMCTL32 and IMM32 import slot of game.exe
 * at our implementation. A name with no implementation gets a stub that logs and stops.
 * Environment:
 *   RA2UI_LOG=<file>       log file (default: stderr)
 *   RA2UI_TRACE=1          trace messages and calls
 *   RA2UI_ALSO_LOAD=<dll>  load this DLL after patching (rc.dll for lifted code)
 *   RA2UI_ONLY=<names>     patch only these imports (comma list), for bisecting */
#include "../ui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
__declspec(dllimport) BOOL __stdcall VirtualProtect(void *, size_t, DWORD, DWORD *);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *);
__declspec(dllimport) DWORD __stdcall GetLastError(void);
INT UICDECL ui_wsprintfA(char *out, const char *fmt, ...);

#include "record.c"

struct ui_export { const char *dll, *name; void *fn; };
static const struct ui_export exports[] = {
#define UIFN(dll, ret, name, args) { #dll ".DLL", #name, (void *)ui_##name },
#include "../ui_api.inc"
#undef UIFN
    { "USER32.DLL", "wsprintfA", (void *)ui_wsprintfA },
    { "COMCTL32.DLL", "#17", (void *)ui_InitCommonControls },
};
#define NEXP (int)(sizeof exports / sizeof exports[0])

/* stubs for names we do not know: each loads its name into eax and jumps to missing() */
static void __cdecl missing(const char *name, void *ret)
{
    ui_fatal("win32ui: game called %s (no implementation), caller %p", name, ret);
}
static BYTE *make_stub(const char *name)
{
    static BYTE *page; static int used;
    if (!page || used + 32 > 4096) {
        __declspec(dllimport) void *__stdcall VirtualAlloc(void *, size_t, DWORD, DWORD);
        page = VirtualAlloc(0, 4096, 0x3000, 0x40); used = 0;
    }
    BYTE *s = page + used; used += 32;
    /* push [esp]; push name; call missing */
    s[0] = 0xff; s[1] = 0x34; s[2] = 0x24;
    s[3] = 0x68; memcpy(s + 4, &name, 4);
    s[8] = 0xe8; int32_t rel = (int32_t)((BYTE *)missing - (s + 13)); memcpy(s + 9, &rel, 4);
    s[13] = 0xcc;
    return s;
}
static int wanted(const char *name)
{
    const char *only = getenv("RA2UI_ONLY");
    if (!only || !*only) return 1;
    size_t n = strlen(name);
    for (const char *p = only; (p = strstr(p, name)); p += n)
        if ((p == only || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) return 1;
    return 0;
}
static void patch_imports(void)
{
    BYTE *base = GetModuleHandleA(NULL);
    BYTE *pe = base + *(DWORD *)(base + 0x3c);
    DWORD rva = *(DWORD *)(pe + 24 + 96 + 8);
    int patched = 0, stubs = 0;
    for (DWORD *d = (DWORD *)(base + rva); d[3]; d += 5) {
        const char *dll = (const char *)base + d[3];
        char up[32]; size_t k;
        for (k = 0; dll[k] && k < 31; k++) up[k] = (char)(dll[k] >= 'a' && dll[k] <= 'z' ? dll[k] - 32 : dll[k]);
        up[k] = 0;
        if (strcmp(up, "USER32.DLL") && strcmp(up, "GDI32.DLL") && strcmp(up, "COMCTL32.DLL") && strcmp(up, "IMM32.DLL")) continue;
        DWORD *names = (DWORD *)(base + (d[0] ? d[0] : d[4]));
        DWORD *iat = (DWORD *)(base + d[4]);
        for (int i = 0; names[i]; i++) {
            char ord[16]; const char *name;
            if (names[i] & 0x80000000u) { snprintf(ord, sizeof ord, "#%u", names[i] & 0xffff); name = ord; }
            else name = (const char *)base + names[i] + 2;
            if (!wanted(name)) continue;
            void *fn = NULL;
            for (int e = 0; e < NEXP; e++) if (!strcmp(exports[e].dll, up) && !strcmp(exports[e].name, name)) { fn = exports[e].fn; break; }
            if (!fn) { fn = make_stub(ui_strdup(name)); stubs++; }
            fn = rec_import(up, name, fn);
            DWORD old;
            VirtualProtect(&iat[i], 4, 0x04, &old);
            iat[i] = (DWORD)(uintptr_t)fn;
            VirtualProtect(&iat[i], 4, old, &old);
            patched++;
        }
    }
    ui_log("win32ui: patched %d import slots (%d without implementation)", patched, stubs);
}
BOOL __stdcall DllMain(HINSTANCE inst, DWORD reason, void *res)
{
    (void)inst; (void)res;
    if (reason == 1) {
        rec_init();
        patch_imports();
        const char *also = getenv("RA2UI_ALSO_LOAD");
        if (also && *also && !LoadLibraryA(also)) ui_log("win32ui: cannot load %s (error %u)", also, GetLastError());
    }
    return 1;
}
