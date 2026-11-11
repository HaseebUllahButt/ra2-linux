/*
 * Listbox controls
 *
 * Copyright 1996 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */
/*
 * Combo controls
 *
 * Copyright 1997 Alex Korobka
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * TODO:
 *   - CB_SETTOPINDEX
 */
#include "compat.h"
/* ================= List box (first version) ================= */
typedef struct { char *s; DWORD data; int sel; } lb_item;
typedef struct { lb_item *it; int n, cap, cur, top, item_h, anchor; HFONT font; int combo; HWND owner; int captured; } lb_state;
static lb_state *lbs(ui_win *w) { if (!w->ctl) { lb_state *s = calloc(1, sizeof *s); s->cur = -1; s->item_h = 13; w->ctl = s; } return w->ctl; }
enum { LBS_NOTIFY = 1, LBS_SORT = 2, LBS_MULTIPLESEL = 8, LBS_OWNERDRAWFIXED = 0x10, LBS_OWNERDRAWVARIABLE = 0x20,
       LBS_HASSTRINGS = 0x40, LBS_EXTENDEDSEL = 0x800 };
enum { LBN_SELCHANGE = 1, LBN_DBLCLK = 2, LBN_SELCANCEL = 3, LBN_SETFOCUS = 4, LBN_KILLFOCUS = 5 };
static int lb_strings(ui_win *w) { return !(w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (w->style & LBS_HASSTRINGS); }
static int lb_insert(ui_win *w, int at, LPARAM lp, int sort)
{
    lb_state *s = lbs(w);
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 16; s->it = realloc(s->it, sizeof(lb_item) * (size_t)s->cap); }
    if (at < 0 || at > s->n) at = s->n;
    if (sort && (w->style & LBS_SORT) && lb_strings(w)) {
        const char *str = (const char *)(uintptr_t)lp;
        for (at = 0; at < s->n && ui_stricmp(s->it[at].s, str) <= 0; at++) {}
    }
    memmove(s->it + at + 1, s->it + at, sizeof(lb_item) * (size_t)(s->n - at));
    lb_item *it = &s->it[at]; memset(it, 0, sizeof *it);
    if (lb_strings(w)) it->s = ui_strdup((const char *)(uintptr_t)lp); else { it->s = ui_strdup(""); it->data = (DWORD)lp; }
    s->n++;
    if (s->cur >= at) s->cur++;
    if (s->n == 1 && (w->style & 0x4000)) s->cur = 0;
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
    if ((w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || s->it[i].data) {
        DELETEITEMSTRUCT di = { s->combo ? ODT_COMBOBOX : ODT_LISTBOX, w->id, (UINT)i, s->combo ? (HWND)s->combo : w->h, s->it[i].data };
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
    if (s->combo && !ui_is_visible(ui_get(s->combo))) return;
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
LRESULT UIAPI listbox_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    lb_state *s = lbs(w);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)(uintptr_t)lp;
        s->owner = cs ? cs->hwndParent : 0;
        if (!(w->style & 0x8000) && (w->style & LBS_OWNERDRAWFIXED)) {
            MEASUREITEMSTRUCT mi = { ODT_LISTBOX, w->id, (UINT)-1, 0, (UINT)ctl_text_height(h, 0), 0 };
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
    case WM_ERASEBKGND:
        if (w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE))
            ui_send_h(s->owner ? s->owner : ui_handle(w->parent), WM_CTLCOLORLISTBOX, wp, h);
        return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFONT: s->font = wp; if (!(w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE))) s->item_h = ctl_text_height(h, wp); if (lp) ui_InvalidateRect(h, NULL, 1); return 0;
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
    case 0x180: return lb_insert(w, -1, lp, 1);                  /* LB_ADDSTRING */
    case 0x181: return lb_insert(w, (int)wp == -1 ? s->n : (int)wp, lp, 0); /* LB_INSERTSTRING */
    case 0x182: if ((int)wp < 0 || (int)wp >= s->n) return -1; lb_delete(w, (int)wp); ui_InvalidateRect(h, NULL, 1); return lbs(w)->n;
    case 0x184: while (s->n) lb_delete(w, s->n - 1); s->top = 0; s->cur = -1; ui_InvalidateRect(h, NULL, 1); return 0;
    case 0x185: /* LB_SETSEL */
        if (!(w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL))) return -1;
        for (int i = 0; i < s->n; i++) if ((int)lp == -1 || i == (int)lp) s->it[i].sel = (int)wp;
        ui_InvalidateRect(h, NULL, 1); return 0;
    case 0x186: /* LB_SETCURSEL */
        if (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL | 0x4000)) return -1;
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
        if ((int)wp < 0 || (int)wp >= s->n) return -1; s->it[wp].data = (DWORD)lp; return 1;
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
typedef struct { HWND lb; int dropped; HFONT font; int item_h; int sel_h; RECT drop; int captured; int drop_width, resizing; DWORD original_style; } cb_state;
enum { CBS_SIMPLE = 1, CBS_DROPDOWN = 2, CBS_DROPDOWNLIST = 3, CBS_OWNERDRAWFIXED = 0x10, CBS_OWNERDRAWVARIABLE = 0x20,
       CBS_SORT = 0x100, CBS_HASSTRINGS = 0x200 };
