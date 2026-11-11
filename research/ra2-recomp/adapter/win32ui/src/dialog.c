/* dialog.c: dialog templates, dialog creation, modal loop, DefDlgProc, dialog keyboard navigation. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dialog base units for "MS Sans Serif" 8 under Wine at 96 dpi. Checked against Wine window rects. */
static int base_x = 6, base_y = 13;
static INT muldiv(INT a, INT b, INT c)
{
    int64_t r = (int64_t)a * b;
    if ((r < 0) != (c < 0)) r -= c / 2; else r += c / 2;
    return (INT)(r / c);
}

/* ---- template parsing ---- */
typedef struct {
    DWORD style, exstyle, helpid; INT x, y, cx, cy; UINT id;
    char cls[64]; int cls_atom; char title[256]; int title_ord;
    const BYTE *data;
} dlg_item;
typedef struct {
    int ex; DWORD style, exstyle, helpid; int n; INT x, y, cx, cy;
    char cls[64]; char title[256]; int has_font; int pt; char font[64];
    const BYTE *items;
} dlg_tmpl;
static const BYTE *wstr(const BYTE *p, char *out, int n, int *ord)
{
    const WORD *w = (const WORD *)p;
    if (ord) *ord = -1;
    if (w[0] == 0xffff) { if (ord) *ord = w[1]; if (out) snprintf(out, (size_t)n, "#%u", w[1]); return p + 4; }
    int k = 0;
    while (*w) { if (out && k < n - 1) out[k++] = (char)(*w < 256 ? *w : '?'); w++; }
    if (out) out[k] = 0;
    return (const BYTE *)(w + 1);
}
static const BYTE *align4(const BYTE *p, const BYTE *base) { return base + (((p - base) + 3) & ~3); }
static void parse_tmpl(const BYTE *t, dlg_tmpl *d)
{
    memset(d, 0, sizeof *d);
    const BYTE *p;
    if (((const WORD *)t)[0] == 1 && ((const WORD *)t)[1] == 0xffff) {
        d->ex = 1;
        memcpy(&d->helpid, t + 4, 4); memcpy(&d->exstyle, t + 8, 4); memcpy(&d->style, t + 12, 4);
        d->n = *(const WORD *)(t + 16);
        d->x = *(const int16_t *)(t + 18); d->y = *(const int16_t *)(t + 20);
        d->cx = *(const int16_t *)(t + 22); d->cy = *(const int16_t *)(t + 24);
        p = t + 26;
    } else {
        memcpy(&d->style, t, 4); memcpy(&d->exstyle, t + 4, 4);
        d->n = *(const WORD *)(t + 8);
        d->x = *(const int16_t *)(t + 10); d->y = *(const int16_t *)(t + 12);
        d->cx = *(const int16_t *)(t + 14); d->cy = *(const int16_t *)(t + 16);
        p = t + 18;
    }
    int ord;
    p = wstr(p, NULL, 0, &ord);                      /* menu */
    if (ord != -1 || (p - t) > 2 + (d->ex ? 26 : 18)) ui_log("dialog template with a menu: ignored");
    p = wstr(p, d->cls, sizeof d->cls, &ord);         /* class */
    if (!d->cls[0]) strcpy(d->cls, "#32770");
    p = wstr(p, d->title, sizeof d->title, NULL);
    if (d->style & DS_SETFONT) {
        d->has_font = 1; d->pt = *(const WORD *)p; p += 2;
        if (d->ex) p += 4;
        p = wstr(p, d->font, sizeof d->font, NULL);
    }
    d->items = p;
    (void)t;
}
static const BYTE *parse_item(const BYTE *p, const BYTE *base, int ex, dlg_item *it)
{
    memset(it, 0, sizeof *it);
    p = align4(p, base);
    if (ex) {
        memcpy(&it->helpid, p, 4); memcpy(&it->exstyle, p + 4, 4); memcpy(&it->style, p + 8, 4);
        it->x = *(const int16_t *)(p + 12); it->y = *(const int16_t *)(p + 14);
        it->cx = *(const int16_t *)(p + 16); it->cy = *(const int16_t *)(p + 18);
        DWORD id; memcpy(&id, p + 20, 4); it->id = id; p += 24;
    } else {
        memcpy(&it->style, p, 4); memcpy(&it->exstyle, p + 4, 4);
        it->x = *(const int16_t *)(p + 8); it->y = *(const int16_t *)(p + 10);
        it->cx = *(const int16_t *)(p + 12); it->cy = *(const int16_t *)(p + 14);
        it->id = *(const WORD *)(p + 16); p += 18;
    }
    int ord;
    p = wstr(p, it->cls, sizeof it->cls, &ord);
    if (ord != -1) {
        static const char *n[] = { "Button", "Edit", "Static", "ListBox", "ScrollBar", "ComboBox" };
        if (ord >= 0x80 && ord <= 0x85) strcpy(it->cls, n[ord - 0x80]);
        it->cls_atom = ord;
    }
    p = wstr(p, it->title, sizeof it->title, &it->title_ord);
    WORD extra = *(const WORD *)p; p += 2;
    it->data = extra ? p : NULL;
    p += extra;
    return p;
}

