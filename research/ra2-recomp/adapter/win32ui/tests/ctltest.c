/* Compare Wine and RA2UI control messages using game.exe dialog resources. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *out;
static HWND objects[512];
static unsigned nobjects;
static unsigned handle(HWND h)
{
    if (!h) return 0;
    for (unsigned i = 0; i < nobjects; i++) if (objects[i] == h) return i + 1;
    objects[nobjects++] = h; return nobjects;
}
static INT_PTR CALLBACK parent(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    fprintf(out, "parent %u %04x", handle(h), m);
    switch (m) {
    case WM_COMMAND: fprintf(out, " %u %u %u", LOWORD(wp), HIWORD(wp), handle((HWND)lp)); break;
    case WM_HSCROLL: case WM_VSCROLL:
        fprintf(out, " %u %u %u", LOWORD(wp), HIWORD(wp), handle((HWND)lp)); break;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *s = (void *)lp;
        fprintf(out, " %u %u %u %u %u %lu", s->CtlType, s->CtlID, s->itemID, s->itemWidth, s->itemHeight, s->itemData);
        s->itemHeight = 18; break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *s = (void *)lp;
        fprintf(out, " %u %u %u %u %u %u %ld,%ld,%ld,%ld %lu", s->CtlType, s->CtlID,
            s->itemID, s->itemAction, s->itemState, handle(s->hwndItem),
            s->rcItem.left, s->rcItem.top, s->rcItem.right, s->rcItem.bottom, s->itemData); break;
    }
    case WM_DELETEITEM: {
        DELETEITEMSTRUCT *s = (void *)lp;
        fprintf(out, " %u %u %u %u %lu", s->CtlType, s->CtlID, s->itemID, handle(s->hwndItem), s->itemData); break;
    }
    case WM_CTLCOLORBTN: case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT:
        fprintf(out, " %u", handle((HWND)lp)); break;
    }
    fputc('\n', out); fflush(out);
    if (m == WM_INITDIALOG) return FALSE;
    if (m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_DELETEITEM) return TRUE;
    if (m >= WM_CTLCOLORMSGBOX && m <= WM_CTLCOLORSTATIC) return (INT_PTR)GetStockObject(WHITE_BRUSH);
    return FALSE;
}
static void send(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    fprintf(out, "send %u %04x\n", handle(h), m);
    LRESULT r = SendMessageA(h, m, wp, lp);
    fprintf(out, "result %ld\n", r);
}
static void rect(HWND h)
{
    RECT r; GetWindowRect(h, &r);
    fprintf(out, "size %ld %ld\n", r.right-r.left, r.bottom-r.top);
}
static void drive(HWND h)
{
    char cls[80]; GetClassNameA(h, cls, sizeof cls);
    fprintf(out, "sequence %s %d\n", cls, GetDlgCtrlID(h));
    rect(h);
    send(h, WM_GETDLGCODE, 0, 0);
    if (!lstrcmpiA(cls, "Button")) {
        send(h, BM_GETCHECK, 0, 0); send(h, BM_SETCHECK, 1, 0); send(h, BM_GETCHECK, 0, 0);
        send(h, BM_CLICK, 0, 0); send(h, BM_GETCHECK, 0, 0);
    } else if (!lstrcmpiA(cls, "Static")) {
        send(h, WM_SETTEXT, 0, (LPARAM)"test");
        send(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4,4)); send(h, WM_LBUTTONDBLCLK, 0, MAKELPARAM(4,4));
    } else if (!lstrcmpiA(cls, "ComboBox") || !lstrcmpiA(cls, "ListBox")) {
        int cb = !lstrcmpiA(cls, "ComboBox");
        send(h, cb?CB_ADDSTRING:LB_ADDSTRING, 0, (LPARAM)"Zulu");
        send(h, cb?CB_ADDSTRING:LB_ADDSTRING, 0, (LPARAM)"Alpha");
        send(h, cb?CB_GETCOUNT:LB_GETCOUNT, 0, 0);
        send(h, cb?CB_SETCURSEL:LB_SETCURSEL, 0, 0);
        send(h, WM_KEYDOWN, VK_DOWN, 0);
        send(h, cb?CB_GETCURSEL:LB_GETCURSEL, 0, 0);
        if (cb) {
            RECT r; send(h, CB_GETITEMHEIGHT, -1, 0);
            send(h, CB_SETITEMHEIGHT, -1, 22); rect(h);
            send(h, CB_GETITEMHEIGHT, -1, 0);
            send(h, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&r);
            fprintf(out, "drop-size %ld %ld\n", r.right-r.left, r.bottom-r.top);
            send(h, CB_SHOWDROPDOWN, 1, 0); send(h, WM_KEYDOWN, VK_DOWN, 0);
            send(h, WM_KEYDOWN, VK_RETURN, 0); send(h, CB_SHOWDROPDOWN, 0, 0);
        }
        send(h, cb?CB_RESETCONTENT:LB_RESETCONTENT, 0, 0);
    } else if (!lstrcmpiA(cls, "Edit")) {
        send(h, WM_SETTEXT, 0, (LPARAM)"abc"); send(h, EM_SETSEL, 1, 2);
        send(h, EM_REPLACESEL, 0, (LPARAM)"XYZ"); send(h, WM_CHAR, 'q', 0);
        send(h, EM_GETSEL, 0, 0); send(h, WM_GETTEXTLENGTH, 0, 0);
    } else if (!lstrcmpiA(cls, TRACKBAR_CLASSA)) {
        send(h, TBM_GETPAGESIZE, 0, 0); send(h, TBM_SETRANGE, 0, MAKELPARAM(0,20));
        send(h, TBM_GETPAGESIZE, 0, 0); send(h, TBM_SETPOS, 0, 10);
        send(h, WM_KEYDOWN, VK_RIGHT, 0); send(h, WM_KEYUP, VK_RIGHT, 0);
        send(h, TBM_GETPOS, 0, 0);
    } else if (!lstrcmpiA(cls, PROGRESS_CLASSA)) {
        send(h, PBM_SETRANGE32, 10, 20); send(h, PBM_GETPOS, 0, 0);
        send(h, PBM_SETPOS, 99, 0); send(h, PBM_GETPOS, 0, 0);
        send(h, PBM_SETSTEP, -3, 0); send(h, PBM_STEPIT, 0, 0); send(h, PBM_GETPOS, 0, 0);
        send(h, PBM_DELTAPOS, -100, 0); send(h, PBM_GETPOS, 0, 0);
    } else if (!lstrcmpiA(cls, HOTKEY_CLASSA)) {
        send(h, HKM_SETRULES, HKCOMB_NONE, HOTKEYF_CONTROL);
        send(h, WM_KEYDOWN, 'A', 0); send(h, HKM_GETHOTKEY, 0, 0);
        send(h, WM_KEYDOWN, VK_SHIFT, 0); send(h, WM_KEYDOWN, 'B', 0);
        send(h, WM_KEYUP, VK_SHIFT, 0); send(h, HKM_GETHOTKEY, 0, 0);
        send(h, WM_KEYDOWN, VK_BACK, 0); send(h, HKM_GETHOTKEY, 0, 0);
    }
    fprintf(out, "end-sequence\n"); fflush(out);
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    out = fopen(argv[2], "w"); if (!out) return 3;
    if (getenv("RA2UI") && !LoadLibraryA("ra2ui.dll")) return 4;
    InitCommonControls();
    HMODULE game = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) { fprintf(out, "load-error %lu\n", GetLastError()); return 5; }
    HWND root = CreateWindowExA(0, "Static", "ctltest", WS_POPUP, 0, 0, 1024, 768, NULL, NULL, NULL, NULL);
    int ids[] = {188,192};
    for (unsigned i = 0; i < sizeof ids/sizeof ids[0]; i++) {
        fprintf(out, "dialog %d\n", ids[i]);
        HWND dlg = CreateDialogParamA(game, MAKEINTRESOURCEA(ids[i]), root, parent, 0);
        if (!dlg) { fprintf(out, "create-error %lu\n", GetLastError()); return 6; }
        for (HWND c=GetWindow(dlg,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) handle(c);
        if (getenv("CTLTEST_VISIBLE")) { ShowWindow(root, SW_SHOWNOACTIVATE); ShowWindow(dlg, SW_SHOWNOACTIVATE); UpdateWindow(dlg); }
        /* Choose one control of each class, in resource order. */
        const char *classes[] = {"Button", "Static", "ListBox", "ComboBox", "Edit", TRACKBAR_CLASSA};
        for (unsigned j=0; j<sizeof classes/sizeof classes[0];j++) {
            for (HWND c=GetWindow(dlg,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) {
                char name[80]; GetClassNameA(c,name,sizeof name);
                if (!lstrcmpiA(name,classes[j])) { drive(c); break; }
            }
        }
        for (HWND c=GetWindow(dlg,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) {
            char name[80]; GetClassNameA(c,name,sizeof name);
            if (!lstrcmpiA(name,"Button") && (GetWindowLongA(c,GWL_STYLE)&15)==BS_AUTOCHECKBOX) { drive(c); break; }
        }
        if (!i) {
            HWND p=CreateWindowExA(0,PROGRESS_CLASSA,"",WS_CHILD,0,0,100,20,dlg,(HMENU)9001,NULL,NULL); drive(p);
            HWND k=CreateWindowExA(0,HOTKEY_CLASSA,"",WS_CHILD,0,0,100,20,dlg,(HMENU)9002,NULL,NULL); drive(k);
        }
        DestroyWindow(dlg);
    }
    DestroyWindow(root);
    FreeLibrary(game); fclose(out); return 0;
}
