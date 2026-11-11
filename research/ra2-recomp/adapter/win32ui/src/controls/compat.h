/* Private control compatibility layer. Public window extra bytes stay empty. */
#ifndef CTL_COMPAT_H
#define CTL_COMPAT_H
#include "../ui.h"
#include <stdlib.h>
#include <string.h>
static inline int ctl_text_height(HWND h, HFONT font)
{
    LONG tm[14];
    HDC dc = ui_GetDC(h);
    HGDIOBJ old = font ? ui_SelectObject(dc, font) : 0;
    ui_GetTextMetricsA(dc, tm);
    if (font) ui_SelectObject(dc, old);
    ui_ReleaseDC(h, dc);
    return tm[0];
}
static inline void ctl_notify(ui_win *w, UINT code)
{
    if (w->parent && w->parent != ui_desktop())
        ui_send(w->parent, WM_COMMAND, MAKELONG(w->id, code), w->h);
}
enum { BS_PUSHBUTTON, BS_DEFPUSHBUTTON, BS_CHECKBOX, BS_AUTOCHECKBOX, BS_RADIOBUTTON, BS_3STATE, BS_AUTO3STATE,
       BS_GROUPBOX, BS_USERBUTTON, BS_AUTORADIOBUTTON, BS_PUSHBOX, BS_OWNERDRAW, BS_TYPEMASK = 0xf, BS_NOTIFY = 0x4000 };
enum { BST_CHECKED = 1, BST_INDETERMINATE = 2, BST_PUSHED = 4, BST_FOCUS = 8, BTN_PRESSED = 0x40, BTN_NSTATES = 0xf };
enum { BN_CLICKED = 0, BN_DOUBLECLICKED = 5, BN_SETFOCUS = 6, BN_KILLFOCUS = 7 };
enum { BM_GETCHECK = 0xf0, BM_SETCHECK, BM_GETSTATE, BM_SETSTATE, BM_SETSTYLE, BM_CLICK, BM_GETIMAGE, BM_SETIMAGE };
enum { ODT_LISTBOX = 2, ODT_COMBOBOX = 3, ODT_BUTTON = 4, ODA_DRAWENTIRE = 1, ODA_SELECT = 2, ODA_FOCUS = 4,
       ODS_SELECTED = 1, ODS_DISABLED = 4, ODS_CHECKED = 8, ODS_FOCUS = 0x10, ODS_COMBOBOXEDIT = 0x1000 };
typedef struct { UINT CtlType, CtlID, itemID, itemAction, itemState; HWND hwndItem; HDC hDC; RECT rcItem; DWORD itemData; } DRAWITEMSTRUCT;
typedef struct { UINT CtlType, CtlID, itemID, itemWidth, itemHeight; DWORD itemData; } MEASUREITEMSTRUCT;
typedef struct { UINT CtlType, CtlID, itemID; HWND hwndItem; DWORD itemData; } DELETEITEMSTRUCT;
static inline void notify_parent(ui_win *w, UINT code)
{
    ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : NULL;
    if (p) ui_send(p, WM_COMMAND, MAKELONG(w->id, code), (LPARAM)w->h);
}
static inline int pt_in_client(ui_win *w, LPARAM lp)
{
    int x = (int16_t)LOWORD(lp), y = (int16_t)HIWORD(lp);
    return x >= 0 && y >= 0 && x < w->cr.right - w->cr.left && y < w->cr.bottom - w->cr.top;
}

LRESULT UIAPI ui_DefDlgProcA(HWND, UINT, WPARAM, LPARAM);
HWND ui_create(DWORD, const char *, const char *, DWORD, INT, INT, INT, INT, HWND, HMENU, HINSTANCE, void *, int);
LRESULT UIAPI button_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI static_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI listbox_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI combo_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI edit_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI trackbar_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI progress_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT UIAPI hotkey_proc(HWND, UINT, WPARAM, LPARAM);
void ctl_listbox_free(ui_win *w);
#endif
