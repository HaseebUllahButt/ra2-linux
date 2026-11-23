/*
 * Static control
 *
 * Copyright  David W. Metcalfe, 1993
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
 * Notes:
 *   - Controls with SS_SIMPLE but without SS_NOPREFIX:
 *     The text should not be changed. Windows doesn't clear the
 *     client rectangle, so the new text must be larger than the old one.
 *   - The SS_RIGHTJUST style is currently not implemented by Windows
 *     (or it does something different than documented).
 *
 * TODO:
 *   - Animated cursors
 */
#include "compat.h"
/* ================= Static ================= */
enum { SS_TYPEMASK = 0x1f, SS_ICON = 3, SS_BITMAP = 0xe, SS_ENHMETAFILE = 0xf, SS_NOTIFY = 0x100 };
static int has_text(UINT type) { return type <= 2 || type == 0xb || type == 0xc || type == 0xd; }
static void static_paint(ui_win *w, HDC dc)
{
    ui_win *p = w->parent && w->parent != ui_desktop() ? w->parent : w;
    ui_send(p, WM_CTLCOLORSTATIC, dc, w->h);
    if ((w->style & SS_TYPEMASK) == 0xd) {
        DRAWITEMSTRUCT d = {5, w->id, 0, ODA_DRAWENTIRE,
            (w->style & WS_DISABLED) ? ODS_DISABLED : 0, w->h, dc, {0}, 0};
        ui_GetClientRect(w->h, &d.rcItem);
        ui_send(p, WM_DRAWITEM, w->id, (LPARAM)&d);
    }
}
static void static_try_paint(ui_win *w)
{
    if (!ui_is_visible(w)) return;
    HWND h = w->h; HDC dc = ui_GetDC(h);
    static_paint(w, dc); ui_ReleaseDC(h, dc);
}
LRESULT UIAPI static_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
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
    case 0x318: case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = wp ? (HDC)wp : ui_BeginPaint(h, &ps);
        static_paint(w, dc);
        if (!wp) ui_EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_ENABLE: static_try_paint(w); if ((w = ui_get(h)) && (w->style & SS_NOTIFY)) notify_parent(w, wp ? 2 : 3); return 0;
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
        if (!has_text(type)) return 0;
        ui_DefWindowProcA(h, msg, wp, lp);
        static_try_paint(w);
        return 1;
    case WM_SETFONT: if (!has_text(type)) return 0; if (w->ctl == NULL) w->ctl = calloc(1, sizeof(HFONT)); *(HFONT *)w->ctl = wp; if (LOWORD(lp)) ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_GETFONT: return w->ctl ? (LRESULT)*(HFONT *)w->ctl : 0;
    case 0x170: case 0x171: case 0x172: case 0x173: return 0;   /* STM_* images */
    }
    return ui_DefWindowProcA(h, msg, wp, lp);
}
