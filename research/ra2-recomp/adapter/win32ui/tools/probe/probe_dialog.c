/* probe_dialog.exe: create game.exe dialogs from their templates and print every window rect.
 * usage: probe_dialog.exe <game.exe> <id>...   env RA2UI=1: LoadLibraryA("ra2ui.dll") first (our code).
 * Prints: dialog id, then for each child: class id style rect (parent client coordinates). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static INT_PTR CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return m == WM_INITDIALOG; }
int main(int argc, char **argv)
{
    if (getenv("RA2UI") && *getenv("RA2UI") == '1' && !LoadLibraryA(getenv("RA2UI_DLL") ? getenv("RA2UI_DLL") : "ra2ui.dll")) { printf("no ra2ui.dll\n"); return 1; }
    HMODULE g = LoadLibraryExA(argv[1], 0, LOAD_LIBRARY_AS_DATAFILE);
    if (!g) { printf("cannot load %s\n", argv[1]); return 1; }
    HWND top = CreateWindowExA(0, "Static", "top", WS_POPUP, 0, 0, 1024, 768, 0, 0, 0, 0);
    for (int a = 2; a < argc; a++) {
        int id = atoi(argv[a]);
        HRSRC r = FindResourceA(g, MAKEINTRESOURCEA(id), (LPCSTR)RT_DIALOG);
        if (!r) { printf("dialog %d: none\n", id); continue; }
        void *t = LockResource(LoadResource(g, r));
        HWND d = CreateDialogIndirectParamA(GetModuleHandleA(0), t, top, proc, 0);
        RECT wr, cr; GetWindowRect(d, &wr); GetClientRect(d, &cr);
        printf("dialog %d style %08lx window %ld,%ld,%ld,%ld client %ldx%ld\n", id, GetWindowLongA(d, GWL_STYLE), wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom);
        for (HWND c = GetWindow(d, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            char cls[64]; GetClassNameA(c, cls, sizeof cls);
            RECT rr; GetWindowRect(c, &rr); ScreenToClient(d, (POINT *)&rr.left); ScreenToClient(d, (POINT *)&rr.right);
            RECT cc; GetClientRect(c, &cc);
            printf("  %-18s %5d %08lx %ld,%ld,%ld,%ld client %ldx%ld\n", cls, GetDlgCtrlID(c), GetWindowLongA(c, GWL_STYLE), rr.left, rr.top, rr.right, rr.bottom, cc.right, cc.bottom);
        }
        DestroyWindow(d);
    }
    return 0;
}
