/* File: button.c -- Button type widgets
 *
 * Copyright (C) 1993 Johannes Ruscheinski
 * Copyright (C) 1993 David Metcalfe
 * Copyright (C) 1994 Alexandre Julliard
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
 * TODO
 *  Styles
 *  - BS_NOTIFY: is it complete?
 *  - BS_RIGHTBUTTON: same as BS_LEFTTEXT
 *
 *  Messages
 *  - WM_CHAR: Checks a (manual or automatic) check box on '+' or '=', clears it on '-' key.
 *  - WM_NCCREATE: Turns any BS_OWNERDRAW button into a BS_PUSHBUTTON button.
 *  - WM_SYSKEYUP
 *
 *  Notifications
 *  - BN_DISABLE
 *  - BN_PUSHED/BN_HILITE
 *  + BN_KILLFOCUS: is it OK?
 *  - BN_PAINT
 *  + BN_SETFOCUS: is it OK?
 *  - BN_UNPUSHED/BN_UNHILITE
 */
#include "compat.h"
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
LRESULT UIAPI button_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
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
