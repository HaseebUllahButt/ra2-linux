/* message.c: message queue, paint, timers, focus, activation, capture, keyboard and mouse messages. */
#include "ui.h"
#include "input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- posted message queue (one UI thread) ---- */
typedef struct qmsg { MSG m; struct qmsg *next; } qmsg;
static qmsg *qhead, *qtail;
static int quit_posted, quit_code;
static unsigned ui_thread;
void ui_lock(void);
void ui_unlock(void);
unsigned ui_thread_id(void);
static void check_thread(const char *who)
{
    unsigned t = ui_thread_id();
    if (!ui_thread) ui_thread = t;
    else if (t != ui_thread) {
        static int n;
        if (n++ < 20) ui_log("%s from a second thread %u (UI thread %u)", who, t, ui_thread);
    }
}
static POINT cursor;    /* screen coordinates */
static void fill_msg(MSG *m, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    m->hwnd = h; m->message = msg; m->wParam = wp; m->lParam = lp; m->time = ui_ticks(); m->pt = cursor;
}
void ui_post(ui_win *w, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    qmsg *q = calloc(1, sizeof *q);
    fill_msg(&q->m, w ? w->h : h, msg, wp, lp);
    ui_lock();
    if (qtail) qtail->next = q; else qhead = q;
    qtail = q;
    ui_unlock();
}
BOOL UIAPI ui_PostMessageA(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (h == 0xffff) { ui_log("PostMessageA broadcast %04x ignored", m); return 1; }
    ui_win *w = NULL;
    if (h) { w = ui_get_logged(h, "PostMessageA"); if (!w) return 0; }
    ui_trace("post %08x %04x %08x %08x", h, m, wp, (unsigned)lp);
    ui_post(w, h, m, wp, lp);
    return 1;
}
void UIAPI ui_PostQuitMessage(INT code) { quit_posted = 1; quit_code = code; ui_trace("PostQuitMessage %d", code); }
void ui_remove_window_messages(ui_win *w)
{
    ui_lock();
    qmsg **pp = &qhead; qtail = NULL;
    while (*pp) {
        if ((*pp)->m.hwnd == w->h) { qmsg *d = *pp; *pp = d->next; free(d); }
        else { qtail = *pp; pp = &(*pp)->next; }
    }
    ui_unlock();
}
static int match(ui_win *filter, HWND fh, const MSG *m, UINT lo, UINT hi)
{
    if (lo || hi) { if (m->message < lo || m->message > hi) return 0; }
    if (fh == (HWND)-1) return m->hwnd == 0;
    if (!fh) return 1;
    if (m->hwnd == fh) return 1;
    ui_win *w = ui_get(m->hwnd);
    return w && filter && ui_is_child_of(w, filter);
}
static int take_posted(MSG *out, ui_win *filter, HWND fh, UINT lo, UINT hi, int remove)
{
    int got = 0;
    ui_lock();
    qmsg *prev = NULL;
    for (qmsg *q = qhead; q; prev = q, q = q->next) {
        if (!match(filter, fh, &q->m, lo, hi)) continue;
        *out = q->m; got = 1;
        if (remove) {
            if (prev) prev->next = q->next; else qhead = q->next;
            if (qtail == q) qtail = prev;
            free(q);
        }
        break;
    }
    ui_unlock();
    return got;
}

