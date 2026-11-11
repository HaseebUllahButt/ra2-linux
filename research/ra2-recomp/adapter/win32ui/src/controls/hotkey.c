/*
 * Hotkey control
 *
 * Copyright 1998, 1999 Eric Kohl
 * Copyright 2002 Gyorgy 'Nog' Jeney
 * Copyright 2004 Robert Shearman
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
 * This code was audited for completeness against the documented features
 * of Comctl32.dll version 6.0 on Sep. 21, 2004, by Robert Shearman.
 *
 * Unless otherwise noted, we believe this code to be complete, as per
 * the specification mentioned above.
 * If you discover missing features or bugs please note them below.
 *
 */

#include "compat.h"
/* ANSI, pixel-free adaptation of Wine 11 hotkey.c. */
typedef struct {
    WORD key, invalid, replacement;
    BYTE modifiers;
    HFONT font;
    int focus, height;
} hotkey_state;
static int invalid_modifiers(hotkey_state *s)
{
    static const BYTE masks[] = { 1, 2, 4, 16, 8, 32, 64, 128 };
    return !!(s->invalid & masks[s->modifiers & 7]);
}
LRESULT UIAPI hotkey_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h); if (!w) return 0;
    hotkey_state *s = w->ctl;
    if (!s && m != WM_NCCREATE) return ui_DefWindowProcA(h, m, wp, lp);
    switch (m) {
    case WM_NCCREATE:
        s = calloc(1, sizeof *s); if (!s) return 0;
        w->ctl = s; w->exstyle |= WS_EX_CLIENTEDGE;
        return ui_DefWindowProcA(h, m, wp, lp);
    case WM_CREATE:
        s->font = ui_GetStockObject(13); s->height = ctl_text_height(h, s->font); return 0;
    case WM_DESTROY: free(s); w->ctl = NULL; return 0;
    case 0x401: s->key = wp; ui_InvalidateRect(h, NULL, 1); return 0;
    case 0x402: return s->key;
    case 0x403: s->invalid = wp; s->replacement = lp; return 0;
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_GETFONT: return s->font;
    case WM_SETFONT:
        s->font = wp; s->height = ctl_text_height(h, wp);
        if (LOWORD(lp)) ui_InvalidateRect(h, NULL, 1);
        return 0;
    case WM_LBUTTONDOWN: if (!(w->style & WS_DISABLED)) ui_SetFocus(h); return 0;
    case WM_SETFOCUS: s->focus = 1; return 0;
    case WM_KILLFOCUS: s->focus = 0; return 0;
    case WM_ERASEBKGND:
        if (!(w->style & WS_DISABLED)) ui_send(w->parent, WM_CTLCOLOREDIT, wp, h);
        return -1;
    case WM_CHAR: case WM_SYSCHAR:
        wp = ui_MapVirtualKeyA((lp >> 16) & 255, 1);
        /* fall through */
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        if (w->style & WS_DISABLED) return 0;
        WORD old = s->key; BYTE mods = s->modifiers;
        s->key = 0;
        switch (wp) {
        case VK_RETURN: case VK_TAB: case VK_SPACE: case VK_DELETE: case VK_ESCAPE: case VK_BACK:
            ui_InvalidateRect(h, NULL, 1); return ui_DefWindowProcA(h, WM_KEYDOWN, wp, lp);
        case VK_SHIFT: s->modifiers |= 1; break;
        case VK_CONTROL: s->modifiers |= 2; break;
        case VK_MENU: s->modifiers |= 4; break;
        default: s->key = (wp & 255) | ((invalid_modifiers(s) ? s->replacement : s->modifiers) << 8); break;
        }
        if (old != s->key || mods != s->modifiers) { ui_InvalidateRect(h, NULL, 1); ctl_notify(w, 0x300); }
        return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP: {
        if (w->style & WS_DISABLED) return 0;
        BYTE old = s->modifiers;
        switch (wp) {
        case VK_SHIFT: s->modifiers &= ~1; break;
        case VK_CONTROL: s->modifiers &= ~2; break;
        case VK_MENU: s->modifiers &= ~4; break;
        default: return 1;
        }
        if (old != s->modifiers) { ui_InvalidateRect(h, NULL, 1); ctl_notify(w, 0x300); }
        return 0;
    }
    case WM_PAINT: case 0x318: {
        PAINTSTRUCT ps; if (!wp) { ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); } return 0;
    }
    }
    return ui_DefWindowProcA(h, m, wp, lp);
}
