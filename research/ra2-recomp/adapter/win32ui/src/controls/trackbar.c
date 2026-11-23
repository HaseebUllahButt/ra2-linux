/*
 * Trackbar control
 *
 * Copyright 1998, 1999 Eric Kohl
 * Copyright 1998, 1999 Alex Priem
 * Copyright 2002 Dimitrie O. Paun
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
 */
#include "compat.h"
/* ================= Trackbar (first version) ================= */
typedef struct { int min, max, pos, page, line, captured, user_page; } tb_state;
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
static void tb_page(tb_state *t) { if (!t->user_page) { t->page = (t->max - t->min) / 5; if (!t->page) t->page = 1; } }
LRESULT UIAPI trackbar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
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
    case 0x406: t->min = (int16_t)LOWORD(lp); t->max = (int16_t)HIWORD(lp); t->pos = UI_MAX(t->min, UI_MIN(t->max, t->pos)); tb_page(t); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;
    case 0x407: t->min = (int)lp; t->pos = UI_MAX(t->min, t->pos); tb_page(t); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;
    case 0x408: t->max = (int)lp; t->pos = UI_MIN(t->max, t->pos); tb_page(t); if (wp) ui_InvalidateRect(h, NULL, 0); return 0;
    case 0x414: return 0;                                                     /* TBM_SETTICFREQ */
    case 0x415: { int o = t->page; t->user_page = lp != -1; if (t->user_page) t->page = lp; else tb_page(t); return o; }             /* TBM_SETPAGESIZE */
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
        return 1;
    }
    case WM_KEYUP:
        if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_HOME || wp == VK_END)
            tb_notify(w, TB_ENDTRACK, 0);
        return 1;
    }
    if (msg >= 0x400 && msg < 0x440) ui_log("trackbar: message %04x not handled", msg);
    return ui_DefWindowProcA(h, msg, wp, lp);
}
