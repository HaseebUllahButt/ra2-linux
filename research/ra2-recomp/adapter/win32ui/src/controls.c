/* controls.c: built-in window classes. Button and Static follow Wine's user32 behaviour.
 * List box, combo box, edit, trackbar, progress, hotkey and list view are first versions
 * with state only (stage 1); stage 2 replaces them with full ports. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LRESULT UIAPI ui_DefDlgProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp);
HWND ui_create(DWORD ex, const char *clsname, const char *name, DWORD style, INT x, INT y, INT cx, INT cy,
               HWND hparent, HMENU menu, HINSTANCE inst, void *param, int dialog);
static void notify_parent(ui_win *w, UINT code)
{
    ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : NULL;
    if (p) ui_send(p, WM_COMMAND, MAKELONG(w->id, code), (LPARAM)w->h);
}
static int pt_in_client(ui_win *w, LPARAM lp)
{
    int x = (int16_t)LOWORD(lp), y = (int16_t)HIWORD(lp);
    return x >= 0 && y >= 0 && x < w->cr.right - w->cr.left && y < w->cr.bottom - w->cr.top;
}

/* ================= Button ================= */
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
typedef struct { int state; HFONT font; } btn_state;
static btn_state *btn(ui_win *w) { if (!w->ctl) w->ctl = calloc(1, sizeof(btn_state)); return w->ctl; }
static void btn_paint(ui_win *w, UINT action)
{
    UINT type = w->style & BS_TYPEMASK;
    if (!ui_is_visible(w)) return;
    HDC dc = ui_GetDC(w->h);
    ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
    if (type == BS_OWNERDRAW) {
        btn_state *s = btn(w);
        DRAWITEMSTRUCT d = { ODT_BUTTON, w->id, 0, action, 0, w->h, dc, { 0, 0, w->cr.right - w->cr.left, w->cr.bottom - w->cr.top }, 0 };
        if (s->state & BST_PUSHED) d.itemState |= ODS_SELECTED;
        if (s->state & BST_FOCUS) d.itemState |= ODS_FOCUS;
        if (w->style & WS_DISABLED) d.itemState |= ODS_DISABLED;
        ui_send(p, WM_CTLCOLORBTN, dc, (LPARAM)w->h);
        ui_send(p, WM_DRAWITEM, w->id, (LPARAM)&d);
    } else if (type == BS_GROUPBOX || type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_RADIOBUTTON ||
               type == BS_AUTORADIOBUTTON || type == BS_3STATE || type == BS_AUTO3STATE) {
        ui_send(p, WM_CTLCOLORSTATIC, dc, (LPARAM)w->h);
    } else {
        ui_send(p, WM_CTLCOLORBTN, dc, (LPARAM)w->h);
    }
    ui_ReleaseDC(w->h, dc);
}
static void check_auto_radio(ui_win *w)
{
    ui_win *p = w->parent;
    /* the group: from the WS_GROUP item at or before w, to the next WS_GROUP item */
    ui_win *start = w;
    while (start->prev && !(start->style & WS_GROUP)) start = start->prev;
    for (ui_win *c = start; c; c = c->next) {
        if (c != start && (c->style & WS_GROUP)) break;
        if (c->cls == w->cls && (c->style & BS_TYPEMASK) == BS_AUTORADIOBUTTON)
            ui_send(c, BM_SETCHECK, c == w, 0);
    }
    (void)p;
}
static LRESULT UIAPI button_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    UINT type = w->style & BS_TYPEMASK;
    btn_state *s = btn(w);
    switch (msg) {
    case WM_GETDLGCODE:
        switch (type) {
        case BS_USERBUTTON: case BS_PUSHBUTTON: return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
        case BS_DEFPUSHBUTTON: return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
        case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON: return DLGC_BUTTON | DLGC_RADIOBUTTON;
        case BS_GROUPBOX: return DLGC_STATIC;
        default: return DLGC_BUTTON;
        }
    case WM_ENABLE: btn_paint(w, ODA_DRAWENTIRE); return 0;
    case WM_CREATE:
        if (type > BS_OWNERDRAW) return -1;
        if (type == BS_USERBUTTON) w->style = (w->style & ~BS_TYPEMASK) | BS_PUSHBUTTON;
        s->state = 0;
        return 0;
    case WM_ERASEBKGND:
        if (type == BS_OWNERDRAW) {
            ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
            ui_send(p, WM_CTLCOLORBTN, wp, (LPARAM)h);
        }
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        if (type == BS_OWNERDRAW) {
            ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
            DRAWITEMSTRUCT d = { ODT_BUTTON, w->id, 0, ODA_DRAWENTIRE, 0, h, dc, { 0, 0, w->cr.right - w->cr.left, w->cr.bottom - w->cr.top }, 0 };
            if (s->state & BST_PUSHED) d.itemState |= ODS_SELECTED;
            if (s->state & BST_FOCUS) d.itemState |= ODS_FOCUS;
            if (w->style & WS_DISABLED) d.itemState |= ODS_DISABLED;
            ui_send(p, WM_CTLCOLORBTN, dc, (LPARAM)h);
            ui_send(p, WM_DRAWITEM, w->id, (LPARAM)&d);
        } else {
            ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
            ui_send(p, (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) ? WM_CTLCOLORBTN : WM_CTLCOLORSTATIC, dc, (LPARAM)h);
        }
        if (!wp) ui_EndPaint(h, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_SPACE) { ui_SetCapture(h); ui_send(w, BM_SETSTATE, 1, 0); if ((w = ui_get(h))) btn(w)->state |= BTN_PRESSED; }
        return 0;
    case WM_LBUTTONDBLCLK:
        if ((w->style & BS_NOTIFY) || type == BS_RADIOBUTTON || type == BS_USERBUTTON || type == BS_OWNERDRAW) {
            notify_parent(w, BN_DOUBLECLICKED); return 0;
        }
        /* fall through */
    case WM_LBUTTONDOWN:
        ui_SetCapture(h);
        if (!(w = ui_get(h))) return 0;
        btn(w)->state |= BTN_PRESSED;
        ui_SetFocus(h);
        ui_send_h(h, BM_SETSTATE, 1, 0);
        return 0;
    case WM_KEYUP:
        if (wp != VK_SPACE) return 0;
        /* fall through */
    case WM_LBUTTONUP: {
        int st = s->state;
        if (!(st & BTN_PRESSED)) return 0;
        st &= BTN_NSTATES; s->state = st;
        if (!(st & BST_PUSHED)) { ui_ReleaseCapture(); return 0; }
        ui_send(w, BM_SETSTATE, 0, 0);
        if (!(w = ui_get(h))) return 0;
        if (msg == WM_KEYUP || pt_in_client(w, lp)) {
            st = btn(w)->state;
            switch (type) {
            case BS_AUTOCHECKBOX: ui_send(w, BM_SETCHECK, !(st & BST_CHECKED), 0); break;
            case BS_AUTORADIOBUTTON: check_auto_radio(w); break;
            case BS_AUTO3STATE: ui_send(w, BM_SETCHECK, (st & BST_INDETERMINATE) ? 0 : ((st & 3) + 1), 0); break;
            }
            ui_ReleaseCapture();
            if ((w = ui_get(h))) notify_parent(w, BN_CLICKED);
        } else ui_ReleaseCapture();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (h == (HWND)lp) return 0;
        if (s->state & BTN_PRESSED) {
            s->state &= BTN_NSTATES;
            if (s->state & BST_PUSHED) ui_send(w, BM_SETSTATE, 0, 0);
        }
        return 0;
    case WM_MOUSEMOVE:
        if ((wp & MK_LBUTTON) && ui_capture() == w) ui_send(w, BM_SETSTATE, pt_in_client(w, lp), 0);
        return 0;
    case WM_SETTEXT:
        if (ui_is_visible(w)) {
            HDC dc = ui_GetDC(h);
            ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
            UINT m = (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_USERBUTTON || type == BS_OWNERDRAW) ? WM_CTLCOLORBTN : WM_CTLCOLORSTATIC;
            ui_send(p, m, dc, (LPARAM)h);
            ui_ReleaseDC(h, dc);
        }
        ui_DefWindowProcA(h, WM_SETTEXT, wp, lp);
        if (!(w = ui_get(h))) return 1;
        if (type == BS_GROUPBOX) ui_InvalidateRect(h, NULL, 1); else btn_paint(w, ODA_DRAWENTIRE);
        return 1;
    case WM_SETFONT: s->font = wp; if (lp) ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_GETFONT: return (LRESULT)s->font;
    case WM_SETFOCUS:
        s->state |= BST_FOCUS;
        btn_paint(w, ODA_FOCUS);
        if (!(w = ui_get(h))) return 0;
        if (w->style & BS_NOTIFY) notify_parent(w, BN_SETFOCUS);
        if ((type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) && !(btn(w)->state & (BST_CHECKED | BTN_PRESSED)))
            notify_parent(w, BN_CLICKED);
        return 0;
    case WM_KILLFOCUS: {
        int st = s->state;
        s->state = st & ~BST_FOCUS;
        btn_paint(w, ODA_FOCUS);
        if ((st & BTN_PRESSED) && ui_capture() == w) ui_ReleaseCapture();
        if (!(w = ui_get(h))) return 0;
        if (w->style & BS_NOTIFY) notify_parent(w, BN_KILLFOCUS);
        ui_InvalidateRect(h, NULL, 0);
        return 0;
    }
    case BM_SETSTYLE:
        w->style = (w->style & ~BS_TYPEMASK) | (wp & BS_TYPEMASK);
        if (lp) ui_InvalidateRect(h, NULL, 1);
        return 0;
    case BM_CLICK: ui_send(w, WM_LBUTTONDOWN, 0, 0); ui_send_h(h, WM_LBUTTONUP, 0, 0); return 0;
    case BM_SETIMAGE: case BM_GETIMAGE: return 0;
    case BM_GETCHECK: return s->state & 3;
    case BM_SETCHECK: {
        static const UINT maxc[] = { 0, 0, 1, 1, 1, 2, 2, 0, 0, 1, 0, 0 };
        if (wp > maxc[type]) wp = maxc[type];
        if (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) { if (wp) w->style |= WS_TABSTOP; else w->style &= ~WS_TABSTOP; }
        if ((UINT)(s->state & 3) != wp) { s->state = (s->state & ~3) | (int)wp; btn_paint(w, ODA_SELECT); }
        return 0;
    }
    case BM_GETSTATE: return s->state;
    case BM_SETSTATE:
        if (wp) s->state |= BST_PUSHED; else s->state &= ~BST_PUSHED;
        btn_paint(w, ODA_SELECT);
        return 0;
    case WM_NCHITTEST:
        if (type == BS_GROUPBOX) return HTTRANSPARENT;
        break;
    }
    return ui_DefWindowProcA(h, msg, wp, lp);
}