/* ---- paint ---- */
static void rect_union(RECT *d, const RECT *a)
{
    if (a->left >= a->right || a->top >= a->bottom) return;
    if (d->left >= d->right || d->top >= d->bottom) { *d = *a; return; }
    d->left = UI_MIN(d->left, a->left); d->top = UI_MIN(d->top, a->top);
    d->right = UI_MAX(d->right, a->right); d->bottom = UI_MAX(d->bottom, a->bottom);
}
static int needs_paint(ui_win *w) { return w->has_upd || w->upd_internal; }
void ui_invalidate(ui_win *w, const RECT *rin, UINT flags)
{
    if (!w || w->destroyed) return;
    if (!ui_is_visible(w)) return;
    RECT cl = { 0, 0, w->cr.right - w->cr.left, w->cr.bottom - w->cr.top }, r;
    if (rin) { if (!ui_IntersectRect(&r, rin, &cl)) { if (!(flags & RDW_INTERNALPAINT)) goto children; r.left = r.right = 0; } }
    else r = cl;
    if (flags & RDW_INVALIDATE) {
        if (r.right > r.left && r.bottom > r.top) {
            if (!w->has_upd) { w->upd = r; w->has_upd = 1; } else rect_union(&w->upd, &r);
            if (flags & RDW_ERASE) w->upd_erase = 1;
        }
    } else if (flags & RDW_VALIDATE) {
        if (w->has_upd && (!rin || (r.left <= w->upd.left && r.top <= w->upd.top && r.right >= w->upd.right && r.bottom >= w->upd.bottom))) {
            w->has_upd = 0; w->upd_erase = 0; memset(&w->upd, 0, sizeof w->upd);
        }
        if (flags & RDW_NOERASE) w->upd_erase = 0;
    }
    if (flags & RDW_INTERNALPAINT) w->upd_internal = 1;
    if (flags & RDW_NOINTERNALPAINT) w->upd_internal = 0;
children:
    if ((flags & RDW_ALLCHILDREN) || (!(flags & RDW_NOCHILDREN) && !(w->style & WS_CLIPCHILDREN))) {
        for (ui_win *c = w->child; c; c = c->next) {
            if (!(c->style & WS_VISIBLE)) continue;
            RECT base = rin ? r : cl, cr;
            RECT ca = { c->cr.left, c->cr.top, c->cr.right, c->cr.bottom };
            if (!ui_IntersectRect(&cr, &base, &ca)) continue;
            cr.left -= c->cr.left; cr.right -= c->cr.left; cr.top -= c->cr.top; cr.bottom -= c->cr.top;
            ui_invalidate(c, &cr, flags & ~(RDW_UPDATENOW | RDW_ERASENOW));
        }
    }
}
static ui_win *find_paint(ui_win *parent)
{
    for (ui_win *w = parent->child; w; w = w->next) {
        if (!(w->style & WS_VISIBLE)) continue;
        if (needs_paint(w)) return w;
        ui_win *r = find_paint(w);
        if (r) return r;
    }
    return NULL;
}
static void erase_now(ui_win *w)
{
    if (w->upd_erase) {
        w->upd_erase = 0;
        HDC dc = ui_GetDC(w->h);
        ui_send(w, WM_ERASEBKGND, dc, 0);
        ui_ReleaseDC(w->h, dc);
    }
}
static void update_now(ui_win *w, UINT flags)
{
    if (!w || w->destroyed || !(w->style & WS_VISIBLE)) return;
    HWND h = w->h;
    if (needs_paint(w)) {
        if (flags & RDW_UPDATENOW) ui_send(w, WM_PAINT, 0, 0);
        else erase_now(w);
    }
    w = ui_get(h);
    if (!w) return;
    /* children: collect first, procs may change the tree */
    int n = 0; for (ui_win *c = w->child; c; c = c->next) n++;
    if (!n) return;
    HWND *list = malloc(sizeof(HWND) * (size_t)n); int k = 0;
    for (ui_win *c = w->child; c; c = c->next) list[k++] = c->h;
    for (int i = 0; i < k; i++) update_now(ui_get(list[i]), flags);
    free(list);
}
BOOL UIAPI ui_RedrawWindow(HWND h, const RECT *r, HRGN rgn, UINT flags)
{
    if (rgn) ui_log("RedrawWindow with a region: using the full window");
    ui_win *w = h ? ui_get_logged(h, "RedrawWindow") : ui_desktop();
    if (!w) return 0;
    ui_trace("RedrawWindow %08x %x", h, flags);
    ui_invalidate(w, rgn ? NULL : r, flags);
    if (flags & (RDW_UPDATENOW | RDW_ERASENOW)) update_now(w, flags);
    return 1;
}
BOOL UIAPI ui_InvalidateRect(HWND h, const RECT *r, BOOL erase)
{
    ui_win *w = h ? ui_get_logged(h, "InvalidateRect") : ui_desktop();
    if (!w) return 0;
    ui_invalidate(w, r, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
    return 1;
}
BOOL UIAPI ui_ValidateRect(HWND h, const RECT *r)
{
    ui_win *w = h ? ui_get_logged(h, "ValidateRect") : ui_desktop();
    if (!w) return 0;
    ui_invalidate(w, r, RDW_VALIDATE);
    return 1;
}
BOOL UIAPI ui_UpdateWindow(HWND h)
{
    ui_win *w = ui_get_logged(h, "UpdateWindow");
    if (!w) return 0;
    update_now(w, RDW_UPDATENOW);
    return 1;
}
BOOL UIAPI ui_GetUpdateRect(HWND h, RECT *r, BOOL erase)
{
    ui_win *w = ui_get_logged(h, "GetUpdateRect");
    if (!w) return 0;
    if (erase && w->has_upd) erase_now(w);
    if (r) { if (w->has_upd) *r = w->upd; else memset(r, 0, sizeof *r); }
    return w->has_upd;
}
HDC UIAPI ui_BeginPaint(HWND h, PAINTSTRUCT *ps)
{
    ui_win *w = ui_get_logged(h, "BeginPaint");
    if (!w || !ps) return 0;
    memset(ps, 0, sizeof *ps);
    ps->rcPaint = w->has_upd ? w->upd : (RECT){0, 0, 0, 0};
    int erase = w->upd_erase;
    w->has_upd = 0; w->upd_erase = 0; w->upd_internal = 0;
    ps->hdc = ui_GetDC(h);
    if (erase) ps->fErase = !ui_send(w, WM_ERASEBKGND, ps->hdc, 0);
    return ps->hdc;
}
BOOL UIAPI ui_EndPaint(HWND h, const PAINTSTRUCT *ps)
{
    if (ps) ui_ReleaseDC(h, ps->hdc);
    return 1;
}

/* ---- timers ---- */
typedef struct timer { HWND h; UINT id; UINT ms; TIMERPROC proc; DWORD due; struct timer *next; } timer;
static timer *timers;
static UINT next_sys_timer = 0x7fff;
UINT UIAPI ui_SetTimer(HWND h, UINT id, UINT ms, TIMERPROC f)
{
    if (h && !ui_get_logged(h, "SetTimer")) return 0;
    if (ms < 10) ms = 10;
    if (!h) id = ++next_sys_timer;
    timer *t;
    for (t = timers; t; t = t->next) if (t->h == h && t->id == id) break;
    if (!t) { t = calloc(1, sizeof *t); t->h = h; t->id = id; t->next = timers; timers = t; }
    t->ms = ms; t->proc = f; t->due = ui_ticks() + ms;
    ui_trace("SetTimer %08x %u %u %p", h, id, ms, (void *)f);
    return id ? id : 1;
}
BOOL UIAPI ui_KillTimer(HWND h, UINT id)
{
    for (timer **pp = &timers; *pp; pp = &(*pp)->next)
        if ((*pp)->h == h && (*pp)->id == id) { timer *d = *pp; *pp = d->next; free(d); return 1; }
    return 0;
}
void ui_timers_kill_window(ui_win *w)
{
    for (timer **pp = &timers; *pp;)
        if ((*pp)->h == w->h) { timer *d = *pp; *pp = d->next; free(d); } else pp = &(*pp)->next;
}
static int take_timer(MSG *out, ui_win *filter, HWND fh, UINT lo, UINT hi, int remove)
{
    DWORD now = ui_ticks();
    timer *best = NULL;
    for (timer *t = timers; t; t = t->next) {
        if ((int32_t)(now - t->due) < 0) continue;
        MSG m; fill_msg(&m, t->h, WM_TIMER, t->id, (LPARAM)(uintptr_t)t->proc);
        if (!match(filter, fh, &m, lo, hi)) continue;
        if (!best || (int32_t)(t->due - best->due) < 0) best = t;
    }
    if (!best) return 0;
    fill_msg(out, best->h, WM_TIMER, best->id, (LPARAM)(uintptr_t)best->proc);
    if (remove) best->due = now + best->ms;
    return 1;
}
DWORD ui_next_timer_wait(void)
{
    DWORD now = ui_ticks(), best = 50;
    for (timer *t = timers; t; t = t->next) {
        int32_t d = (int32_t)(t->due - now);
        if (d <= 0) return 0;
        if ((DWORD)d < best) best = (DWORD)d;
    }
    return best;
}

/* ---- focus, activation, capture ---- */
static ui_win *focus_w, *active_w, *capture_w;
static int app_active;
ui_win *ui_focus(void) { return focus_w; }
ui_win *ui_active(void) { return active_w; }
ui_win *ui_capture(void) { return capture_w; }
void ui_input_forget(ui_win *w)
{
    if (capture_w == w) capture_w = NULL;
    if (focus_w == w) focus_w = NULL;
    if (active_w == w) active_w = NULL;
}
void ui_activate(ui_win *w, int how)
{
    ui_win *top = ui_toplevel(w);
    if (top == ui_desktop()) top = NULL;
    if (top == active_w) return;
    ui_win *old = active_w;
    if (old) {
        ui_send(old, WM_NCACTIVATE, 0, (LPARAM)ui_handle(top));
        ui_send(old, WM_ACTIVATE, MAKELONG(WA_INACTIVE, 0), (LPARAM)ui_handle(top));
    }
    active_w = top;
    if (!top) return;
    if (!app_active) {
        app_active = 1;
        for (ui_win *t = ui_desktop()->child; t; t = t->next) ui_send(t, WM_ACTIVATEAPP, 1, 0);
    }
    ui_send(top, WM_NCACTIVATE, 1, (LPARAM)ui_handle(old));
    ui_send(top, WM_ACTIVATE, MAKELONG(how, 0), (LPARAM)ui_handle(old));
}
void ui_set_focus(ui_win *w)
{
    if (w == focus_w) return;
    if (w) {
        ui_win *top = ui_toplevel(w);
        if (top != active_w) { ui_activate(top, WA_ACTIVE); if (focus_w == w) return; }
    }
    ui_win *old = focus_w;
    focus_w = w;
    if (old) ui_send(old, WM_KILLFOCUS, ui_handle(w), 0);
    if (w && focus_w == w) ui_send(w, WM_SETFOCUS, ui_handle(old), 0);
}
HWND UIAPI ui_SetFocus(HWND h)
{
    HWND old = ui_handle(focus_w);
    ui_win *w = NULL;
    if (h) {
        w = ui_get_logged(h, "SetFocus");
        if (!w) return 0;
        if (w->style & WS_DISABLED) return 0;
    }
    ui_trace("SetFocus %08x (old %08x)", h, old);
    ui_set_focus(w);
    return old;
}
HWND UIAPI ui_GetFocus(void) { return ui_handle(focus_w); }
HWND UIAPI ui_GetActiveWindow(void) { return ui_handle(active_w); }
HWND UIAPI ui_GetForegroundWindow(void) { return ui_handle(active_w); }
HWND UIAPI ui_SetActiveWindow(HWND h)
{
    HWND old = ui_handle(active_w);
    ui_win *w = ui_get_logged(h, "SetActiveWindow");
    if (w) ui_activate(w, WA_ACTIVE);
    return old;
}
BOOL UIAPI ui_SetForegroundWindow(HWND h)
{
    ui_win *w = ui_get_logged(h, "SetForegroundWindow");
    if (!w) return 0;
    ui_activate(w, WA_ACTIVE);
    return 1;
}
HWND UIAPI ui_SetCapture(HWND h)
{
    HWND old = ui_handle(capture_w);
    ui_win *w = ui_get_logged(h, "SetCapture");
    if (w == capture_w) return old;
    ui_win *o = capture_w;
    capture_w = w;
    if (o) ui_send(o, WM_CAPTURECHANGED, 0, (LPARAM)h);
    return old;
}
BOOL UIAPI ui_ReleaseCapture(void)
{
    ui_win *o = capture_w;
    capture_w = NULL;
    if (o) ui_send(o, WM_CAPTURECHANGED, 0, 0);
    return 1;
}
HWND UIAPI ui_GetCapture(void) { return ui_handle(capture_w); }
void ui_window_shown(ui_win *w) { (void)w; }
void ui_hidden(ui_win *w)
{
    if (focus_w && (focus_w == w || ui_is_child_of(focus_w, w))) {
        ui_win *p = (w->style & WS_CHILD) ? w->parent : NULL;
        if (p == ui_desktop()) p = NULL;
        ui_set_focus(p);
    }
    if (capture_w && (capture_w == w || ui_is_child_of(capture_w, w))) ui_ReleaseCapture();
    if (active_w == w) {
        ui_win *next = NULL;
        for (ui_win *t = ui_desktop()->child; t; t = t->next) if (t != w && (t->style & WS_VISIBLE) && !(t->style & WS_DISABLED)) { next = t; break; }
        if (w->owner && (w->owner->style & WS_VISIBLE)) next = w->owner;
        ui_activate(next, WA_ACTIVE);
        if (!next) active_w = NULL;
    }
}

/* ---- keyboard and mouse ---- */
static BYTE keystate[256];   /* thread state: 0x80 down, 1 toggled */
static BYTE asyncstate[256]; /* 0x80 down, 1 pressed since last query */
static int cursor_count;
INT UIAPI ui_GetKeyState(INT vk)
{
    vk &= 0xff;
    INT r = (keystate[vk] & 0x80) ? (INT)0xffff8000 : 0;
    return r | (keystate[vk] & 1);
}
INT UIAPI ui_GetAsyncKeyState(INT vk)
{
    vk &= 0xff;
    INT r = (asyncstate[vk] & 0x80) ? (INT)0xffff8000 : 0;
    if (asyncstate[vk] & 1) r |= 1;
    asyncstate[vk] &= ~1;
    return r;
}
static void set_key(BYTE *st, int vk, int down)
{
    if (down) { if (!(st[vk] & 0x80)) st[vk] ^= 1; st[vk] |= 0x80; }
    else st[vk] &= (BYTE)~0x80;
}
static void key_both(BYTE *st, int vk, int down)
{
    set_key(st, vk, down);
    int gen = 0;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) gen = VK_SHIFT;
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) gen = VK_CONTROL;
    if (vk == VK_LMENU || vk == VK_RMENU) gen = VK_MENU;
    if (gen) {
        int l = gen == VK_SHIFT ? VK_LSHIFT : gen == VK_CONTROL ? VK_LCONTROL : VK_LMENU;
        int dn = (st[l] & 0x80) || (st[l + 1] & 0x80);
        set_key(st, gen, dn);
    }
}
BOOL UIAPI ui_GetCursorPos(POINT *p) { if (!p) return 0; *p = cursor; return 1; }
void ui_sdl_warp(INT x, INT y);
BOOL UIAPI ui_SetCursorPos(INT x, INT y)
{
    ui_sdl_warp(x, y);
    cursor.x = x; cursor.y = y;
    return 1;
}
void ui_sdl_show_cursor(int show);
INT UIAPI ui_ShowCursor(BOOL show)
{
    cursor_count += show ? 1 : -1;
    ui_sdl_show_cursor(cursor_count >= 0);
    return cursor_count;
}
static HCURSOR cur_cursor;
HCURSOR UIAPI ui_SetCursor(HCURSOR c) { HCURSOR o = cur_cursor; cur_cursor = c; return o; }
HCURSOR UIAPI ui_LoadCursorA(HINSTANCE i, const char *name)
{
    (void)i;
    return 0x00b10000u | (IS_INTRESOURCE(name) ? (DWORD)(uintptr_t)name & 0xffff : 0xfff0);
}
HICON UIAPI ui_LoadIconA(HINSTANCE i, const char *name)
{
    (void)i;
    return 0x00b20000u | (IS_INTRESOURCE(name) ? (DWORD)(uintptr_t)name & 0xffff : 0xfff0);
}

