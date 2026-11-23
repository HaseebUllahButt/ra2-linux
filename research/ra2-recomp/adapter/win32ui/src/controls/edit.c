/*
 *	Edit control
 *
 *	Copyright  David W. Metcalfe, 1994
 *	Copyright  William Magro, 1995, 1996
 *	Copyright  Frans van Dorsselaer, 1996, 1997
 *
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
 *   - EN_ALIGN_LTR_EC
 *   - EN_ALIGN_RTL_EC
 *   - ES_OEMCONVERT
 *
 */
#include "compat.h"
/* ================= Edit (first version: text, limit, selection) ================= */
typedef struct { int limit, sel0, sel1; HFONT font; } ed_state;
LRESULT UIAPI edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    if (!w->ctl) { ed_state *e = calloc(1, sizeof *e); e->limit = 30000; w->ctl = e; }
    ed_state *e = w->ctl;
    int len = (int)strlen(w->text ? w->text : "");
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS | ((w->style & 4) ? DLGC_WANTALLKEYS : 0);
    case WM_SETFONT: e->font = wp; if (lp) ui_InvalidateRect(h, NULL, 1); return 0;
    case WM_GETFONT: return (LRESULT)e->font;
    case WM_SETTEXT: ui_DefWindowProcA(h, msg, wp, lp); e->sel0 = e->sel1 = 0; ui_InvalidateRect(h, NULL, 1); if (!(w->style & 4)) { notify_parent(w, 0x400); if ((w = ui_get(h))) notify_parent(w, 0x300); } return 1;
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
        int n = 0;
        if (wp == 8) { if (a == b && a > 0) a--; }
        else if (wp < 32) return 0;
        if (wp != 8 && len - (b - a) >= e->limit) return 0;
        char *buf = malloc((size_t)len + 2);
        if (!buf) return 0;
        memcpy(buf, t, (size_t)a); n = a;
        if (wp != 8) buf[n++] = (char)wp;
        int caret = n;
        size_t rest = strlen(t + b);
        memcpy(buf + n, t + b, rest); buf[n + rest] = 0;
        free(w->text); w->text = buf;
        e->sel0 = e->sel1 = caret;
        ui_InvalidateRect(h, NULL, 1);
        notify_parent(w, 0x400); if ((w = ui_get(h))) notify_parent(w, 0x300);
        return 1;
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
        return 1;
    }
    case 0xcf: if (wp) w->style |= 0x800; else w->style &= ~0x800u; return 1;   /* EM_SETREADONLY */
    case 0xcc: case 0xd2: case 0xd3: case 0xc8: case 0xc9: case 0xb5: case 0xb6: case 0xb7: return 0;
    case 0xe1: case 0xe9: return 0;
    }
    if (msg >= 0xb0 && msg <= 0xff) ui_log("edit: message %04x not handled", msg);
    return ui_DefWindowProcA(h, msg, wp, lp);
}