/* ================= Static ================= */
enum { SS_TYPEMASK = 0x1f, SS_ICON = 3, SS_BITMAP = 0xe, SS_ENHMETAFILE = 0xf, SS_NOTIFY = 0x100 };
static LRESULT UIAPI static_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    UINT type = w->style & SS_TYPEMASK;
    switch (msg) {
    case WM_CREATE:
        if (type > 0x12) return -1;
        return 0;
    case WM_NCCREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)(uintptr_t)lp;
        if (type == SS_ICON || type == SS_BITMAP) { (void)cs; return ui_DefWindowProcA(h, msg, wp, 0) ? 1 : 0; }
        return ui_DefWindowProcA(h, msg, wp, lp);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        if (ui_is_visible(w) && type <= 0xc && type != 4 && type != 5 && type != 6 && type != 7 && type != 8 && type != 9) {
            ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
            ui_send(p, WM_CTLCOLORSTATIC, dc, (LPARAM)h);
        }
        if (!wp) ui_EndPaint(h, &ps);
        return 0;
    }
    case WM_ENABLE: ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_NCHITTEST:
        if (w->style & SS_NOTIFY) return HTCLIENT;
        return HTTRANSPARENT;
    case WM_GETDLGCODE: return DLGC_STATIC;
    case WM_LBUTTONDOWN:
        if (w->style & SS_NOTIFY) notify_parent(w, 0 /* STN_CLICKED */);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (w->style & SS_NOTIFY) notify_parent(w, 1 /* STN_DBLCLK */);
        return 0;
    case WM_SETTEXT:
        if (type == SS_ICON || type == SS_BITMAP) return 1;
        ui_DefWindowProcA(h, msg, wp, lp);
        if (type == 0xc /* SS_LEFTNOWORDWRAP */ || type <= 2) ui_InvalidateRect(h, NULL, 1);
        else ui_InvalidateRect(h, NULL, 0);
        return 1;
    case WM_SETFONT: if (w->ctl == NULL) w->ctl = calloc(1, sizeof(HFONT)); *(HFONT *)w->ctl = wp; if (LOWORD(lp)) ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_GETFONT: return w->ctl ? (LRESULT)*(HFONT *)w->ctl : 0;
    case 0x170: case 0x171: case 0x172: case 0x173: return 0;   /* STM_* images */
    }
    return ui_DefWindowProcA(h, msg, wp, lp);
}

/* ================= List box (first version) ================= */
typedef struct { char *s; DWORD data; int sel; } lb_item;
typedef struct { lb_item *it; int n, cap, cur, top, item_h, anchor; HFONT font; int combo; HWND owner; int captured; } lb_state;
static lb_state *lbs(ui_win *w) { if (!w->ctl) { lb_state *s = calloc(1, sizeof *s); s->cur = -1; s->item_h = 13; w->ctl = s; } return w->ctl; }
enum { LBS_NOTIFY = 1, LBS_SORT = 2, LBS_MULTIPLESEL = 8, LBS_OWNERDRAWFIXED = 0x10, LBS_OWNERDRAWVARIABLE = 0x20,
       LBS_HASSTRINGS = 0x40, LBS_EXTENDEDSEL = 0x800 };