/* raw input from the platform: fifo of events, turned into messages when read */
#define NRAW 512
static ui_raw raw[NRAW];
static int raw_head, raw_tail;
void ui_raw_push(const ui_raw *r)
{
    int n = (raw_tail + 1) % NRAW;
    if (n == raw_head) { ui_log("input queue full, event dropped"); return; }
    raw[raw_tail] = *r; raw_tail = n;
    if (r->kind == UI_RAW_KEY) key_both(asyncstate, r->vk, r->down), asyncstate[r->vk] |= r->down ? 1 : 0;
    if (r->kind == UI_RAW_BUTTON) {
        int vk = r->button == 1 ? VK_LBUTTON : r->button == 2 ? VK_RBUTTON : VK_MBUTTON;
        set_key(asyncstate, vk, r->down); if (r->down) asyncstate[vk] |= 1;
    }
}
static struct { DWORD time; UINT msg; HWND h; POINT p; } lastclick;
static UINT mouse_mk(void)
{
    UINT k = 0;
    if (keystate[VK_LBUTTON] & 0x80) k |= MK_LBUTTON;
    if (keystate[VK_RBUTTON] & 0x80) k |= MK_RBUTTON;
    if (keystate[VK_MBUTTON] & 0x80) k |= MK_MBUTTON;
    if (keystate[VK_SHIFT] & 0x80) k |= MK_SHIFT;
    if (keystate[VK_CONTROL] & 0x80) k |= MK_CONTROL;
    return k;
}
/* Turn the next raw event into a message. Returns 1 with *out set, 0 if the event made no message. */
static int raw_to_msg(const ui_raw *r, MSG *out)
{
    if (r->kind == UI_RAW_KEY) {
        key_both(keystate, r->vk, r->down);
        ui_win *t = focus_w ? focus_w : active_w;
        if (!t) return 0;
        int alt = (keystate[VK_MENU] & 0x80) != 0;
        UINT msg = r->down ? WM_KEYDOWN : WM_KEYUP;
        int sys = (alt && !(keystate[VK_CONTROL] & 0x80)) || r->vk == VK_MENU || r->vk == 0x79 /* F10 */ || !focus_w;
        if (sys) msg = r->down ? WM_SYSKEYDOWN : WM_SYSKEYUP;
        int vk = r->vk;
        if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
        if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
        if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
        DWORD lp = 1 | ((DWORD)r->scan << 16) | (r->ext ? 1u << 24 : 0) | (alt ? 1u << 29 : 0);
        if (!r->down) lp |= 3u << 30;
        else if (r->repeat) lp |= 1u << 30;
        fill_msg(out, t->h, msg, (WPARAM)vk, (LPARAM)lp); out->time = r->time;
        return 1;
    }
    if (r->kind == UI_RAW_WHEEL) {
        ui_win *t = focus_w ? focus_w : active_w;
        if (!t) return 0;
        fill_msg(out, t->h, WM_MOUSEWHEEL, MAKELONG(mouse_mk(), r->wheel * 120), MAKELONG(cursor.x, cursor.y));
        return 1;
    }
    /* mouse */
    cursor.x = r->x; cursor.y = r->y;
    UINT msg = WM_MOUSEMOVE;
    if (r->kind == UI_RAW_BUTTON) {
        int vk = r->button == 1 ? VK_LBUTTON : r->button == 2 ? VK_RBUTTON : VK_MBUTTON;
        set_key(keystate, vk, r->down);
        msg = r->button == 1 ? (r->down ? WM_LBUTTONDOWN : WM_LBUTTONUP)
            : r->button == 2 ? (r->down ? WM_RBUTTONDOWN : WM_RBUTTONUP) : (r->down ? WM_MBUTTONDOWN : WM_MBUTTONUP);
    }
    ui_win *t; int hit = HTCLIENT;
    if (capture_w) t = capture_w;
    else {
        t = ui_window_from_point(ui_desktop(), cursor, &hit);
        if (!t) return 0;
    }
    HWND th = t->h;
    /* activation on click */
    if (r->kind == UI_RAW_BUTTON && r->down && !capture_w) {
        ui_win *top = ui_toplevel(t);
        if (top && top != active_w) {
            LRESULT ma = ui_send(t, WM_MOUSEACTIVATE, ui_handle(top), MAKELONG(hit, msg));
            if (ma == MA_ACTIVATE || ma == MA_ACTIVATEANDEAT || !ma) ui_activate(top, WA_CLICKACTIVE);
            if (ma == MA_ACTIVATEANDEAT || ma == MA_NOACTIVATEANDEAT) return 0;
            t = ui_get(th); if (!t) return 0;
        }
    }
    if (!capture_w) {
        ui_send(t, WM_SETCURSOR, th, MAKELONG(hit, msg));
        t = ui_get(th); if (!t) return 0;
    }
    if (hit != HTCLIENT && !capture_w) {
        fill_msg(out, th, msg - WM_MOUSEMOVE + WM_NCMOUSEMOVE, (WPARAM)hit, MAKELONG(cursor.x, cursor.y));
        out->time = r->time;
        return 1;
    }
    /* double click */
    if (r->kind == UI_RAW_BUTTON && r->down) {
        if (t->cls && (t->cls->style & 8 /* CS_DBLCLKS */) && lastclick.msg == msg && lastclick.h == th &&
            r->time - lastclick.time < 500 && abs(cursor.x - lastclick.p.x) <= 2 && abs(cursor.y - lastclick.p.y) <= 2) {
            msg += 2; lastclick.msg = 0;
        } else { lastclick.msg = msg; lastclick.h = th; lastclick.time = r->time; lastclick.p = cursor; }
    }
    POINT cp = cursor; ui_screen_to_client(t, &cp);
    fill_msg(out, th, msg, mouse_mk(), MAKELONG(cp.x, cp.y));
    out->time = r->time;
    return 1;
}
static MSG pend_in; static int has_pend_in;   /* converted input kept by a PM_NOREMOVE peek */
static int take_input(MSG *out, ui_win *filter, HWND fh, UINT lo, UINT hi, int remove)
{
    for (;;) {
        if (!has_pend_in) {
            if (raw_head == raw_tail) return 0;
            ui_raw *r = &raw[raw_head];
            raw_head = (raw_head + 1) % NRAW;
            if (!raw_to_msg(r, &pend_in)) continue;
            has_pend_in = 1;
        }
        if (!match(filter, fh, &pend_in, lo, hi)) return 0;   /* stays queued, like a hardware message */
        *out = pend_in;
        if (remove) has_pend_in = 0;
        return 1;
    }
}
/* ---- peek and dispatch ---- */
int ui_peek(MSG *m, HWND h, UINT lo, UINT hi, UINT flags)
{
    check_thread("PeekMessageA");
    ui_win *filter = NULL;
    if (h && h != (HWND)-1) { filter = ui_get_logged(h, "PeekMessageA"); if (!filter) return 0; }
    int remove = flags & PM_REMOVE;
    ui_pump_input();
    if (take_posted(m, filter, h, lo, hi, remove)) return 1;
    if (quit_posted && (!lo && !hi)) {
        fill_msg(m, 0, WM_QUIT, (WPARAM)quit_code, 0);
        if (remove) quit_posted = 0;
        return 1;
    }
    if (take_input(m, filter, h, lo, hi, remove)) return 1;
    if (!lo || (lo <= WM_PAINT && hi >= WM_PAINT)) {
        ui_win *p = find_paint(ui_desktop());
        if (p && (!filter || p == filter || ui_is_child_of(p, filter)) && h != (HWND)-1) {
            fill_msg(m, p->h, WM_PAINT, 0, 0);
            if (p->upd_internal && !p->has_upd) p->upd_internal = 0;
            return 1;
        }
    }
    if (take_timer(m, filter, h, lo, hi, remove)) return 1;
    return 0;
}
BOOL UIAPI ui_PeekMessageA(MSG *m, HWND h, UINT lo, UINT hi, UINT flags)
{
    if (!m) return 0;
    int r = ui_peek(m, h, lo, hi, flags);
    if (r && ui_trace_on && m->message != WM_TIMER) ui_trace("peek -> %08x %04x %08x %08x", m->hwnd, m->message, m->wParam, (unsigned)m->lParam);
    return r;
}
LRESULT UIAPI ui_DispatchMessageA(const MSG *m)
{
    if (!m) return 0;
    if (m->message == WM_TIMER && m->lParam) {
        TIMERPROC f = (TIMERPROC)(uintptr_t)m->lParam;
        f(m->hwnd, WM_TIMER, m->wParam, m->time);
        return 0;
    }
    if (!m->hwnd) return 0;
    ui_win *w = ui_get_logged(m->hwnd, "DispatchMessageA");
    if (!w) return 0;
    LRESULT r = ui_call_proc(w->proc, m->hwnd, m->message, m->wParam, m->lParam);
    if (m->message == WM_PAINT && (w = ui_get(m->hwnd)) && w->upd_erase) erase_now(w);
    return r;
}
/* US keyboard layout */
static int vk_to_char(int vk, int shift, int caps)
{
    if (vk >= 'A' && vk <= 'Z') { int up = shift ^ caps; return up ? vk : vk + 32; }
    if (vk >= '0' && vk <= '9') { static const char s[] = ")!@#$%^&*("; return shift ? s[vk - '0'] : vk; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9) return '0' + vk - VK_NUMPAD0;
    switch (vk) {
    case VK_SPACE: return ' ';
    case VK_RETURN: return '\r';
    case VK_BACK: return 8;
    case VK_TAB: return 9;
    case VK_ESCAPE: return 27;
    case VK_MULTIPLY: return '*';
    case VK_ADD: return '+';
    case VK_SUBTRACT: return '-';
    case VK_DECIMAL: return '.';
    case VK_DIVIDE: return '/';
    case VK_OEM_1: return shift ? ':' : ';';
    case VK_OEM_PLUS: return shift ? '+' : '=';
    case VK_OEM_COMMA: return shift ? '<' : ',';
    case VK_OEM_MINUS: return shift ? '_' : '-';
    case VK_OEM_PERIOD: return shift ? '>' : '.';
    case VK_OEM_2: return shift ? '?' : '/';
    case VK_OEM_3: return shift ? '~' : '`';
    case VK_OEM_4: return shift ? '{' : '[';
    case VK_OEM_5: return shift ? '|' : '\\';
    case VK_OEM_6: return shift ? '}' : ']';
    case VK_OEM_7: return shift ? '"' : '\'';
    }
    return 0;
}
INT UIAPI ui_ToAscii(UINT vk, UINT scan, const BYTE *st, WORD *out, UINT flags)
{
    (void)flags;
    if (scan & 0x8000) return 0;   /* key up */
    if (!st) st = keystate;
    int ctrl = (st[VK_CONTROL] & 0x80) != 0;
    int c = vk_to_char((int)vk & 0xff, (st[VK_SHIFT] & 0x80) != 0, st[VK_CAPITAL] & 1);
    if (ctrl && c) {
        if (c >= 'a' && c <= 'z') c -= 96;
        else if (c >= 'A' && c <= 'Z') c -= 64;
        else if (c != 8 && c != 13 && c != 27 && c != 9) c = 0;
    }
    if (!c) return 0;
    if (out) *out = (WORD)c;
    return 1;
}
BOOL UIAPI ui_TranslateMessage(const MSG *m)
{
    if (!m) return 0;
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return 0;
    WORD ch;
    if (ui_ToAscii(m->wParam, (m->lParam >> 16) & 0xff, keystate, &ch, 0) != 1) return 0;
    UINT msg = m->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR;
    /* WM_CHAR goes to the front of the queue */
    qmsg *q = calloc(1, sizeof *q);
    q->m = *m; q->m.message = msg; q->m.wParam = ch;
    ui_lock();
    q->next = qhead; qhead = q; if (!qtail) qtail = q;
    ui_unlock();
    return 1;
}
INT UIAPI ui_TranslateAcceleratorA(HWND h, HACCEL a, MSG *m) { (void)h; (void)a; (void)m; return 0; }
UINT UIAPI ui_MapVirtualKeyA(UINT code, UINT type)
{
    switch (type) {
    case 0: return ui_vk_to_scan(code);
    case 1: return ui_scan_to_vk(code);
    case 2: { int c = vk_to_char((int)code, 0, 0); return (UINT)(c >= 'a' && c <= 'z' ? c - 32 : c); }
    case 3: return ui_scan_to_vk(code);
    }
    return 0;
}
INT UIAPI ui_GetKeyNameTextA(LONG l, char *buf, INT n)
{
    if (!buf || n <= 0) return 0;
    const char *name = ui_scan_name((l >> 16) & 0xff, (l >> 24) & 1);
    snprintf(buf, (size_t)n, "%s", name);
    return (INT)strlen(buf);
}
BOOL UIAPI ui_RegisterHotKey(HWND h, INT id, UINT mods, UINT vk)
{
    ui_log("RegisterHotKey(%08x, %d, %x, %x): not delivered", h, id, mods, vk);
    return 1;
}
/* wait until input, a posted message or a timer is ready */
void ui_platform_wait(DWORD ms);
void ui_wait_message(void)
{
    if (qhead || raw_head != raw_tail || has_pend_in || find_paint(ui_desktop()) || quit_posted) return;
    DWORD ms = ui_next_timer_wait();
    if (ms) ui_platform_wait(ms);
}
int ui_quit_pending(void) { return quit_posted; }