/* ---- resources: game.exe dialog templates ---- */
const void *ui_find_dialog_resource(HINSTANCE inst, const char *name);

/* ---- tab order ---- */
static int tabbable(ui_win *c) { return (c->style & (WS_VISIBLE | WS_DISABLED)) == WS_VISIBLE; }
HWND UIAPI ui_GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL prev)
{
    ui_win *d = ui_get_logged(dlg, "GetNextDlgTabItem");
    if (!d || !d->child) return 0;
    ui_win *c = ctl ? ui_get(ctl) : NULL;
    if (c && c->parent != d) c = NULL;
    /* walk the child list cyclically */
    int n = 0; for (ui_win *x = d->child; x; x = x->next) n++;
    ui_win *start = c;
    ui_win *x = c;
    for (int i = 0; i < n; i++) {
        if (!prev) { x = x && x->next ? x->next : d->child; }
        else { if (x && x->prev) x = x->prev; else { x = d->child; while (x->next) x = x->next; } }
        if (x == start && start) break;
        if (tabbable(x) && (x->style & WS_TABSTOP)) return x->h;
    }
    return start ? start->h : 0;
}
static HWND first_group_item(ui_win *d)
{
    for (ui_win *x = d->child; x; x = x->next) if (tabbable(x)) return x->h;
    return 0;
}

/* ---- creation ---- */
HWND ui_create(DWORD ex, const char *clsname, const char *name, DWORD style, INT x, INT y, INT cx, INT cy,
               HWND hparent, HMENU menu, HINSTANCE inst, void *param, int dialog);