enum { LBN_SELCHANGE = 1, LBN_DBLCLK = 2, LBN_SELCANCEL = 3, LBN_SETFOCUS = 4, LBN_KILLFOCUS = 5 };
static int lb_strings(ui_win *w) { return !(w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (w->style & LBS_HASSTRINGS); }
static int lb_insert(ui_win *w, int at, LPARAM lp)
{
    lb_state *s = lbs(w);
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 16; s->it = realloc(s->it, sizeof(lb_item) * (size_t)s->cap); }
    if (at < 0 || at > s->n) at = s->n;
    if (at == s->n && (w->style & LBS_SORT) && lb_strings(w)) {
        const char *str = (const char *)(uintptr_t)lp;
        for (at = 0; at < s->n && ui_stricmp(s->it[at].s, str) <= 0; at++) {}
    }
    memmove(s->it + at + 1, s->it + at, sizeof(lb_item) * (size_t)(s->n - at));
    lb_item *it = &s->it[at]; memset(it, 0, sizeof *it);
    if (lb_strings(w)) it->s = ui_strdup((const char *)(uintptr_t)lp); else { it->s = ui_strdup(""); it->data = (DWORD)lp; }
    s->n++;
    if (s->cur >= at) s->cur++;
    if ((w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) == LBS_OWNERDRAWVARIABLE) {
        MEASUREITEMSTRUCT mi = { ODT_LISTBOX, w->id, (UINT)at, (UINT)(w->cr.right - w->cr.left), (UINT)s->item_h, it->data };
        ui_send_h(s->owner ? s->owner : ui_handle(w->parent), WM_MEASUREITEM, w->id, (LPARAM)&mi);
    }
    ui_InvalidateRect(w->h, NULL, 1);
    return at;
}
static void lb_delete(ui_win *w, int i)
{
    lb_state *s = lbs(w);
    if (w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) {
        DELETEITEMSTRUCT di = { ODT_LISTBOX, w->id, (UINT)i, w->h, s->it[i].data };
        ui_send_h(s->owner ? s->owner : ui_handle(w->parent), WM_DELETEITEM, w->id, (LPARAM)&di);
    }
    free(s->it[i].s);
    memmove(s->it + i, s->it + i + 1, sizeof(lb_item) * (size_t)(s->n - i - 1));
    s->n--;
    if (s->cur == i) s->cur = -1; else if (s->cur > i) s->cur--;
    if (s->top > 0 && s->top >= s->n) s->top = s->n - 1;
}
static int lb_find(ui_win *w, int start, const char *str, int exact)
{
    lb_state *s = lbs(w);
    if (!s->n) return -1;
    size_t len = strlen(str);
    for (int k = 1; k <= s->n; k++) {
        int i = (start + k) % s->n;
        if (start < 0) i = k - 1;
        if (!lb_strings(w)) { if (s->it[i].data == (DWORD)(uintptr_t)str) return i; continue; }
        if (exact ? !ui_stricmp(s->it[i].s, str) : !strncasecmp(s->it[i].s, str, len)) return i;
    }
    return -1;
}
static int lb_visible_rows(ui_win *w) { lb_state *s = lbs(w); int r = (w->cr.bottom - w->cr.top) / (s->item_h ? s->item_h : 1); return r > 0 ? r : 1; }
static void lb_draw_item(ui_win *w, HDC dc, int i, UINT action)
{
    lb_state *s = lbs(w);
    if (!(w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE))) return;
    ui_win *owner = s->owner ? ui_get(s->owner) : w->parent;
    DRAWITEMSTRUCT d = { ODT_LISTBOX, w->id, (UINT)i, action, 0, w->h, dc, { 0, 0, 0, 0 }, 0 };
    d.rcItem.top = (i - s->top) * s->item_h; d.rcItem.bottom = d.rcItem.top + s->item_h; d.rcItem.right = w->cr.right - w->cr.left;
    if (i >= 0 && i < s->n) {
        d.itemData = s->it[i].data;
        if ((w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) ? s->it[i].sel : i == s->cur) d.itemState |= ODS_SELECTED;
    }
    if (w->style & WS_DISABLED) d.itemState |= ODS_DISABLED;
    if (ui_focus() == w && i == s->cur) d.itemState |= ODS_FOCUS;
    ui_send(owner, WM_DRAWITEM, w->id, (LPARAM)&d);
}
static void lb_set_cur(ui_win *w, int i, int notify)
{
    lb_state *s = lbs(w);
    if (i >= s->n) i = -1;
    if (i == s->cur) return;
    s->cur = i;
    if (i >= 0) {
        int rows = lb_visible_rows(w);
        if (i < s->top) s->top = i; else if (i >= s->top + rows) s->top = i - rows + 1;
    }
    ui_InvalidateRect(w->h, NULL, 1);
    if (notify && (w->style & LBS_NOTIFY)) notify_parent(w, LBN_SELCHANGE);
}
static LRESULT UIAPI listbox_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    lb_state *s = lbs(w);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)(uintptr_t)lp;
        s->owner = cs ? cs->hwndParent : 0;
        if (w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) {
            MEASUREITEMSTRUCT mi = { ODT_LISTBOX, w->id, 0, (UINT)(w->cr.right - w->cr.left), 13, 0 };
            ui_send_h(s->owner, WM_MEASUREITEM, w->id, (LPARAM)&mi);
            if ((w = ui_get(h))) lbs(w)->item_h = mi.itemHeight ? (int)mi.itemHeight : 13;
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        ui_win *p = s->owner ? ui_get(s->owner) : w->parent;
        ui_send(p, WM_CTLCOLORLISTBOX, dc, (LPARAM)h);
        if ((w = ui_get(h))) {
            s = lbs(w);
            int rows = lb_visible_rows(w);
            for (int i = s->top; i < s->n && i <= s->top + rows; i++) lb_draw_item(w, dc, i, ODA_DRAWENTIRE);
        }
        if (!wp) ui_EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFONT: s->font = wp; if (lp) ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_GETFONT: return (LRESULT)s->font;
    case WM_SETFOCUS: if (w->style & LBS_NOTIFY) notify_parent(w, LBN_SETFOCUS); return 0;
    case WM_KILLFOCUS: if (w->style & LBS_NOTIFY) notify_parent(w, LBN_KILLFOCUS); return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        int y = (int16_t)HIWORD(lp);
        int i = s->top + y / (s->item_h ? s->item_h : 1);
        if (!s->combo) ui_SetFocus(h);
        if (!(w = ui_get(h))) return 0;
        s = lbs(w);
        if (i < s->n) {
            if (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) { s->it[i].sel = !s->it[i].sel; s->cur = i; ui_InvalidateRect(h, NULL, 1); if (w->style & LBS_NOTIFY) notify_parent(w, LBN_SELCHANGE); }
            else lb_set_cur(w, i, 1);
        }
        if (!(w = ui_get(h))) return 0;
        if (msg == WM_LBUTTONDBLCLK && (w->style & LBS_NOTIFY)) notify_parent(w, LBN_DBLCLK);
        else if (!s->combo) { ui_SetCapture(h); lbs(w)->captured = 1; }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (s->captured && (wp & MK_LBUTTON)) {
            int y = (int16_t)HIWORD(lp);
            if (y >= 0 && y < w->cr.bottom - w->cr.top) {
                int i = s->top + y / (s->item_h ? s->item_h : 1);
                if (i < s->n && !(w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL))) lb_set_cur(w, i, 1);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (s->captured) { s->captured = 0; ui_ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED: s->captured = 0; return 0;
    case WM_KEYDOWN: {
        int i = s->cur;
        switch (wp) {
        case VK_UP: i = i > 0 ? i - 1 : 0; break;
        case VK_DOWN: i = i + 1 < s->n ? i + 1 : s->n - 1; break;
        case VK_HOME: i = 0; break;
        case VK_END: i = s->n - 1; break;
        case VK_PRIOR: i -= lb_visible_rows(w); if (i < 0) i = 0; break;
        case VK_NEXT: i += lb_visible_rows(w); if (i >= s->n) i = s->n - 1; break;
        default: return 0;
        }
        if (s->n) lb_set_cur(w, i, 1);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = (int16_t)HIWORD(wp) / 120;
        s->top -= d * 3; if (s->top > s->n - 1) s->top = s->n - 1; if (s->top < 0) s->top = 0;
        ui_InvalidateRect(h, NULL, 1);
        return 0;
    }
    case 0x180: return lb_insert(w, -1, lp);                  /* LB_ADDSTRING */
    case 0x181: return lb_insert(w, (int)wp == -1 ? s->n : (int)wp, lp); /* LB_INSERTSTRING */
    case 0x182: if ((int)wp < 0 || (int)wp >= s->n) return -1; lb_delete(w, (int)wp); ui_InvalidateRect(h, NULL, 1); return lbs(w)->n;
    case 0x184: while (s->n) lb_delete(w, s->n - 1); s->top = 0; s->cur = -1; ui_InvalidateRect(h, NULL, 1); return 0;
    case 0x185: /* LB_SETSEL */
        if (!(w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL))) return -1;
        for (int i = 0; i < s->n; i++) if ((int)lp == -1 || i == (int)lp) s->it[i].sel = (int)wp;
        ui_InvalidateRect(h, NULL, 1); return 0;
    case 0x186: /* LB_SETCURSEL */
        if (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) return -1;
        lb_set_cur(w, (int)wp, 0);
        return (int)wp >= s->n ? -1 : (LRESULT)(int)wp;
    case 0x187: if ((int)wp < 0 || (int)wp >= s->n) return -1; return (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) ? s->it[wp].sel : (int)wp == s->cur;
    case 0x188: return s->cur;                                  /* LB_GETCURSEL */
    case 0x189: if ((int)wp < 0 || (int)wp >= s->n) return -1;  /* LB_GETTEXT */
        if (lb_strings(w)) { strcpy((char *)(uintptr_t)lp, s->it[wp].s); return (LRESULT)strlen(s->it[wp].s); }
        memcpy((void *)(uintptr_t)lp, &s->it[wp].data, 4); return 4;
    case 0x18a: if ((int)wp < 0 || (int)wp >= s->n) return -1; return lb_strings(w) ? (LRESULT)strlen(s->it[wp].s) : 4;
    case 0x18b: return s->n;                                    /* LB_GETCOUNT */
    case 0x18c: { int i = lb_find(w, (int)wp, (const char *)(uintptr_t)lp, 0); if (i >= 0) lb_set_cur(w, i, 0); return i; }
    case 0x18e: return s->top;                                  /* LB_GETTOPINDEX */
    case 0x18f: return lb_find(w, (int)wp, (const char *)(uintptr_t)lp, 0);
    case 0x190: { int n = 0; for (int i = 0; i < s->n; i++) n += s->it[i].sel != 0; return (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) ? n : -1; }
    case 0x191: { int n = 0; int *out = (int *)(uintptr_t)lp; for (int i = 0; i < s->n && n < (int)wp; i++) if (s->it[i].sel) out[n++] = i; return n; }
    case 0x197: /* LB_SETTOPINDEX */
        if ((int)wp < 0 || ((int)wp >= s->n && s->n)) return -1;
        s->top = (int)wp; ui_InvalidateRect(h, NULL, 1); return 0;
    case 0x198: { /* LB_GETITEMRECT */
        RECT *r = (RECT *)(uintptr_t)lp; if (!r) return -1;
        r->left = 0; r->right = w->cr.right - w->cr.left; r->top = ((int)wp - s->top) * s->item_h; r->bottom = r->top + s->item_h;
        return (int)wp < s->n || (!s->n && !wp) ? 1 : -1;
    }
    case 0x199: if ((int)wp < 0 || (int)wp >= s->n) return -1; return (LRESULT)s->it[wp].data;
    case 0x19a: if ((int)wp == -1) { for (int i = 0; i < s->n; i++) s->it[i].data = (DWORD)lp; return 0; }
        if ((int)wp < 0 || (int)wp >= s->n) return -1; s->it[wp].data = (DWORD)lp; return 0;
    case 0x19b: return 0;                                       /* LB_SELITEMRANGE */
    case 0x19c: s->anchor = (int)wp; return 0;
    case 0x19d: return s->anchor;
    case 0x19f: return s->cur;                                  /* LB_GETCARETINDEX */
    case 0x1a0: if (LOWORD(lp)) s->item_h = LOWORD(lp); ui_InvalidateRect(h, NULL, 1); return 0;   /* LB_SETITEMHEIGHT */
    case 0x1a1: return s->item_h;                               /* LB_GETITEMHEIGHT */
    case 0x1a2: return lb_find(w, (int)wp, (const char *)(uintptr_t)lp, 1);
    case 0x194: case 0x195: case 0x196: return 0;               /* horizontal extent, column width */
    case 0x1a8: return 0;
    }
    if (msg >= 0x180 && msg <= 0x1b0) ui_log("listbox: message %04x not handled", msg);
    return ui_DefWindowProcA(h, msg, wp, lp);
}

