/*
 * Progress control
 *
 * Copyright 1997, 2002 Dimitrie O. Paun
 * Copyright 1998, 1999 Eric Kohl
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
 *
 * Styles:
 *    -- PBS_SMOOTHREVERSE
 *
 */

#include "compat.h"
/* ANSI, pixel-free adaptation of Wine 11 progress.c. */
typedef struct {
    int min, max, pos, step;
    DWORD bar, bk; HFONT font; UINT state; int marquee;
} progress_state;
static void coerce(progress_state *p) { p->pos = UI_MIN(p->max, UI_MAX(p->min, p->pos)); }
LRESULT UIAPI progress_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    progress_state *p = w->ctl;
    if (!p && m != WM_CREATE) return ui_DefWindowProcA(h, m, wp, lp);
    switch (m) {
    case WM_CREATE:
        p = calloc(1, sizeof *p); if (!p) return -1;
        w->ctl = p; p->max = 100; p->step = 10;
        p->bar = p->bk = 0xff000000; p->state = 1;
        w->exstyle = (w->exstyle & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE)) | WS_EX_STATICEDGE;
        ui_SetWindowPos(h, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_DESTROY: free(p); w->ctl = NULL; return 0;
    case WM_GETFONT: return p->font;
    case WM_SETFONT: { HFONT old = p->font; p->font = wp; return old; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: case 0x318: {
        PAINTSTRUCT ps; if (!wp) { ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); } return 0;
    }
    case 0x401: case 0x406: {
        int old = MAKELONG(p->min, p->max);
        int low = m == 0x401 ? LOWORD(lp) : (int)wp;
        int high = m == 0x401 ? HIWORD(lp) : lp;
        if (p->min != low || p->max != high) {
            p->min = low; p->max = high; coerce(p); ui_InvalidateRect(h, NULL, 1);
        }
        return old;
    }
    case 0x402: case 0x403: case 0x405: {
        int old = p->pos;
        if (m == 0x402 && (w->style & 8)) return 1;
        if (m == 0x402) p->pos = wp;
        else if (m == 0x403) p->pos += (int)wp;
        else if (p->min != p->max) {
            p->pos += p->step;
            if (p->pos > p->max) p->pos = (p->pos - p->min) % (p->max - p->min) + p->min;
            if (p->pos < p->min) p->pos = (p->pos - p->min) % (p->max - p->min) + p->max;
        }
        if (m != 0x405) coerce(p);
        if (old != p->pos) { ui_InvalidateRect(h, NULL, p->pos < old); ui_UpdateWindow(h); }
        return old;
    }
    case 0x404: { int old = p->step; p->step = wp; return old; }
    case 0x407:
        if (lp) { ((int *)(uintptr_t)lp)[0] = p->min; ((int *)(uintptr_t)lp)[1] = p->max; }
        return wp ? p->min : p->max;
    case 0x408: return p->pos;
    case 0x409: case 0x2001: {
        DWORD *color = m == 0x409 ? &p->bar : &p->bk;
        DWORD old = *color; *color = lp; ui_InvalidateRect(h, NULL, 1); return old;
    }
    case 0x40a:
        p->marquee = !!wp;
        if (wp) ui_SetTimer(h, 1, lp ? (UINT)lp : 30, NULL); else ui_KillTimer(h, 1);
        return p->marquee;
    case 0x40d: return p->step;
    case 0x40e: return p->bk;
    case 0x40f: return p->bar;
    case 0x410: { UINT old = p->state; if (wp >= 1 && wp <= 3) p->state = wp; return old; }
    case 0x411: return p->state;
    }
    return ui_DefWindowProcA(h, m, wp, lp);
}