static HWND create_indirect(HINSTANCE inst, const void *tmpl, HWND owner, DLGPROC proc, LPARAM param, int modal, HWND *modal_owner)
{
    ui_controls_init();
    dlg_tmpl t; parse_tmpl(tmpl, &t);
    RECT r = { 0, 0, muldiv(t.cx, base_x, 4), muldiv(t.cy, base_y, 8) };
    if (t.style & DS_CONTROL) t.style &= ~(WS_CAPTION | WS_SYSMENU);
    t.style |= 4;   /* DS_3DLOOK */
    if (t.style & DS_MODALFRAME) t.exstyle |= WS_EX_DLGMODALFRAME;
    if ((t.style & DS_CONTROL) || !(t.style & WS_CHILD)) t.exstyle |= WS_EX_CONTROLPARENT;
    ui_AdjustWindowRectEx(&r, t.style, 0, t.exstyle);
    POINT pos = { r.left, r.top };
    SIZE sz = { r.right - r.left, r.bottom - r.top };
    if (t.style & DS_CENTER) {
        pos.x = (ui_GetSystemMetrics(SM_CXSCREEN) - sz.cx) / 2;
        pos.y = (ui_GetSystemMetrics(SM_CYSCREEN) - sz.cy) / 2;
    } else {
        pos.x += muldiv(t.x, base_x, 4); pos.y += muldiv(t.y, base_y, 8);
        if (!(t.style & (WS_CHILD | DS_ABSALIGN)) && owner) ui_ClientToScreen(owner, &pos);
    }
    if (!(t.style & WS_CHILD)) {
        int sw = ui_GetSystemMetrics(SM_CXSCREEN), sh = ui_GetSystemMetrics(SM_CYSCREEN), dx, dy;
        if ((dx = pos.x + sz.cx + ui_GetSystemMetrics(SM_CXDLGFRAME) - sw) > 0) pos.x -= dx;
        if ((dy = pos.y + sz.cy + ui_GetSystemMetrics(SM_CYDLGFRAME) - sh) > 0) pos.y -= dy;
        if (pos.x < 0) pos.x = 0;
        if (pos.y < 0) pos.y = 0;
    }
    HWND disabled_owner = 0;
    if (modal && owner) {
        ui_win *o = ui_get(owner);
        while (o && (o->style & (WS_POPUP | WS_CHILD)) == WS_CHILD && o->parent && o->parent != ui_desktop()) o = o->parent;
        owner = ui_handle(o);
        if (modal_owner) *modal_owner = owner;
        if (o && !(o->style & WS_DISABLED)) { disabled_owner = owner; ui_EnableWindow(owner, 0); }
    }
    HFONT font = t.has_font ? ui_CreateFontIndirectA(NULL) : 0;
    ui_trace("dialog create class=%s style=%08x %d,%d %dx%d items=%d", t.cls, t.style, pos.x, pos.y, sz.cx, sz.cy, t.n);
    HWND h = ui_create(t.exstyle, t.cls, t.title, t.style & ~WS_VISIBLE, pos.x, pos.y, sz.cx, sz.cy, owner, 0, inst, NULL, 1);
    if (!h) { if (disabled_owner) ui_EnableWindow(disabled_owner, 1); return 0; }
    ui_win *w = ui_get(h);
    w->is_dialog = 1; w->font = font; w->dlg_result = IDOK; w->dlg_modal = modal;
    if (w->n_extra >= DLGWINDOWEXTRA) { LONG v = (LONG)(uintptr_t)proc; memcpy(w->extra + DWL_DLGPROC, &v, 4); }
    if (proc && font) ui_send(w, WM_SETFONT, font, 0);
    /* controls */
    const BYTE *p = t.items; HWND defbtn = 0;
    for (int i = 0; i < t.n; i++) {
        dlg_item it; p = parse_item(p, tmpl, t.ex, &it);
        DWORD st = (it.style & ~WS_POPUP) | WS_CHILD, ex = it.exstyle;
        if (st & WS_BORDER) { st &= ~WS_BORDER; ex |= WS_EX_CLIENTEDGE; }
        char ordtitle[4]; const char *title = it.title;
        if (it.title_ord != -1) { ordtitle[0] = (char)0xff; ordtitle[1] = (char)(it.title_ord & 0xff); ordtitle[2] = (char)(it.title_ord >> 8); ordtitle[3] = 0; title = ordtitle; }
        HWND c = ui_create(ex | WS_EX_NOPARENTNOTIFY, it.cls, title, st,
                           muldiv(it.x, base_x, 4), muldiv(it.y, base_y, 8), muldiv(it.cx, base_x, 4), muldiv(it.cy, base_y, 8),
                           h, it.id, inst, (void *)(uintptr_t)it.data, 0);
        if (!c) {
            ui_log("dialog control %s id %u creation failed", it.cls, it.id);
            if (t.style & 0x10 /* DS_NOFAILCREATE */) continue;
            if (disabled_owner) ui_EnableWindow(disabled_owner, 1);
            ui_DestroyWindow(h); return 0;
        }
        if (font) ui_send_h(c, WM_SETFONT, font, 0);
        if (ui_send_h(c, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON) {
            if (defbtn) ui_send_h(defbtn, 0xf4 /* BM_SETSTYLE */, 0 /* BS_PUSHBUTTON */, 0);
            defbtn = c;
            if ((w = ui_get(h))) w->dlg_result = (int)ui_GetWindowLongA(c, GWL_ID);
        }
        if (!(w = ui_get(h))) return 0;
    }
    if (proc) {
        HWND focus = ui_GetNextDlgTabItem(h, 0, 0);
        if (!focus) focus = first_group_item(w);
        if (ui_send(w, WM_INITDIALOG, focus, param) && (w = ui_get(h)) && (!(t.style & DS_CONTROL) || (t.style & WS_VISIBLE))) {
            focus = ui_GetNextDlgTabItem(h, 0, 0);
            if (!focus) focus = first_group_item(w);
            if (focus) {
                if (ui_send_h(focus, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) ui_send_h(focus, 0xb1 /* EM_SETSEL */, 0, 0x7fffffff);
                ui_SetFocus(focus);
            } else if (!(t.style & WS_CHILD)) ui_SetFocus(h);
        }
    }
    if (!(w = ui_get(h))) return 0;
    if (modal && ui_capture()) ui_send(ui_capture(), WM_CANCELMODE, 0, 0);
    if ((t.style & WS_VISIBLE) && !(w->style & WS_VISIBLE)) {
        ui_ShowWindow(h, SW_SHOWNORMAL);
        ui_UpdateWindow(h);
    }
    return h;
}
HWND UIAPI ui_CreateDialogIndirectParamA(HINSTANCE i, const void *t, HWND p, DLGPROC f, LPARAM l)
{
    if (!t) return 0;
    return create_indirect(i, t, p, f, l, 0, NULL);
}
HWND UIAPI ui_CreateDialogParamA(HINSTANCE i, const char *name, HWND p, DLGPROC f, LPARAM l)
{
    const void *t = ui_find_dialog_resource(i, name);
    if (!t) { ui_log("CreateDialogParamA: no dialog %u", (unsigned)(uintptr_t)name); return 0; }
    ui_trace("CreateDialogParamA %u", (unsigned)(uintptr_t)name);
    return create_indirect(i, t, p, f, l, 0, NULL);
}
int ui_peek(MSG *m, HWND h, UINT lo, UINT hi, UINT flags);
void ui_wait_message(void);
static INT modal_loop(HWND h, HWND owner)
{
    ui_win *w = ui_get(h);
    if (!w) return -1;
    int first_empty = 1;
    MSG m;
    if (!w->dlg_end) {
        for (;;) {
            if (!ui_peek(&m, 0, 0, 0, PM_REMOVE)) {
                if (first_empty) { ui_ShowWindow(h, SW_SHOWNORMAL); first_empty = 0; }
                if ((w = ui_get(h)) && !(w->style & DS_NOIDLEMSG) && owner) ui_SendMessageA(owner, WM_ENTERIDLE, 0, (LPARAM)h);
                while (!ui_peek(&m, 0, 0, 0, PM_REMOVE)) ui_wait_message();
            }
            if (m.message == WM_QUIT) { ui_PostQuitMessage((INT)m.wParam); if (!ui_get(h)) return 0; break; }
            if (!(w = ui_get(h))) return 0;
            if (!w->dlg_end && !ui_IsDialogMessageA(h, &m)) { ui_TranslateMessage(&m); ui_DispatchMessageA(&m); }
            if (!(w = ui_get(h))) return 0;
            if (w->dlg_end) break;
            if (first_empty && m.message == WM_TIMER) { ui_ShowWindow(h, SW_SHOWNORMAL); first_empty = 0; }
        }
    }
    INT r = w->dlg_result;
    ui_DestroyWindow(h);
    return r;
}
INT UIAPI ui_DialogBoxIndirectParamA(HINSTANCE i, const void *t, HWND p, DLGPROC f, LPARAM l)
{
    HWND owner = 0;
    HWND h = create_indirect(i, t, p, f, l, 1, &owner);
    if (!h) return -1;
    INT r = modal_loop(h, owner);
    if (owner) ui_EnableWindow(owner, 1);   /* EndDialog re-enables; this covers a destroyed dialog */
    return r;
}
INT UIAPI ui_DialogBoxParamA(HINSTANCE i, const char *name, HWND p, DLGPROC f, LPARAM l)
{
    const void *t = ui_find_dialog_resource(i, name);
    if (!t) { ui_log("DialogBoxParamA: no dialog %u", (unsigned)(uintptr_t)name); return -1; }
    ui_trace("DialogBoxParamA %u", (unsigned)(uintptr_t)name);
    return ui_DialogBoxIndirectParamA(i, t, p, f, l);
}
BOOL UIAPI ui_EndDialog(HWND h, INT r)
{
    ui_win *w = ui_get_logged(h, "EndDialog");
    if (!w) return 0;
    w->dlg_result = r; w->dlg_end = 1;
    HWND owner = ui_handle(w->owner);
    if (owner && w->dlg_modal) ui_EnableWindow(owner, 1);
    if (ui_focus() == w || ui_is_child_of(ui_focus(), w)) ui_SetFocus(owner);
    ui_SetWindowPos(h, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    if (owner && ui_get(owner) && ui_active() == NULL) ui_SetForegroundWindow(owner);
    ui_PostMessageA(h, WM_NULL, 0, 0);
    return 1;
}

/* ---- dialog item helpers ---- */
HWND UIAPI ui_GetDlgItem(HWND h, INT id)
{
    ui_win *w = ui_get_logged(h, "GetDlgItem");
    if (!w) return 0;
    for (ui_win *c = w->child; c; c = c->next) if ((INT)c->id == id) return c->h;
    return 0;
}
INT UIAPI ui_GetDlgCtrlID(HWND h) { ui_win *w = ui_get_logged(h, "GetDlgCtrlID"); return w ? (INT)w->id : 0; }
LRESULT UIAPI ui_SendDlgItemMessageA(HWND h, INT id, UINT m, WPARAM wp, LPARAM lp)
{
    HWND c = ui_GetDlgItem(h, id);
    if (!c) { ui_trace("SendDlgItemMessageA: no item %d in %08x", id, h); return 0; }
    return ui_SendMessageA(c, m, wp, lp);
}
BOOL UIAPI ui_SetDlgItemTextA(HWND h, INT id, const char *s) { return (BOOL)ui_SendDlgItemMessageA(h, id, WM_SETTEXT, 0, (LPARAM)(uintptr_t)s); }
UINT UIAPI ui_IsDlgButtonChecked(HWND h, INT id) { return (UINT)ui_SendDlgItemMessageA(h, id, 0xf0 /* BM_GETCHECK */, 0, 0); }
BOOL UIAPI ui_CheckDlgButton(HWND h, INT id, UINT c) { ui_SendDlgItemMessageA(h, id, 0xf1 /* BM_SETCHECK */, c, 0); return 1; }

/* ---- DefDlgProc: the #32770 class procedure ---- */
static LONG get_extra(ui_win *w, int off) { LONG v = 0; if (w->n_extra >= off + 4) memcpy(&v, w->extra + off, 4); return v; }
static void set_extra(ui_win *w, int off, LONG v) { if (w->n_extra >= off + 4) memcpy(w->extra + off, &v, 4); }
static void save_focus(ui_win *w)
{
    ui_win *f = ui_focus();
    if (f && ui_is_child_of(f, w)) w->dlg_focus = f->h;
}
static void restore_focus(ui_win *w, int just_activate)
{
    if (w->dlg_end) return;
    HWND f = w->dlg_focus;
    if (!ui_get(f) || f == w->h) {
        if (just_activate) return;
        f = ui_GetNextDlgTabItem(w->h, 0, 0);
        if (!f) f = first_group_item(w);
        if (!ui_get(f)) return;
    }
    if (!just_activate && (ui_send_h(f, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)) ui_send_h(f, 0xb1, 0, -1);
    ui_SetFocus(f);
    if ((w = ui_get(w->h))) w->dlg_focus = 0;
}
static LRESULT dlg_default(ui_win *w, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND h = w->h;
    switch (msg) {
    case WM_ERASEBKGND: {
        LRESULT br = ui_send(w, WM_CTLCOLORDLG, wp, (LPARAM)h);
        (void)br;
        return 1;
    }
    case WM_NCDESTROY: return ui_DefWindowProcA(h, msg, wp, lp);
    case WM_SHOWWINDOW: if (!wp) save_focus(w); return ui_DefWindowProcA(h, msg, wp, lp);
    case WM_ACTIVATE: if (wp) restore_focus(w, 1); else save_focus(w); return 0;
    case WM_SETFOCUS: restore_focus(w, 0); return 0;
    case DM_SETDEFID: if (!w->dlg_end) w->dlg_result = (int)wp; return 1;
    case DM_GETDEFID:
        if (!w->dlg_end) {
            if (w->dlg_result) return MAKELONG(w->dlg_result, 0x534b /* DC_HASDEFID */);
            for (ui_win *c = w->child; c; c = c->next)
                if (ui_send(c, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON) return MAKELONG(c->id, 0x534b);
        }
        return 0;
    case WM_NEXTDLGCTL: {
        HWND dest = (HWND)wp;
        if (!lp) dest = ui_GetNextDlgTabItem(h, ui_GetFocus(), (BOOL)wp);
        if (dest) {
            if (ui_send_h(dest, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) ui_send_h(dest, 0xb1, 0, -1);
            ui_SetFocus(dest);
        }
        return 0;
    }
    case WM_ENTERMENULOOP: case WM_LBUTTONDOWN: case 0xa1 /* WM_NCLBUTTONDOWN */: {
        HWND f = ui_GetFocus();
        if (f && !ui_SendMessageA(f, 0x14f /* CB_SHOWDROPDOWN */, 0, 0)) ui_SendMessageA(ui_GetParent(f), 0x14f, 0, 0);
        return ui_DefWindowProcA(h, msg, wp, lp);
    }
    case WM_GETFONT: return (LRESULT)w->font;
    case WM_CLOSE: ui_PostMessageA(h, WM_COMMAND, MAKELONG(IDCANCEL, 0), (LPARAM)ui_GetDlgItem(h, IDCANCEL)); return 0;
    }
    return 0;
}
LRESULT UIAPI ui_DefDlgProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    set_extra(w, DWL_MSGRESULT, 0);
    DLGPROC proc = (DLGPROC)(uintptr_t)get_extra(w, DWL_DLGPROC);
    LRESULT r = proc ? proc(h, msg, wp, lp) : 0;
    if (!r && (w = ui_get(h))) {
        switch (msg) {
        case WM_ERASEBKGND: case WM_SHOWWINDOW: case WM_ACTIVATE: case WM_SETFOCUS: case DM_SETDEFID: case DM_GETDEFID:
        case WM_NEXTDLGCTL: case WM_GETFONT: case WM_CLOSE: case WM_NCDESTROY: case WM_ENTERMENULOOP:
        case WM_LBUTTONDOWN: case 0xa1:
            return dlg_default(w, msg, wp, lp);
        case WM_INITDIALOG: case WM_VKEYTOITEM: case WM_COMPAREITEM: case WM_CHARTOITEM: break;
        default: return ui_DefWindowProcA(h, msg, wp, lp);
        }
    }
    if ((msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) || msg == WM_COMPAREITEM || msg == WM_VKEYTOITEM ||
        msg == WM_CHARTOITEM || msg == 0x37 /* WM_QUERYDRAGICON */ || msg == WM_INITDIALOG)
        return r;
    w = ui_get(h);
    return w ? get_extra(w, DWL_MSGRESULT) : 0;
}

/* ---- IsDialogMessage: keyboard navigation ---- */
BOOL UIAPI ui_IsDialogMessageA(HWND h, MSG *m)
{
    ui_win *d = ui_get(h);
    if (!d || !m) return 0;
    ui_win *t = ui_get(m->hwnd);
    if (!t || (t != d && !ui_is_child_of(t, d))) return 0;
    if (m->message == WM_KEYDOWN || m->message == WM_CHAR || m->message == WM_SYSCHAR) {
        LRESULT code = ui_send(t, WM_GETDLGCODE, m->wParam, (LPARAM)(uintptr_t)m);
        if (code & DLGC_WANTMESSAGE) goto dispatch;
        if (m->message == WM_KEYDOWN) {
            switch (m->wParam) {
            case VK_TAB:
                if (!(code & DLGC_WANTTAB)) { ui_SendMessageA(h, WM_NEXTDLGCTL, (ui_GetKeyState(VK_SHIFT) & 0x8000) != 0, 0); return 1; }
                break;
            case VK_ESCAPE:
                ui_SendMessageA(h, WM_COMMAND, IDCANCEL, (LPARAM)ui_GetDlgItem(h, IDCANCEL));
                return 1;
            case VK_RETURN: {
                LRESULT def = ui_SendMessageA(h, DM_GETDEFID, 0, 0);
                UINT id = HIWORD(def) == 0x534b ? LOWORD(def) : IDOK;
                HWND b = ui_GetDlgItem(h, (INT)id);
                if (b && !ui_IsWindowEnabled(b)) return 1;
                ui_SendMessageA(h, WM_COMMAND, MAKELONG(id, 0), (LPARAM)b);
                return 1;
            }
            }
        }
        if (m->message == WM_CHAR && (code & DLGC_WANTCHARS)) goto dispatch;
    }
dispatch:
    ui_TranslateMessage(m);
    ui_DispatchMessageA(m);
    return 1;
}