/* ================= Combo box (first version: drop-down list, owner-draw) ================= */
typedef struct { HWND lb; int dropped; HFONT font; int item_h; int sel_h; RECT drop; int captured, resizing; } cb_state;
enum { CBS_SIMPLE = 1, CBS_DROPDOWN = 2, CBS_DROPDOWNLIST = 3, CBS_OWNERDRAWFIXED = 0x10, CBS_OWNERDRAWVARIABLE = 0x20,
       CBS_SORT = 0x100, CBS_HASSTRINGS = 0x200 };
enum { CBN_SELCHANGE = 1, CBN_DBLCLK = 2, CBN_SETFOCUS = 3, CBN_KILLFOCUS = 4, CBN_DROPDOWN = 7, CBN_CLOSEUP = 8,
       CBN_SELENDOK = 9, CBN_SELENDCANCEL = 10 };
static cb_state *cbs(ui_win *w) { if (!w->ctl) w->ctl = calloc(1, sizeof(cb_state)); return w->ctl; }
static void cb_notify(ui_win *w, UINT code) { notify_parent(w, code); }
/* Wine COMBO_Size: drop-down combos always have the text area height plus 2 * 2 border pixels */
static void cb_fix_height(ui_win *w)
{
    cb_state *s = cbs(w);
    if (!s->lb || s->resizing || (w->style & 3) == 1 /* CBS_SIMPLE */) return;
    int want = s->sel_h + 4, cur = w->wr.bottom - w->wr.top;
    if (cur == want) return;
    s->resizing = 1;
    HWND h = w->h;
    ui_SetWindowPos(h, 0, 0, 0, w->wr.right - w->wr.left, want, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOREDRAW);
    if ((w = ui_get(h))) cbs(w)->resizing = 0;
}
static void cb_show_drop(ui_win *w, int show)
{
    cb_state *s = cbs(w);
    HWND h = w->h;
    if (!!show == s->dropped) return;
    ui_win *lb = ui_get(s->lb);
    if (!lb) return;
    if (show) {
        cb_notify(w, CBN_DROPDOWN);
        if (!(w = ui_get(h))) return;
        s = cbs(w);
        RECT r; ui_GetWindowRect(h, &r);
        int n = (int)ui_send(lb, 0x18b, 0, 0);
        int rows = n < 1 ? 1 : n;
        int hgt = s->drop.bottom > 0 ? s->drop.bottom : rows * lbs(lb)->item_h + 2;
        ui_SetWindowPos(s->lb, HWND_TOPMOST, r.left, r.bottom, r.right - r.left, hgt, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s->dropped = 1;
        ui_SetCapture(h); s->captured = 1;
    } else {
        s->dropped = 0;
        ui_ShowWindow(s->lb, SW_HIDE);
        if (!(w = ui_get(h))) return;
        if (cbs(w)->captured) { cbs(w)->captured = 0; if (ui_capture() == w) ui_ReleaseCapture(); }
        if ((w = ui_get(h))) cb_notify(w, CBN_CLOSEUP);
    }
    if ((w = ui_get(h))) ui_InvalidateRect(h, NULL, 1);
}
static LRESULT UIAPI combo_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    cb_state *s = cbs(w);
    ui_win *lb = ui_get(s->lb);
    switch (msg) {
    case WM_NCCREATE:                         /* Wine COMBO_NCCreate: the combo itself has no border or scroll bars */
        w->style &= ~(WS_BORDER | WS_HSCROLL | WS_VSCROLL);
        w->exstyle &= ~WS_EX_CLIENTEDGE;
        return ui_DefWindowProcA(h, msg, wp, lp);
    case WM_CREATE: {
        DWORD ls = 0x40000000u | 0x80000000u | WS_BORDER | WS_VSCROLL | LBS_NOTIFY;   /* hidden popup list */
        ls &= ~WS_CHILD;
        if (w->style & CBS_OWNERDRAWFIXED) ls |= LBS_OWNERDRAWFIXED;
        if (w->style & CBS_OWNERDRAWVARIABLE) ls |= LBS_OWNERDRAWVARIABLE;
        if (w->style & CBS_HASSTRINGS) ls |= LBS_HASSTRINGS;
        if (w->style & CBS_SORT) ls |= LBS_SORT;
        HWND l = ui_create(WS_EX_TOPMOST, "ComboLBox", "", ls, 0, 0, w->cr.right - w->cr.left, 100, h, 1000, w->inst, NULL, 0);
        if (!(w = ui_get(h))) return -1;
        s = cbs(w); s->lb = l;
        ui_win *lw = ui_get(l);
        if (lw) { lbs(lw)->combo = 1; lbs(lw)->owner = ui_handle(w->parent); lw->id = w->id; }
        /* Wine COMBO_Create / CBGetTextAreaHeight: the text height comes from the DC font
           (no font yet: system font). Owner-drawn combos ask the parent with itemID -1 (text area)
           and itemID 0 (list items). The text area height is 2 + the answer. */
        int th; { HDC dc = ui_GetDC(h); LONG tm[16] = {0}; ui_GetTextMetricsA(dc, tm); ui_ReleaseDC(h, dc); th = (int)tm[0] + 4; }   /* combo_get_text_height */
        if (!(w = ui_get(h))) return -1;
        cbs(w)->sel_h = th;
        if (w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
            MEASUREITEMSTRUCT mi = { ODT_COMBOBOX, w->id, (UINT)-1, (UINT)(w->cr.right - w->cr.left), (UINT)(th - 2), 0 };
            ui_send(w->parent, WM_MEASUREITEM, w->id, (LPARAM)&mi);
            if ((w = ui_get(h))) cbs(w)->sel_h = 2 + (int)mi.itemHeight;
            if (w && (w->style & CBS_OWNERDRAWFIXED)) {
                mi.itemID = 0; mi.itemHeight = (UINT)th;
                ui_send(w->parent, WM_MEASUREITEM, w->id, (LPARAM)&mi);
                if (lw && ui_get(l)) lbs(lw)->item_h = mi.itemHeight ? (int)mi.itemHeight : th;
            }
        }
        return 0;
    }
    case WM_DESTROY: if (lb) ui_destroy_window(lb); if ((w = ui_get(h))) cbs(w)->lb = 0; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        ui_send(w->parent, WM_CTLCOLORLISTBOX, dc, (LPARAM)h);
        if ((w = ui_get(h)) && (w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) && (lb = ui_get(cbs(w)->lb))) {
            int cur = (int)ui_send(lb, 0x188, 0, 0);
            DRAWITEMSTRUCT d = { ODT_COMBOBOX, w->id, (UINT)cur, ODA_DRAWENTIRE, ODS_COMBOBOXEDIT, h, dc,
                                 { 3, 3, w->cr.right - w->cr.left - 2 - 17 - 1, 2 + cbs(w)->sel_h - 1 }, 0 };
            if (cur >= 0) d.itemData = (DWORD)ui_send(lb, 0x199, (WPARAM)cur, 0);
            if (ui_focus() == w && !cbs(w)->dropped) d.itemState |= ODS_SELECTED | ODS_FOCUS;
            if (w->style & WS_DISABLED) d.itemState |= ODS_DISABLED;
            ui_send(w->parent, WM_DRAWITEM, w->id, (LPARAM)&d);
        }
        if (!wp) ui_EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFONT:                                             /* Wine COMBO_Font */
        s->font = wp;
        if (!(w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE))) { int tm[20]; ui_font_tm(wp, tm); s->sel_h = tm[0] + 4; }
        if (lb) ui_send(lb, WM_SETFONT, wp, lp);
        if ((w = ui_get(h))) cb_fix_height(w);
        if (lp) ui_InvalidateRect(h, NULL, 1);
        return 0;
    case WM_GETFONT: return (LRESULT)s->font;
    case WM_SETFOCUS: ui_InvalidateRect(h, NULL, 1); cb_notify(w, CBN_SETFOCUS); return 0;
    case WM_KILLFOCUS: cb_show_drop(w, 0); if ((w = ui_get(h))) { ui_InvalidateRect(h, NULL, 1); cb_notify(w, CBN_KILLFOCUS); } return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        if (!s->dropped) { ui_SetFocus(h); if ((w = ui_get(h))) cb_show_drop(w, 1); return 0; }
        /* fall through: click while dropped */
    case WM_LBUTTONUP:
        if (s->dropped && msg != WM_LBUTTONUP) {
            POINT p = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
            ui_client_to_screen(w, &p);
            RECT lr; ui_GetWindowRect(s->lb, &lr);
            if (p.x >= lr.left && p.x < lr.right && p.y >= lr.top && p.y < lr.bottom && lb) {
                POINT lp2 = p; ui_screen_to_client(lb, &lp2);
                int i = lbs(lb)->top + lp2.y / (lbs(lb)->item_h ? lbs(lb)->item_h : 1);
                int n = lbs(lb)->n;
                cb_show_drop(w, 0);
                if (!(w = ui_get(h))) return 0;
                if (i < n) {
                    int old = lbs(lb)->cur;
                    lb_set_cur(lb, i, 0);
                    cb_notify(w, CBN_SELENDOK);
                    if ((w = ui_get(h)) && old != i) cb_notify(w, CBN_SELCHANGE);
                }
            } else {
                cb_show_drop(w, 0);
                if ((w = ui_get(h))) cb_notify(w, CBN_SELENDCANCEL);
            }
            return 0;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (s->dropped && lb) {
            POINT p = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
            ui_client_to_screen(w, &p);
            POINT q = p; ui_screen_to_client(lb, &q);
            if (q.x >= 0 && q.y >= 0 && q.x < lb->cr.right - lb->cr.left && q.y < lb->cr.bottom - lb->cr.top) {
                int i = lbs(lb)->top + q.y / (lbs(lb)->item_h ? lbs(lb)->item_h : 1);
                if (i < lbs(lb)->n) lb_set_cur(lb, i, 0);
            }
        }
        return 0;
    case WM_CAPTURECHANGED: if (s->dropped && (HWND)lp != h) { s->captured = 0; cb_show_drop(w, 0); } return 0;
    case WM_KEYDOWN:
        if (!lb) return 0;
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END || wp == VK_PRIOR || wp == VK_NEXT) {
            if ((wp == VK_DOWN || wp == VK_UP) && (ui_GetKeyState(VK_MENU) & 0x8000)) { cb_show_drop(w, !s->dropped); return 0; }
            int old = lbs(lb)->cur;
            listbox_proc(lb->h, WM_KEYDOWN, wp, lp);
            if ((w = ui_get(h)) && (lb = ui_get(cbs(w)->lb)) && lbs(lb)->cur != old) {
                ui_InvalidateRect(h, NULL, 1);
                if (!cbs(w)->dropped) cb_notify(w, CBN_SELENDOK);
                if ((w = ui_get(h))) cb_notify(w, CBN_SELCHANGE);
            }
        } else if (wp == VK_RETURN || wp == VK_ESCAPE) {
            if (s->dropped) { cb_show_drop(w, 0); if ((w = ui_get(h))) cb_notify(w, wp == VK_RETURN ? CBN_SELENDOK : CBN_SELENDCANCEL); }
        } else if (wp == 0x73 /* F4 */) cb_show_drop(w, !s->dropped);
        return 0;
    case 0x143: if (!lb) return -1; { LRESULT r = ui_send(lb, 0x180, 0, lp); ui_InvalidateRect(h, NULL, 1); return r; }   /* CB_ADDSTRING */
    case 0x144: return lb ? ui_send(lb, 0x182, wp, 0) : -1;     /* CB_DELETESTRING */
    case 0x146: return lb ? ui_send(lb, 0x18b, 0, 0) : 0;       /* CB_GETCOUNT */
    case 0x147: return lb ? lbs(lb)->cur : -1;                   /* CB_GETCURSEL */
    case 0x148: return lb ? ui_send(lb, 0x189, wp, lp) : -1;     /* CB_GETLBTEXT */
    case 0x149: return lb ? ui_send(lb, 0x18a, wp, 0) : -1;
    case 0x14a: return lb ? ui_send(lb, 0x181, wp, lp) : -1;     /* CB_INSERTSTRING */
    case 0x14b: if (lb) ui_send(lb, 0x184, 0, 0); ui_InvalidateRect(h, NULL, 1); return 0;  /* CB_RESETCONTENT */
    case 0x14c: return lb ? ui_send(lb, 0x18f, wp, lp) : -1;     /* CB_FINDSTRING */
    case 0x14d: { if (!lb) return -1; LRESULT r = ui_send(lb, 0x18c, wp, lp); ui_InvalidateRect(h, NULL, 1); return r; }
    case 0x14e: if (!lb) return -1; lb_set_cur(lb, (int)wp, 0); ui_InvalidateRect(h, NULL, 1); return (int)wp < lbs(lb)->n ? (LRESULT)(int)wp : -1;
    case 0x14f: cb_show_drop(w, (int)wp); return 1;              /* CB_SHOWDROPDOWN */
    case 0x150: return lb ? ui_send(lb, 0x199, wp, 0) : -1;      /* CB_GETITEMDATA */
    case 0x151: return lb ? ui_send(lb, 0x19a, wp, lp) : -1;     /* CB_SETITEMDATA */
    case 0x152: {                                                /* CB_GETDROPPEDCONTROLRECT */
        RECT *r = (RECT *)(uintptr_t)lp; if (!r) return 0;
        RECT wr; ui_GetWindowRect(h, &wr);
        int n = lb ? lbs(lb)->n : 0; int ih = lb ? lbs(lb)->item_h : 13;
        r->left = wr.left; r->right = wr.right; r->top = wr.top;
        r->bottom = s->drop.bottom > 0 ? wr.bottom + s->drop.bottom : wr.bottom + (n ? n : 1) * ih + 2;
        return 1;
    }
    case 0x153:                                                  /* CB_SETITEMHEIGHT */
        if ((int)wp == -1) { s->sel_h = (int)lp + 2; cb_fix_height(w); if (!(w = ui_get(h))) return 0; }
        else if (lb) lbs(lb)->item_h = (int)lp;
        ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_SIZE: cb_fix_height(w); return 0;
    case 0x154: return (int)wp == -1 ? s->sel_h : (lb ? lbs(lb)->item_h : 13);
    case 0x157: return s->dropped;                               /* CB_GETDROPPEDSTATE */
    case 0x158: return lb ? ui_send(lb, 0x1a2, wp, lp) : -1;     /* CB_FINDSTRINGEXACT */
    case 0x15b: return lb ? lbs(lb)->top : 0;                    /* CB_GETTOPINDEX */
    case 0x15c: if (lb) ui_send(lb, 0x197, wp, 0); return 0;
    case 0x160: s->drop.bottom = (int)wp; return (LRESULT)wp;    /* CB_SETDROPPEDWIDTH (approximation) */
    case 0x141: case 0x142: return 1;                            /* CB_LIMITTEXT, CB_SETEDITSEL */
    case 0x140: return 0;                                        /* CB_GETEDITSEL */
    case WM_GETTEXT: case WM_GETTEXTLENGTH:
        if (lb && lbs(lb)->cur >= 0 && lb_strings(lb)) {
            const char *t = lbs(lb)->it[lbs(lb)->cur].s;
            if (msg == WM_GETTEXTLENGTH) return (LRESULT)strlen(t);
            if (!wp) return 0;
            size_t n = strlen(t); if (n > wp - 1) n = wp - 1;
            memcpy((void *)(uintptr_t)lp, t, n); ((char *)(uintptr_t)lp)[n] = 0; return (LRESULT)n;
        }
        if (msg == WM_GETTEXT && wp) ((char *)(uintptr_t)lp)[0] = 0;
        return 0;
    }
    if (msg >= 0x140 && msg <= 0x165) ui_log("combobox: message %04x not handled", msg);
    return ui_DefWindowProcA(h, msg, wp, lp);
}