enum { CBN_SELCHANGE = 1, CBN_DBLCLK = 2, CBN_SETFOCUS = 3, CBN_KILLFOCUS = 4, CBN_DROPDOWN = 7, CBN_CLOSEUP = 8,
       CBN_SELENDOK = 9, CBN_SELENDCANCEL = 10 };
static cb_state *cbs(ui_win *w) { if (!w->ctl) w->ctl = calloc(1, sizeof(cb_state)); return w->ctl; }
static void cb_notify(ui_win *w, UINT code) { notify_parent(w, code); }
/* Wine COMBO_Size / CBForceDummyResize: preserve the popup height during font changes. */
static void cb_resize(ui_win *w, int external)
{
    cb_state *s = cbs(w);
    if (s->resizing || (w->style & 3) == CBS_SIMPLE) return;
    int height = s->sel_h + 4;
    int current = w->wr.bottom - w->wr.top;
    if (external && current > height) s->drop.bottom = s->drop.top + current - height;
    s->resizing = 1;
    ui_SetWindowPos(w->h, 0, 0, 0, w->wr.right - w->wr.left, height,
        SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOREDRAW);
    s->resizing = 0;
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
        int hgt = s->drop.bottom - s->drop.top;
        if (n && rows * lbs(lb)->item_h + 2 < hgt) hgt = rows * lbs(lb)->item_h + 2;
        ui_SetWindowPos(s->lb, HWND_TOPMOST, r.left, r.bottom, s->drop.right - s->drop.left, hgt, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s->dropped = 1;
        ui_UpdateWindow(s->lb);
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
LRESULT UIAPI combo_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    cb_state *s = cbs(w);
    ui_win *lb = ui_get(s->lb);
    switch (msg) {
    case WM_NCCREATE:
        s->original_style = ((CREATESTRUCTA *)(uintptr_t)lp)->style;
        w->style &= ~(WS_BORDER | WS_HSCROLL | WS_VSCROLL);
        w->exstyle &= ~WS_EX_CLIENTEDGE;
        return ui_DefWindowProcA(h, msg, wp, lp);
    case WM_CREATE: {
        s->sel_h = ctl_text_height(h, s->font) + 4;
        ui_GetClientRect(h, &s->drop);
        if (w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
            MEASUREITEMSTRUCT mi = { ODT_COMBOBOX, w->id, (UINT)-1,
                (UINT)(w->cr.right - w->cr.left), (UINT)(s->sel_h - 2), 0 };
            ui_send(w->parent, WM_MEASUREITEM, w->id, (LPARAM)&mi);
            if (!(w = ui_get(h))) return -1;
            s = cbs(w); s->sel_h = 2 + mi.itemHeight;
            if (w->style & CBS_OWNERDRAWFIXED) {
                mi.itemID = 0; mi.itemHeight = ctl_text_height(h, s->font) + 4;
                ui_send(w->parent, WM_MEASUREITEM, w->id, (LPARAM)&mi);
                if (!(w = ui_get(h))) return -1;
                s = cbs(w); s->item_h = mi.itemHeight;
            }
        }
        s->drop.top = s->sel_h + 4;
        if (s->drop.bottom < s->drop.top) s->drop.bottom = s->drop.top;
        DWORD ls = WS_POPUP | WS_BORDER | WS_CLIPSIBLINGS | LBS_NOTIFY | 0x8000;
        ls |= s->original_style & (WS_VSCROLL | CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE);
        if (w->style & CBS_HASSTRINGS) ls |= LBS_HASSTRINGS;
        if (w->style & CBS_SORT) ls |= LBS_SORT;
        HWND l = ui_create(WS_EX_TOPMOST, "ComboLBox", "", ls, 0, s->drop.top,
            s->drop.right, s->drop.bottom - s->drop.top, h, 1000, w->inst, NULL, 0);
        if (!(w = ui_get(h))) return -1;
        s = cbs(w); s->lb = l;
        ui_win *lw = ui_get(l);
        if (lw) {
            lbs(lw)->combo = h; lbs(lw)->owner = ui_handle(w->parent); lw->id = w->id;
            if (w->style & CBS_OWNERDRAWFIXED) lbs(lw)->item_h = s->item_h;
        }
        cb_resize(w, 0);
        return 0;
    }
    case WM_SIZE: cb_resize(w, 1); return 0;
    case WM_DESTROY: if (lb) ui_destroy_window(lb); if ((w = ui_get(h))) cbs(w)->lb = 0; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        ui_send(w->parent, (w->style & WS_DISABLED) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT, dc, (LPARAM)h);
        if ((w = ui_get(h)) && (w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) && (lb = ui_get(cbs(w)->lb))) {
            int cur = (int)ui_send(lb, 0x188, 0, 0);
            ui_send(w->parent, (w->style & WS_DISABLED) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT, dc, (LPARAM)h);
            DRAWITEMSTRUCT d = { ODT_COMBOBOX, w->id, (UINT)cur, ODA_DRAWENTIRE, ODS_COMBOBOXEDIT, h, dc,
                                 { 3, 3, w->cr.right - w->cr.left - 3 - ui_GetSystemMetrics(SM_CXVSCROLL), 1 + s->sel_h }, (DWORD)-1 };
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
    case WM_SETFONT: s->font = wp; if (!(w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE))) s->sel_h = ctl_text_height(h, wp) + 4; if (lb) ui_send(lb, WM_SETFONT, wp, lp); cb_resize(w, 0); if (lp) ui_InvalidateRect(h, NULL, 1); return 0;
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
            if ((w = ui_get(h)) && (lb = ui_get(cbs(w)->lb)) && (lbs(lb)->cur != old || cbs(w)->dropped)) {
                ui_InvalidateRect(h, NULL, 1);
                if (!cbs(w)->dropped) cb_notify(w, CBN_SELENDOK);
                if ((w = ui_get(h))) cb_notify(w, CBN_SELCHANGE);
            }
        } else if (wp == VK_RETURN || wp == VK_ESCAPE) {
            if (s->dropped) { cb_notify(w, wp == VK_RETURN ? CBN_SELENDOK : CBN_SELENDCANCEL); if ((w = ui_get(h))) cb_show_drop(w, 0); return 1; }
        } else if (wp == 0x73 /* F4 */) cb_show_drop(w, !s->dropped);
        return 0;
    case 0x143: if (!lb) return -1; { LRESULT r = ui_send(lb, 0x180, 0, lp); ui_InvalidateRect(h, NULL, 1); return r; }   /* CB_ADDSTRING */
    case 0x144: return lb ? ui_send(lb, 0x182, wp, 0) : -1;     /* CB_DELETESTRING */
    case 0x146: return lb ? ui_send(lb, 0x18b, 0, 0) : 0;       /* CB_GETCOUNT */
    case 0x147: return lb ? lbs(lb)->cur : -1;                   /* CB_GETCURSEL */
    case 0x148: return lb ? ui_send(lb, 0x189, wp, lp) : -1;     /* CB_GETLBTEXT */
    case 0x149: return lb ? ui_send(lb, 0x18a, wp, 0) : -1;
    case 0x14a: return lb ? ui_send(lb, 0x181, wp, lp) : -1;     /* CB_INSERTSTRING */
    case 0x14b: if (lb) ui_send(lb, 0x184, 0, 0); ui_InvalidateRect(h, NULL, 1); return 1;  /* CB_RESETCONTENT */
    case 0x14c: return lb ? ui_send(lb, 0x18f, wp, lp) : -1;     /* CB_FINDSTRING */
    case 0x14d: { if (!lb) return -1; LRESULT r = ui_send(lb, 0x18c, wp, lp); ui_InvalidateRect(h, NULL, 1); return r; }
    case 0x14e: if (!lb) return -1; lb_set_cur(lb, (int)wp, 0); ui_InvalidateRect(h, NULL, 1); return (int)wp < lbs(lb)->n ? (LRESULT)(int)wp : -1;
    case 0x14f: cb_show_drop(w, (int)wp); return 1;              /* CB_SHOWDROPDOWN */
    case 0x150: return lb ? ui_send(lb, 0x199, wp, 0) : -1;      /* CB_GETITEMDATA */
    case 0x151: return lb ? ui_send(lb, 0x19a, wp, lp) : -1;     /* CB_SETITEMDATA */
    case 0x152: {
        RECT *r = (RECT *)(uintptr_t)lp;
        if (r) {
            ui_GetWindowRect(h, r);
            r->right = r->left + s->drop.right - s->drop.left;
            r->bottom = r->top + s->drop.bottom - s->drop.top;
        }
        return 0;
    }
    case 0x153:
        if ((int)wp == -1) {
            if ((int)lp >= 32768) return -1;
            s->sel_h = (int)lp + 2; cb_resize(w, 0); return lp;
        }
        return lb ? ui_send(lb, 0x1a0, wp, lp) : -1;
    case 0x154: return (int)wp < 0 ? s->sel_h : (lb ? ui_send(lb, 0x1a1, wp, 0) : -1);
    case 0x157: return s->dropped;                               /* CB_GETDROPPEDSTATE */
    case 0x158: return lb ? ui_send(lb, 0x1a2, wp, lp) : -1;     /* CB_FINDSTRINGEXACT */
    case 0x15b: return lb ? lbs(lb)->top : 0;                    /* CB_GETTOPINDEX */
    case 0x15c: if (lb) ui_send(lb, 0x197, wp, 0); return 0;
    case 0x160:
        if ((w->style & 3) == CBS_SIMPLE || (int)wp >= 32768) return -1;
        if ((int)wp >= s->drop.right - s->drop.left) s->drop_width = wp;
        else if (wp) s->drop_width = 0;
        s->drop.right = UI_MAX(w->wr.right - w->wr.left, s->drop_width);
        /* fall through */
    case 0x15f: return s->drop_width ? s->drop_width : s->drop.right - s->drop.left;    /* CB_SETDROPPEDWIDTH (approximation) */
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


void ctl_listbox_free(ui_win *w)
{
    lb_state *s = w->ctl;
    for (int i = 0; i < s->n; i++) free(s->it[i].s);
    free(s->it);
}