/* ================= Edit (first version: text, limit, selection) ================= */
typedef struct { int limit, sel0, sel1; HFONT font; } ed_state;
static LRESULT UIAPI edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    if (!w->ctl) { ed_state *e = calloc(1, sizeof *e); e->limit = 30000; w->ctl = e; }
    ed_state *e = w->ctl;
    int len = (int)strlen(w->text ? w->text : "");
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS;
    case WM_SETFONT: e->font = wp; if (lp) ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_GETFONT: return (LRESULT)e->font;
    case WM_SETTEXT: ui_DefWindowProcA(h, msg, wp, lp); e->sel0 = e->sel1 = 0; ui_InvalidateRect(h, NULL, 1); notify_parent(w, 0x400 /* EN_UPDATE */); if ((w = ui_get(h))) notify_parent(w, 0x300 /* EN_CHANGE */); return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        ui_send(w->parent, WM_CTLCOLOREDIT, dc, (LPARAM)h);
        if (!wp) ui_EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: ui_SetFocus(h); return 0;
    case WM_SETFOCUS: notify_parent(w, 0x100 /* EN_SETFOCUS */); return 0;
    case WM_KILLFOCUS: notify_parent(w, 0x200 /* EN_KILLFOCUS */); return 0;
    case WM_CHAR: {
        char *t = w->text ? w->text : "";
        int a = UI_MIN(e->sel0, e->sel1), b = UI_MAX(e->sel0, e->sel1);
        if (a > len) a = len;
        if (b > len) b = len;
        char buf[4096]; int n = 0;
        if (wp == 8) { if (a == b && a > 0) a--; }
        else if (wp < 32) return 0;
        if (wp != 8 && len - (b - a) >= e->limit) return 0;
        memcpy(buf, t, (size_t)a); n = a;
        if (wp != 8) buf[n++] = (char)wp;
        int caret = n;
        size_t rest = strlen(t + b); if (n + rest >= sizeof buf) rest = sizeof buf - 1 - (size_t)n;
        memcpy(buf + n, t + b, rest); buf[n + rest] = 0;
        free(w->text); w->text = ui_strdup(buf);
        e->sel0 = e->sel1 = caret;
        ui_InvalidateRect(h, NULL, 1);
        notify_parent(w, 0x400); if ((w = ui_get(h))) notify_parent(w, 0x300);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_LEFT && e->sel1 > 0) e->sel0 = --e->sel1;
        else if (wp == VK_RIGHT && e->sel1 < len) e->sel0 = ++e->sel1;
        else if (wp == VK_HOME) e->sel0 = e->sel1 = 0;
        else if (wp == VK_END) e->sel0 = e->sel1 = len;
        else if (wp == VK_DELETE && len) {
            int a = UI_MIN(e->sel0, e->sel1), b = UI_MAX(e->sel0, e->sel1);
            if (a == b) b = a + 1;
            if (b > len) return 0;
            memmove(w->text + a, w->text + b, (size_t)(len - b + 1));
            e->sel0 = e->sel1 = a;
            ui_InvalidateRect(h, NULL, 1);
            notify_parent(w, 0x400); if ((w = ui_get(h))) notify_parent(w, 0x300);
        }
        return 0;
    case 0xb0: if (wp) *(DWORD *)(uintptr_t)wp = (DWORD)e->sel0; if (lp) *(DWORD *)(uintptr_t)lp = (DWORD)e->sel1; return MAKELONG(e->sel0, e->sel1);  /* EM_GETSEL */
    case 0xb1: e->sel0 = (int)wp < 0 ? len : UI_MIN((int)wp, len); e->sel1 = (int)lp < 0 ? len : UI_MIN((int)lp, len); if ((int)wp == -1) e->sel1 = e->sel0; return 1;
    case 0xc5: e->limit = wp ? (int)wp : 0x7ffffffe; return 0;   /* EM_LIMITTEXT */
    case 0xd5: return e->limit;                                  /* EM_GETLIMITTEXT */
    case 0xb8: return 0;                                         /* EM_GETMODIFY */
    case 0xb9: return 0;                                         /* EM_SETMODIFY */
    case 0xba: return 1;                                         /* EM_GETLINECOUNT */
    case 0xc2: {                                                 /* EM_REPLACESEL */
        const char *ins = (const char *)(uintptr_t)lp; if (!ins) ins = "";
        int a = UI_MIN(e->sel0, e->sel1), b = UI_MAX(e->sel0, e->sel1);
        if (a > len) a = len;
        if (b > len) b = len;
        size_t il = strlen(ins); char *buf = malloc((size_t)len + il + 1);
        memcpy(buf, w->text, (size_t)a); memcpy(buf + a, ins, il); strcpy(buf + a + il, w->text + b);
        free(w->text); w->text = buf; e->sel0 = e->sel1 = a + (int)il;
        ui_InvalidateRect(h, NULL, 1);
        notify_parent(w, 0x400); if ((w = ui_get(h))) notify_parent(w, 0x300);
        return 0;
    }
    case 0xcf: if (wp) w->style |= 0x800; else w->style &= ~0x800u; return 1;   /* EM_SETREADONLY */
    case 0xcc: case 0xd2: case 0xd3: case 0xc8: case 0xc9: case 0xb5: case 0xb6: case 0xb7: return 0;
    case 0xe1: case 0xe9: return 0;
    }
    if (msg >= 0xb0 && msg <= 0xff) ui_log("edit: message %04x not handled", msg);
    return ui_DefWindowProcA(h, msg, wp, lp);
}

/* ================= Trackbar (first version) ================= */
typedef struct { int min, max, pos, page, line, captured; } tb_state;
enum { TBS_VERT = 2, TB_LINEUP = 0, TB_LINEDOWN, TB_PAGEUP, TB_PAGEDOWN, TB_THUMBPOSITION, TB_THUMBTRACK, TB_TOP, TB_BOTTOM, TB_ENDTRACK };
static void tb_notify(ui_win *w, int code, int pos)
{
    ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : NULL;
    if (p) ui_send(p, (w->style & TBS_VERT) ? WM_VSCROLL : WM_HSCROLL, MAKELONG(code, pos), (LPARAM)w->h);
}
static int tb_pos_from_x(ui_win *w, int x)
{
    tb_state *t = w->ctl;
    int width = (w->style & TBS_VERT) ? w->cr.bottom - w->cr.top : w->cr.right - w->cr.left;
    int left = 13, right = width - 13;
    if (right <= left) return t->min;
    int p = t->min + (int)(((int64_t)(x - left) * (t->max - t->min) + (right - left) / 2) / (right - left));
    return UI_MAX(t->min, UI_MIN(t->max, p));
}
static LRESULT UIAPI trackbar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    if (!w->ctl) { tb_state *t = calloc(1, sizeof *t); t->max = 100; t->page = 20; t->line = 1; w->ctl = t; }
    tb_state *t = w->ctl;
    switch (msg) {
    case 0x400: return t->pos;                                               /* TBM_GETPOS */
    case 0x401: return t->min;                                               /* TBM_GETRANGEMIN */
    case 0x402: return t->max;                                               /* TBM_GETRANGEMAX */
    case 0x405: t->pos = UI_MAX(t->min, UI_MIN(t->max, (int)lp)); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;   /* TBM_SETPOS */
    case 0x406: t->min = (int16_t)LOWORD(lp); t->max = (int16_t)HIWORD(lp); t->pos = UI_MAX(t->min, UI_MIN(t->max, t->pos)); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;
    case 0x407: t->min = (int)lp; t->pos = UI_MAX(t->min, t->pos); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;
    case 0x408: t->max = (int)lp; t->pos = UI_MIN(t->max, t->pos); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;
    case 0x414: return 0;                                                     /* TBM_SETTICFREQ */
    case 0x415: { int o = t->page; t->page = (int)lp; return o; }             /* TBM_SETPAGESIZE */
    case 0x416: return t->page;
    case 0x417: { int o = t->line; t->line = (int)lp; return o; }
    case 0x418: return t->line;
    case 0x404: case 0x409: case 0x40a: case 0x40b: case 0x40c: case 0x40d: case 0x410: case 0x411: case 0x412: case 0x413: return 0;
    case 0x419: case 0x41a: { RECT *r = (RECT *)(uintptr_t)lp; if (r) { r->left = 0; r->top = 0; r->right = w->cr.right - w->cr.left; r->bottom = w->cr.bottom - w->cr.top; } return 0; }
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps); if (!wp) ui_EndPaint(h, &ps); (void)dc; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: {
        ui_SetFocus(h);
        if (!(w = ui_get(h))) return 0;
        int x = (w->style & TBS_VERT) ? (int16_t)HIWORD(lp) : (int16_t)LOWORD(lp);
        t->pos = tb_pos_from_x(w, x);
        t->captured = 1; ui_SetCapture(h);
        if ((w = ui_get(h))) { ui_InvalidateRect(h, NULL, 0); tb_notify(w, TB_THUMBTRACK, t->pos); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (t->captured) {
            int x = (w->style & TBS_VERT) ? (int16_t)HIWORD(lp) : (int16_t)LOWORD(lp);
            int p = tb_pos_from_x(w, x);
            if (p != t->pos) { t->pos = p; ui_InvalidateRect(h, NULL, 0); tb_notify(w, TB_THUMBTRACK, p); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (t->captured) {
            t->captured = 0; ui_ReleaseCapture();
            if ((w = ui_get(h))) { tb_notify(w, TB_THUMBPOSITION, t->pos); if ((w = ui_get(h))) tb_notify(w, TB_ENDTRACK, 0); }
        }
        return 0;
    case WM_CAPTURECHANGED: t->captured = 0; return 0;
    case WM_KEYDOWN: {
        int code = -1, p = t->pos;
        switch (wp) {
        case VK_LEFT: case VK_UP: code = TB_LINEUP; p -= t->line; break;
        case VK_RIGHT: case VK_DOWN: code = TB_LINEDOWN; p += t->line; break;
        case VK_PRIOR: code = TB_PAGEUP; p -= t->page; break;
        case VK_NEXT: code = TB_PAGEDOWN; p += t->page; break;
        case VK_HOME: code = TB_TOP; p = t->min; break;
        case VK_END: code = TB_BOTTOM; p = t->max; break;
        }
        if (code < 0) return 0;
        t->pos = UI_MAX(t->min, UI_MIN(t->max, p));
        ui_InvalidateRect(h, NULL, 0);
        tb_notify(w, code, 0);
        return 0;
    }
    case WM_KEYUP:
        if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_HOME || wp == VK_END)
            tb_notify(w, TB_ENDTRACK, 0);
        return 0;
    }
    if (msg >= 0x400 && msg < 0x440) ui_log("trackbar: message %04x not handled", msg);
    return ui_DefWindowProcA(h, msg, wp, lp);
}

/* ================= Progress, hotkey, list view (first versions) ================= */
static LRESULT UIAPI progress_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    if (!w->ctl) { int *p = calloc(4, sizeof(int)); p[1] = 100; p[3] = 10; w->ctl = p; }
    int *p = w->ctl;   /* min, max, pos, step */
    switch (msg) {
    case WM_NCCREATE:                         /* Wine comctl32 progress: always a static edge */
        w->exstyle = (w->exstyle & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE)) | WS_EX_STATICEDGE;
        return ui_DefWindowProcA(h, msg, wp, lp);
    case 0x401: { int o = MAKELONG(p[0], p[1]); p[0] = LOWORD(lp); p[1] = HIWORD(lp); ui_InvalidateRect(h, NULL, 1); return o; }
    case 0x402: { int o = p[2]; p[2] = (int)wp; ui_InvalidateRect(h, NULL, 1); return o; }
    case 0x403: { int o = p[2]; p[2] += (int)wp; ui_InvalidateRect(h, NULL, 1); return o; }
    case 0x404: { int o = p[3]; p[3] = (int)wp; return o; }
    case 0x405: { int o = p[2]; p[2] += p[3]; if (p[2] > p[1]) p[2] = p[0] + (p[2] - p[1]); ui_InvalidateRect(h, NULL, 1); return o; }
    case 0x406: { int o = MAKELONG(p[0], p[1]); p[0] = (int)wp; p[1] = (int)lp; return o; }
    case 0x407: return wp ? p[0] : p[1];
    case 0x408: return p[2];
    case WM_PAINT: { PAINTSTRUCT ps; if (!wp) { ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); } return 0; }
    case WM_ERASEBKGND: return 1;
    }
    return ui_DefWindowProcA(h, msg, wp, lp);
}
static LRESULT UIAPI hotkey_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    if (!w->ctl) w->ctl = calloc(2, sizeof(int));
    int *k = w->ctl;
    switch (msg) {
    case 0x401: k[0] = (int)wp; ui_InvalidateRect(h, NULL, 1); return 0;     /* HKM_SETHOTKEY */
    case 0x402: return k[0];                                                 /* HKM_GETHOTKEY */
    case 0x403: k[1] = (int)wp; return 0;                                    /* HKM_SETRULES */
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: ui_SetFocus(h); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int mods = ((ui_GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0) | ((ui_GetKeyState(VK_CONTROL) & 0x8000) ? 2 : 0) | ((ui_GetKeyState(VK_MENU) & 0x8000) ? 4 : 0);
        if (wp != VK_SHIFT && wp != VK_CONTROL && wp != VK_MENU) k[0] = (int)(wp & 0xff) | (mods << 8);
        else k[0] = mods << 8;
        ui_InvalidateRect(h, NULL, 1);
        notify_parent(w, 0x300 /* EN_CHANGE */);
        return 0;
    }
    case WM_PAINT: { PAINTSTRUCT ps; if (!wp) { ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); } return 0; }
    }
    return ui_DefWindowProcA(h, msg, wp, lp);
}
static LRESULT UIAPI listview_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg >= 0x1000 && msg < 0x1100) { ui_log("listview: message %04x not supported yet (stage 2)", msg); return 0; }
    if (msg == WM_PAINT) { PAINTSTRUCT ps; if (!wp) { ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); } return 0; }
    return ui_DefWindowProcA(h, msg, wp, lp);
}
static LRESULT UIAPI scrollbar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg >= 0xe0 && msg <= 0xef) { ui_log("scrollbar: message %04x not supported yet", msg); return 0; }
    return ui_DefWindowProcA(h, msg, wp, lp);
}

void ui_controls_free(ui_win *w)
{
    if (!w->ctl) return;
    if (w->cls && w->cls->proc == listbox_proc) {
        lb_state *s = w->ctl;
        for (int i = 0; i < s->n; i++) free(s->it[i].s);
        free(s->it);
    }
    free(w->ctl); w->ctl = NULL;
}
void ui_controls_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    HCURSOR arrow = 0x00b17f00;
    ui_register_builtin("Button", button_proc, 0x0b /* CS_VREDRAW|CS_HREDRAW|CS_DBLCLKS */ | 0x4000, 0, arrow);
    ui_register_builtin("Static", static_proc, 0x4000 | 8, 0, arrow);
    ui_register_builtin("ListBox", listbox_proc, 0x4008, 0, arrow);
    ui_register_builtin("ComboLBox", listbox_proc, 0x0808 | 0x20000 /* CS_DROPSHADOW-ish */, 0, arrow);
    ui_register_builtin("ComboBox", combo_proc, 0x4008 | 3, 0, arrow);
    ui_register_builtin("Edit", edit_proc, 0x4008, 0, 0x00b17f01);
    ui_register_builtin("ScrollBar", scrollbar_proc, 0x4008 | 3, 0, arrow);
    ui_register_builtin("#32770", ui_DefDlgProcA, 0x0808, DLGWINDOWEXTRA, arrow);
    ui_register_builtin("msctls_trackbar32", trackbar_proc, 3 | 8, 0, arrow);
    ui_register_builtin("msctls_progress32", progress_proc, 3, 0, arrow);
    ui_register_builtin("msctls_hotkey32", hotkey_proc, 3, 0, arrow);
    ui_register_builtin("SysListView32", listview_proc, 3 | 8, 0, arrow);
}
