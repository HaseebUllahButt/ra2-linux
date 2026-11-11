/* window.c: window objects, handle table, classes, tree, position, styles, long values. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

int ui_trace_on;
static FILE *logf_;
static void log_open(void)
{
    static int done;
    if (done) return;
    done = 1;
    const char *p = getenv("RA2UI_LOG");
    logf_ = p && *p ? fopen(p, "w") : stderr;
    const char *t = getenv("RA2UI_TRACE");
    ui_trace_on = t && *t && *t != '0';
}
void ui_log(const char *fmt, ...)
{
    log_open();
    if (!logf_) return;
    va_list ap; va_start(ap, fmt); vfprintf(logf_, fmt, ap); va_end(ap);
    fputc('\n', logf_); fflush(logf_);
}
void ui_trace(const char *fmt, ...)
{
    log_open();
    if (!ui_trace_on || !logf_) return;
    va_list ap; va_start(ap, fmt); vfprintf(logf_, fmt, ap); va_end(ap);
    fputc('\n', logf_); fflush(logf_);
}
void ui_exit_process(int code);
_Noreturn void ui_fatal(const char *fmt, ...)
{
    log_open();
    va_list ap; va_start(ap, fmt);
    if (logf_) { vfprintf(logf_, fmt, ap); fputc('\n', logf_); fflush(logf_); }
    va_end(ap);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
    ui_exit_process(70);
    abort();
}
char *ui_strdup(const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s) + 1; char *d = malloc(n); memcpy(d, s, n); return d;
}
int ui_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y || !x) return x - y;
    }
}

/* ---- handle table ----
 * Handle = 0x00c00000 + 4*index. The main window can also carry a native handle
 * (the SDL window's HWND on Windows), kept in a small alias list. */
#define MAXWIN 8192
static ui_win *table[MAXWIN];
static int next_free = 1;
static struct { HWND native; ui_win *w; } alias[8];
static ui_win desktop;
ui_win *ui_desktop(void)
{
    if (!desktop.h) {
        desktop.h = 0x00c00000; desktop.style = WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        desktop.wr.right = desktop.cr.right = 1024; desktop.wr.bottom = desktop.cr.bottom = 768;
        table[0] = &desktop;
    }
    return &desktop;
}
ui_win *ui_get(HWND h)
{
    if (!h) return NULL;
    if ((h & 0xfff00000u) == 0x00c00000u && !(h & 3)) {
        unsigned i = (h - 0x00c00000u) >> 2;
        if (i < MAXWIN && table[i] && !table[i]->destroyed) return table[i];
    }
    for (int i = 0; i < 8; i++) if (alias[i].native == h && alias[i].w && !alias[i].w->destroyed) return alias[i].w;
    return NULL;
}
ui_win *ui_get_logged(HWND h, const char *who)
{
    ui_win *w = ui_get(h);
    if (!w && h) ui_trace("%s: invalid window %08x", who, h);
    return w;
}
HWND ui_handle(ui_win *w) { return w ? w->h : 0; }
void ui_set_alias(ui_win *w, HWND native)
{
    for (int i = 0; i < 8; i++) if (!alias[i].w || alias[i].w == w) { alias[i].native = native; alias[i].w = w; w->h = native; return; }
}
static ui_win *alloc_win(void)
{
    ui_desktop();
    for (int n = 0; n < MAXWIN; n++) {
        int i = next_free; next_free = next_free + 1 < MAXWIN ? next_free + 1 : 1;
        if (!table[i]) {
            ui_win *w = calloc(1, sizeof *w);
            w->h = 0x00c00000u + 4u * (unsigned)i; table[i] = w; return w;
        }
    }
    ui_fatal("win32ui: window table full");
}
static void free_win(ui_win *w)
{
    for (int i = 0; i < 8; i++) if (alias[i].w == w) alias[i].w = NULL, alias[i].native = 0;
    unsigned i = (w->h - 0x00c00000u) >> 2;
    if ((w->h & 0xfff00000u) == 0x00c00000u && i < MAXWIN && table[i] == w) table[i] = NULL;
    for (int k = 1; k < MAXWIN; k++) if (table[k] == w) table[k] = NULL;
    free(w->text); free(w->extra); free(w);
}

/* ---- classes ---- */
static struct ui_class *classes;
const struct ui_class *ui_find_class(const char *name, HINSTANCE inst)
{
    (void)inst;
    char buf[16];
    if (IS_INTRESOURCE(name)) { snprintf(buf, sizeof buf, "#%u", (unsigned)(uintptr_t)name); name = buf; }
    for (struct ui_class *c = classes; c; c = c->next) if (!c->builtin && !ui_stricmp(c->name, name)) return c;
    for (struct ui_class *c = classes; c; c = c->next) if (c->builtin && !ui_stricmp(c->name, name)) return c;
    return NULL;
}
void ui_register_builtin(const char *name, WNDPROC proc, UINT style, int wnd_extra, HCURSOR cur)
{
    struct ui_class *c = calloc(1, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->proc = proc; c->style = style; c->wnd_extra = wnd_extra; c->builtin = 1; c->cursor = cur;
    c->next = classes; classes = c;
}
static WORD next_atom = 0xc000;
WORD UIAPI ui_RegisterClassA(const WNDCLASSA *wc)
{
    ui_controls_init();
    if (!wc || !wc->lpszClassName) return 0;
    const char *name = wc->lpszClassName;
    if (IS_INTRESOURCE(name)) ui_fatal("win32ui: RegisterClassA with atom name");
    for (struct ui_class *c = classes; c; c = c->next) if (!c->builtin && !ui_stricmp(c->name, name)) return 0;
    struct ui_class *c = calloc(1, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->style = wc->style; c->proc = wc->lpfnWndProc; c->cls_extra = wc->cbClsExtra; c->wnd_extra = wc->cbWndExtra;
    c->inst = wc->hInstance; c->icon = wc->hIcon; c->cursor = wc->hCursor; c->bg = wc->hbrBackground;
    c->next = classes; classes = c;
    ui_trace("RegisterClassA %s proc=%p", name, (void *)wc->lpfnWndProc);
    return next_atom++;
}
INT UIAPI ui_GetClassNameA(HWND h, char *buf, INT n)
{
    ui_win *w = ui_get_logged(h, "GetClassNameA");
    if (!w || !buf || n <= 0) return 0;
    snprintf(buf, (size_t)n, "%s", w->cls ? w->cls->name : "");
    return (INT)strlen(buf);
}

/* ---- tree ---- */
static void unlink_win(ui_win *w)
{
    if (!w->parent) return;
    if (w->prev) w->prev->next = w->next; else w->parent->child = w->next;
    if (w->next) w->next->prev = w->prev;
    w->next = w->prev = NULL;
}
/* insert: after == NULL means top; (ui_win*)1 means bottom */
static void link_win(ui_win *w, ui_win *parent, ui_win *after)
{
    w->parent = parent;
    if (!after || !parent->child) {
        w->next = parent->child; w->prev = NULL;
        if (parent->child) parent->child->prev = w;
        parent->child = w; return;
    }
    if (after == (ui_win *)1) { after = parent->child; while (after->next) after = after->next; }
    w->prev = after; w->next = after->next;
    if (after->next) after->next->prev = w;
    after->next = w;
}
int ui_is_child_of(ui_win *w, ui_win *parent)
{
    for (w = w ? w->parent : NULL; w; w = w->parent) if (w == parent) return 1;
    return 0;
}
ui_win *ui_toplevel(ui_win *w)
{
    while (w && w->parent && w->parent != &desktop && (w->style & WS_CHILD)) w = w->parent;
    return w;
}
int ui_is_visible(ui_win *w)
{
    for (; w && w != &desktop; w = w->parent) if (!(w->style & WS_VISIBLE)) return 0;
    return w != NULL;
}
static void origin(ui_win *w, POINT *p)   /* screen position of the client origin */
{
    p->x = p->y = 0;
    for (; w && w != &desktop; w = w->parent) { p->x += w->cr.left; p->y += w->cr.top; }
}
void ui_client_to_screen(ui_win *w, POINT *p) { POINT o; origin(w, &o); p->x += o.x; p->y += o.y; }
void ui_screen_to_client(ui_win *w, POINT *p) { POINT o; origin(w, &o); p->x -= o.x; p->y -= o.y; }

/* ---- non-client metrics (Wine defaults, checked against Wine later) ---- */
INT UIAPI ui_GetSystemMetrics(INT i)
{
    switch (i) {
    case SM_CXSCREEN: return ui_desktop()->wr.right;
    case SM_CYSCREEN: return ui_desktop()->wr.bottom;
    case SM_CXVSCROLL: case SM_CYHSCROLL: return 16;
    case SM_CYCAPTION: return 19;
    case SM_CXBORDER: case SM_CYBORDER: return 1;
    case SM_CXDLGFRAME: case SM_CYDLGFRAME: return 3;
    case SM_CXICON: case SM_CYICON: return 32;
    case SM_CXCURSOR: case SM_CYCURSOR: return 32;
    case SM_CYMENU: return 19;
    case SM_CXFRAME: case SM_CYFRAME: return 4;
    case SM_CXEDGE: case SM_CYEDGE: return 2;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    case 19: return 1;          /* SM_MOUSEPRESENT */
    case 23: return 0;          /* SM_SWAPBUTTON */
    case 43: return 3;          /* SM_CMOUSEBUTTONS */
    case 75: return 1;          /* SM_MOUSEWHEELPRESENT */
    case 16: return ui_desktop()->wr.right;                       /* SM_CXFULLSCREEN */
    case 17: return ui_desktop()->wr.bottom - 19;                 /* SM_CYFULLSCREEN */
    case 0x1000: return 0;      /* SM_REMOTESESSION */
    }
    ui_log("GetSystemMetrics(%d): unknown index, returning 0", i);
    return 0;
}
static void nc_size(DWORD style, DWORD ex, RECT *r)
{
    int fx = 0, fy = 0;
    if ((style & WS_THICKFRAME)) fx = fy = ui_GetSystemMetrics(SM_CXFRAME);
    else if ((ex & WS_EX_DLGMODALFRAME) || ((style & WS_DLGFRAME) && !(style & WS_BORDER)) || (style & WS_CAPTION) == WS_CAPTION)
        fx = fy = ui_GetSystemMetrics(SM_CXDLGFRAME);
    else if (style & WS_BORDER) fx = fy = 1;
    if ((ex & WS_EX_DLGMODALFRAME) && (style & WS_BORDER) && !(style & WS_DLGFRAME) && !(style & WS_THICKFRAME)) fx = fy = 3;
    r->left -= fx; r->right += fx; r->top -= fy; r->bottom += fy;
    if ((style & WS_CAPTION) == WS_CAPTION) r->top -= ui_GetSystemMetrics(SM_CYCAPTION);
    if (ex & WS_EX_CLIENTEDGE) { r->left -= 2; r->top -= 2; r->right += 2; r->bottom += 2; }
    if (ex & WS_EX_STATICEDGE) { r->left -= 1; r->top -= 1; r->right += 1; r->bottom += 1; }
}
BOOL UIAPI ui_AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex)
{
    if (!r) return 0;
    nc_size(style, ex, r);
    if (menu) r->top -= ui_GetSystemMetrics(SM_CYMENU);
    return 1;
}
BOOL UIAPI ui_AdjustWindowRect(RECT *r, DWORD style, BOOL menu) { return ui_AdjustWindowRectEx(r, style, menu, 0); }
static void calc_client(ui_win *w)
{
    RECT nc = {0, 0, 0, 0};
    nc_size(w->style, w->exstyle, &nc);
    w->cr.left = w->wr.left - nc.left; w->cr.top = w->wr.top - nc.top;
    w->cr.right = w->wr.right - nc.right; w->cr.bottom = w->wr.bottom - nc.bottom;
    if (w->cr.right < w->cr.left) w->cr.right = w->cr.left;
    if (w->cr.bottom < w->cr.top) w->cr.bottom = w->cr.top;
}

/* ---- message sending ---- */
LRESULT ui_call_proc(WNDPROC p, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!p) return 0;
    return p(h, msg, wp, lp);
}
LRESULT ui_send(ui_win *w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!w || w->destroyed || w == &desktop) return 0;
    if (ui_trace_on) ui_trace("send %08x %04x %08x %08x", w->h, msg, wp, (unsigned)lp);
    return ui_call_proc(w->proc, w->h, msg, wp, lp);
}
LRESULT ui_send_h(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return ui_send(ui_get(h), msg, wp, lp); }
LRESULT UIAPI ui_SendMessageA(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (h == 0xffff) { ui_log("SendMessageA broadcast %04x ignored", m); return 0; }
    ui_win *w = ui_get_logged(h, "SendMessageA");
    return w ? ui_send(w, m, wp, lp) : 0;
}
LRESULT UIAPI ui_CallWindowProcA(WNDPROC p, HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    return ui_call_proc(p, h, m, wp, lp);
}

/* ---- creation ---- */
static void send_parent_notify(ui_win *w, UINT msg)
{
    for (;;) {
        if (!(w->style & WS_CHILD) || (w->exstyle & WS_EX_NOPARENTNOTIFY) || !w->parent || w->parent == &desktop) return;
        ui_win *p = w->parent;
        ui_send(p, WM_PARENTNOTIFY, MAKELONG(msg, w->id), (LPARAM)w->h);
        w = p;
    }
}
HWND ui_create(DWORD ex, const char *clsname, const char *name, DWORD style, INT x, INT y, INT cx, INT cy,
               HWND hparent, HMENU menu, HINSTANCE inst, void *param, int dialog)
{
    ui_controls_init();
    const struct ui_class *c = ui_find_class(clsname, inst);
    if (!c) {
        ui_log("CreateWindowExA: unknown class %s", IS_INTRESOURCE(clsname) ? "(atom)" : clsname);
        return 0;
    }
    ui_win *parent = hparent ? ui_get(hparent) : NULL;
    if (hparent && !parent) { ui_log("CreateWindowExA: bad parent %08x", hparent); return 0; }
    ui_win *owner = NULL;
    if (!(style & WS_CHILD)) { owner = parent ? ui_toplevel(parent) : NULL; parent = NULL; }
    else if (!parent) { ui_log("CreateWindowExA: child with no parent"); return 0; }
    if (!parent) parent = ui_desktop();
    /* CW_USEDEFAULT */
    if (x == (INT)0x80000000) { x = 0; y = 0; }
    if (cx == (INT)0x80000000) { cx = 640; cy = 480; }
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    ui_win *w = alloc_win();
    w->cls = c; w->style = style & ~WS_VISIBLE; w->exstyle = ex; w->owner = owner; w->inst = inst;
    if ((style & (WS_CHILD | WS_POPUP)) == 0) w->style |= WS_CAPTION | WS_CLIPSIBLINGS;  /* overlapped */
    if (style & WS_CHILD) w->id = menu;
    w->proc = c->proc; w->text = ui_strdup(IS_INTRESOURCE(name) ? "" : name);
    w->tid = 0;
    w->n_extra = c->wnd_extra; w->extra = calloc(1, (size_t)c->wnd_extra + 4);
    w->is_dialog = dialog;
    link_win(w, parent, (style & WS_CHILD) ? (ui_win *)1 : NULL);
    w->wr.left = x; w->wr.top = y; w->wr.right = x + cx; w->wr.bottom = y + cy;
    w->cr = w->wr;
    ui_trace("create %08x class=%s style=%08x ex=%08x %d,%d %dx%d parent=%08x id=%u", w->h, c->name, style, ex, x, y, cx, cy,
             parent->h, w->id);
    CREATESTRUCTA cs = { param, inst, menu, hparent, cy, cx, y, x, (LONG)style, name, clsname, ex };
    if (!(style & WS_CHILD)) {
        void *sdl = ui_sdl_create(w);
        if (sdl) { w->sdl = sdl; HWND nat = ui_sdl_native(sdl); if (nat) { extern void ui_set_alias(ui_win *, HWND); ui_set_alias(w, nat); } }
    }
    if (!ui_send(w, WM_NCCREATE, 0, (LPARAM)&cs)) { ui_log("create %08x aborted by WM_NCCREATE", w->h); ui_destroy_window(w); return 0; }
    {
        RECT r = w->wr; POINT o; origin(parent, &o);
        r.left += o.x; r.right += o.x; r.top += o.y; r.bottom += o.y;
        ui_send(w, WM_NCCALCSIZE, 0, (LPARAM)&r);
        calc_client(w);
    }
    if (ui_send(w, WM_CREATE, 0, (LPARAM)&cs) == -1) { ui_log("create %08x aborted by WM_CREATE", w->h); ui_destroy_window(w); return 0; }
    if (w->destroyed) return 0;
    ui_send(w, WM_SIZE, 0, MAKELONG(w->cr.right - w->cr.left, w->cr.bottom - w->cr.top));
    ui_send(w, WM_MOVE, 0, MAKELONG(w->cr.left, w->cr.top));
    send_parent_notify(w, WM_CREATE);
    if (w->destroyed) return 0;
    if (style & WS_VISIBLE) ui_ShowWindow(w->h, SW_SHOW);
    return w->h;
}
HWND UIAPI ui_CreateWindowExA(DWORD ex, const char *cls, const char *name, DWORD style, INT x, INT y, INT cx, INT cy,
                              HWND parent, HMENU menu, HINSTANCE inst, void *param)
{
    return ui_create(ex, cls, name, style, x, y, cx, cy, parent, menu, inst, param, 0);
}

/* ---- destruction ---- */
void ui_timers_kill_window(ui_win *w);
void ui_input_forget(ui_win *w);
static void send_destroy(ui_win *w)
{
    ui_send(w, WM_DESTROY, 0, 0);
    for (ui_win *c = w->child; c; c = c->next) if (!c->destroyed) send_destroy(c);
}
static void free_tree(ui_win *w)
{
    while (w->child) { ui_win *c = w->child; unlink_win(c); free_tree(c); }
    ui_send(w, WM_NCDESTROY, 0, 0);
    w->destroyed = 1;
    ui_timers_kill_window(w);
    ui_remove_window_messages(w);
    void ui_sdl_destroy(ui_win *w);
    if (w->sdl) ui_sdl_destroy(w);
    ui_controls_free(w);
    free_win(w);
}
void ui_destroy_window(ui_win *w)
{
    if (!w || w->destroyed || w->in_destroy) return;
    w->in_destroy = 1;
    ui_trace("destroy %08x", w->h);
    send_parent_notify(w, WM_DESTROY);
    if (w->style & WS_VISIBLE) {
        if (w->style & WS_CHILD) ui_ShowWindow(w->h, SW_HIDE);
        else ui_SetWindowPos(w->h, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    /* owned popups go first */
    for (int again = 1; again;) {
        again = 0;
        for (ui_win *c = ui_desktop()->child; c; c = c->next)
            if (c->owner == w && !c->in_destroy) { ui_destroy_window(c); again = 1; break; }
    }
    ui_input_forget(w);
    send_destroy(w);
    for (ui_win *c = w->child; c; c = c->next) ui_input_forget(c);
    unlink_win(w);
    free_tree(w);
}
BOOL UIAPI ui_DestroyWindow(HWND h)
{
    ui_win *w = ui_get_logged(h, "DestroyWindow");
    if (!w) return 0;
    ui_destroy_window(w);
    return 1;
}

/* ---- position ---- */
BOOL UIAPI ui_SetWindowPos(HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags)
{
    ui_win *w = ui_get_logged(h, "SetWindowPos");
    if (!w) return 0;
    WINDOWPOS wp = { h, after, x, y, cx, cy, flags };
    if (w->style & WS_VISIBLE) wp.flags &= ~SWP_SHOWWINDOW; else wp.flags &= ~SWP_HIDEWINDOW;
    if (wp.flags & SWP_NOMOVE) { wp.x = w->wr.left; wp.y = w->wr.top; }
    if (wp.flags & SWP_NOSIZE) { wp.cx = w->wr.right - w->wr.left; wp.cy = w->wr.bottom - w->wr.top; }
    if (wp.cx < 0) wp.cx = 0;
    if (wp.cy < 0) wp.cy = 0;
    if (wp.x == w->wr.left && wp.y == w->wr.top) wp.flags |= SWP_NOMOVE;
    if (wp.cx == w->wr.right - w->wr.left && wp.cy == w->wr.bottom - w->wr.top) wp.flags |= SWP_NOSIZE;
    if (!(wp.flags & SWP_NOSENDCHANGING)) ui_send(w, WM_WINDOWPOSCHANGING, 0, (LPARAM)&wp);
    RECT old = w->wr, oldc = w->cr; int was_vis = ui_is_visible(w);
    if (!(wp.flags & SWP_NOZORDER) && w->parent) {
        ui_win *a = NULL;
        if (wp.hwndInsertAfter == HWND_BOTTOM) a = (ui_win *)1;
        else if (wp.hwndInsertAfter == HWND_TOP || wp.hwndInsertAfter == HWND_TOPMOST || wp.hwndInsertAfter == HWND_NOTOPMOST) a = NULL;
        else { a = ui_get(wp.hwndInsertAfter); if (a == w || (a && a->parent != w->parent)) a = NULL; }
        ui_win *p = w->parent;
        unlink_win(w); link_win(w, p, a);
    }
    if (!(wp.flags & SWP_NOMOVE) || !(wp.flags & SWP_NOSIZE) || (wp.flags & SWP_FRAMECHANGED)) {
        w->wr.left = wp.x; w->wr.top = wp.y; w->wr.right = wp.x + wp.cx; w->wr.bottom = wp.y + wp.cy;
        if (wp.flags & SWP_FRAMECHANGED) {
            NCCALCSIZE_PARAMS nc; nc.rgrc[0] = w->wr; nc.rgrc[1] = old; nc.rgrc[2] = w->cr; nc.lppos = &wp;
            ui_send(w, WM_NCCALCSIZE, 1, (LPARAM)&nc);
        }
        calc_client(w);
    }
    if (w->cr.left == oldc.left && w->cr.top == oldc.top) wp.flags |= 0x1000;            /* SWP_NOCLIENTMOVE */
    if (w->cr.right - w->cr.left == oldc.right - oldc.left && w->cr.bottom - w->cr.top == oldc.bottom - oldc.top)
        wp.flags |= 0x800;                                                                /* SWP_NOCLIENTSIZE */
    if (wp.flags & SWP_SHOWWINDOW) w->style |= WS_VISIBLE;
    if (wp.flags & SWP_HIDEWINDOW) w->style &= ~WS_VISIBLE;
    if (w->sdl) ui_sdl_sync(w);
    int vis = ui_is_visible(w);
    if (vis && !(wp.flags & SWP_NOREDRAW) && (!was_vis || !(wp.flags & SWP_NOSIZE) || !(wp.flags & SWP_NOMOVE) || (wp.flags & SWP_FRAMECHANGED)))
        ui_invalidate(w, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    if (was_vis && (!vis || !(wp.flags & SWP_NOMOVE) || !(wp.flags & SWP_NOSIZE)) && w->parent && w->parent != ui_desktop() && !(wp.flags & SWP_NOREDRAW)) {
        ui_invalidate(w->parent, &old, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);   /* expose the old area */
    }
    if ((wp.flags & SWP_HIDEWINDOW) && was_vis) {
        extern void ui_hidden(ui_win *w); ui_hidden(w);
    }
    ui_send(w, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);
    return 1;
}
BOOL UIAPI ui_MoveWindow(HWND h, INT x, INT y, INT cx, INT cy, BOOL repaint)
{
    return ui_SetWindowPos(h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}
BOOL UIAPI ui_BringWindowToTop(HWND h) { return ui_SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }
BOOL UIAPI ui_GetWindowRect(HWND h, RECT *r)
{
    ui_win *w = ui_get_logged(h, "GetWindowRect");
    if (!w || !r) return 0;
    *r = w->wr;
    POINT o; origin(w->parent, &o);
    r->left += o.x; r->right += o.x; r->top += o.y; r->bottom += o.y;
    return 1;
}
BOOL UIAPI ui_GetClientRect(HWND h, RECT *r)
{
    ui_win *w = ui_get_logged(h, "GetClientRect");
    if (!w || !r) return 0;
    r->left = r->top = 0; r->right = w->cr.right - w->cr.left; r->bottom = w->cr.bottom - w->cr.top;
    return 1;
}
BOOL UIAPI ui_ClientToScreen(HWND h, POINT *p)
{
    ui_win *w = ui_get_logged(h, "ClientToScreen");
    if (!w || !p) return 0;
    ui_client_to_screen(w, p); return 1;
}
BOOL UIAPI ui_ScreenToClient(HWND h, POINT *p)
{
    ui_win *w = ui_get_logged(h, "ScreenToClient");
    if (!w || !p) return 0;
    ui_screen_to_client(w, p); return 1;
}

/* ---- show ---- */
BOOL UIAPI ui_ShowWindow(HWND h, INT cmd)
{
    ui_win *w = ui_get_logged(h, "ShowWindow");
    if (!w) return 0;
    int was = (w->style & WS_VISIBLE) != 0;
    int show = cmd != SW_HIDE;
    UINT flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER;
    switch (cmd) {
    case SW_HIDE: if (!was) return 0; flags |= SWP_HIDEWINDOW | SWP_NOACTIVATE; break;
    case SW_SHOWNA: case SW_SHOWNOACTIVATE: case SW_SHOWMINNOACTIVE: flags |= SWP_SHOWWINDOW | SWP_NOACTIVATE; break;
    case SW_MINIMIZE: case SW_SHOWMINIMIZED: flags |= SWP_SHOWWINDOW | SWP_NOACTIVATE; break;
    default: flags |= SWP_SHOWWINDOW; if (w->style & WS_CHILD) flags |= SWP_NOACTIVATE; break;
    }
    if (show == was && cmd != SW_SHOWNORMAL && cmd != SW_SHOW && cmd != SW_RESTORE && cmd != SW_SHOWDEFAULT) return was;
    if (show != was) ui_send(w, WM_SHOWWINDOW, show, 0);
    if (w->destroyed) return was;
    if (show == was) flags &= ~(SWP_SHOWWINDOW | SWP_HIDEWINDOW);
    ui_SetWindowPos(h, 0, 0, 0, 0, 0, flags);
    if (w->destroyed) return was;
    if (show && !was) {
        if (!(flags & SWP_NOACTIVATE)) ui_activate(w, WA_ACTIVE);
        ui_window_shown(w);
    }
    return was;
}
BOOL UIAPI ui_IsWindowVisible(HWND h) { ui_win *w = ui_get(h); return w && ui_is_visible(w); }
BOOL UIAPI ui_CloseWindow(HWND h) { ui_log("CloseWindow(%08x): minimize ignored", h); return ui_get(h) != NULL; }
BOOL UIAPI ui_IsWindowEnabled(HWND h) { ui_win *w = ui_get(h); return w && !(w->style & WS_DISABLED); }
BOOL UIAPI ui_EnableWindow(HWND h, BOOL on)
{
    ui_win *w = ui_get_logged(h, "EnableWindow");
    if (!w) return 0;
    BOOL was_disabled = (w->style & WS_DISABLED) != 0;
    if (on && was_disabled) {
        w->style &= ~WS_DISABLED;
        ui_send(w, WM_ENABLE, 1, 0);
    } else if (!on && !was_disabled) {
        ui_send(w, WM_CANCELMODE, 0, 0);
        w->style |= WS_DISABLED;
        if (ui_focus() == w || ui_is_child_of(ui_focus(), w)) ui_set_focus(NULL);
        if (ui_capture() == w) ui_ReleaseCapture();
        ui_send(w, WM_ENABLE, 0, 0);
    }
    return was_disabled;
}

/* ---- long values ---- */
LONG UIAPI ui_GetWindowLongA(HWND h, INT i)
{
    ui_win *w = ui_get_logged(h, "GetWindowLongA");
    if (!w) return 0;
    switch (i) {
    case GWL_WNDPROC: return (LONG)(uintptr_t)w->proc;
    case GWL_HINSTANCE: return (LONG)w->inst;
    case GWL_HWNDPARENT: return (LONG)((w->style & WS_CHILD) ? ui_handle(w->parent) : ui_handle(w->owner));
    case GWL_STYLE: return (LONG)w->style;
    case GWL_EXSTYLE: return (LONG)w->exstyle;
    case GWL_USERDATA: return w->userdata;
    case GWL_ID: return (LONG)w->id;
    }
    if (i >= 0 && i + 4 <= w->n_extra) { LONG v; memcpy(&v, w->extra + i, 4); return v; }
    ui_trace("GetWindowLongA(%08x, %d): bad index", h, i);
    return 0;
}
LONG UIAPI ui_SetWindowLongA(HWND h, INT i, LONG v)
{
    ui_win *w = ui_get_logged(h, "SetWindowLongA");
    if (!w) return 0;
    LONG old;
    switch (i) {
    case GWL_WNDPROC: old = (LONG)(uintptr_t)w->proc; w->proc = (WNDPROC)(uintptr_t)v; return old;
    case GWL_USERDATA: old = w->userdata; w->userdata = v; return old;
    case GWL_ID: old = (LONG)w->id; w->id = (UINT)v; return old;
    case GWL_HINSTANCE: old = (LONG)w->inst; w->inst = (HINSTANCE)v; return old;
    case GWL_STYLE: case GWL_EXSTYLE: {
        struct { DWORD old, new_; } ss = { i == GWL_STYLE ? w->style : w->exstyle, (DWORD)v };
        ui_send(w, WM_STYLECHANGING, (WPARAM)i, (LPARAM)&ss);
        old = (LONG)ss.old;
        if (i == GWL_STYLE) { DWORD keep = w->style & WS_VISIBLE; w->style = (ss.new_ & ~WS_VISIBLE) | keep; }
        else w->exstyle = ss.new_;
        ui_send(w, WM_STYLECHANGED, (WPARAM)i, (LPARAM)&ss);
        if (i == GWL_STYLE && ((DWORD)old ^ ss.new_) & WS_VISIBLE) {
            if (ss.new_ & WS_VISIBLE) w->style |= WS_VISIBLE; else w->style &= ~WS_VISIBLE;
        }
        return old;
    }
    case GWL_HWNDPARENT: ui_log("SetWindowLongA GWL_HWNDPARENT unsupported"); return 0;
    }
    if (i >= 0 && i + 4 <= w->n_extra) {
        memcpy(&old, w->extra + i, 4); memcpy(w->extra + i, &v, 4);
        if (w->is_dialog && i == DWL_DLGPROC) { /* nothing more */ }
        return old;
    }
    ui_log("SetWindowLongA(%08x, %d): bad index", h, i);
    return 0;
}

/* ---- text ---- */
INT UIAPI ui_GetWindowTextA(HWND h, char *buf, INT n)
{
    if (!buf || n <= 0) return 0;
    buf[0] = 0;
    return (INT)ui_SendMessageA(h, WM_GETTEXT, (WPARAM)n, (LPARAM)buf);
}

/* ---- tree queries ---- */
HWND UIAPI ui_GetParent(HWND h)
{
    ui_win *w = ui_get_logged(h, "GetParent");
    if (!w) return 0;
    if (w->style & WS_CHILD) return w->parent == ui_desktop() ? 0 : ui_handle(w->parent);
    if (w->style & WS_POPUP) return ui_handle(w->owner);
    return 0;
}
HWND UIAPI ui_GetWindow(HWND h, UINT cmd)
{
    ui_win *w = ui_get_logged(h, "GetWindow");
    if (!w) return 0;
    switch (cmd) {
    case GW_HWNDFIRST: return w->parent ? ui_handle(w->parent->child) : 0;
    case GW_HWNDLAST: { ui_win *c = w->parent ? w->parent->child : NULL; while (c && c->next) c = c->next; return ui_handle(c); }
    case GW_HWNDNEXT: return ui_handle(w->next);
    case GW_HWNDPREV: return ui_handle(w->prev);
    case GW_OWNER: return ui_handle(w->owner);
    case GW_CHILD: return ui_handle(w->child);
    }
    return 0;
}
HWND UIAPI ui_GetTopWindow(HWND h)
{
    ui_win *w = h ? ui_get_logged(h, "GetTopWindow") : ui_desktop();
    return w ? ui_handle(w->child) : 0;
}
static int enum_children(ui_win *w, WNDENUMPROC f, LPARAM l)
{
    /* collect first: the callback may destroy windows */
    int n = 0; for (ui_win *c = w->child; c; c = c->next) n++;
    if (!n) return 1;
    HWND *list = malloc(sizeof(HWND) * (size_t)n); int k = 0;
    for (ui_win *c = w->child; c; c = c->next) list[k++] = c->h;
    int go = 1;
    for (int i = 0; i < n && go; i++) {
        ui_win *c = ui_get(list[i]);
        if (!c) continue;
        if (!f(list[i], l)) { go = 0; break; }
        c = ui_get(list[i]);
        if (c && !enum_children(c, f, l)) go = 0;
    }
    free(list);
    return go;
}
BOOL UIAPI ui_EnumChildWindows(HWND h, WNDENUMPROC f, LPARAM l)
{
    ui_win *w = h ? ui_get_logged(h, "EnumChildWindows") : ui_desktop();
    if (!w || !f) return 0;
    enum_children(w, f, l);
    return 1;
}
HWND UIAPI ui_FindWindowA(const char *cls, const char *title)
{
    for (ui_win *w = ui_desktop()->child; w; w = w->next) {
        if (cls && !IS_INTRESOURCE(cls) && ui_stricmp(w->cls->name, cls)) continue;
        if (title && strcmp(w->text ? w->text : "", title)) continue;
        return w->h;
    }
    return 0;
}
static int hit_test(ui_win *w, POINT screen)
{
    return (int)ui_send(w, WM_NCHITTEST, 0, MAKELONG(screen.x, screen.y));
}
/* deepest visible child under the screen point; sends WM_NCHITTEST like Wine's window_from_point */
ui_win *ui_window_from_point(ui_win *root, POINT p, int *hit)
{
    ui_win *res = NULL; int ht = HTNOWHERE;
    for (ui_win *w = root->child; w; w = w->next) {
        if (!(w->style & WS_VISIBLE)) continue;
        RECT r; ui_GetWindowRect(w->h, &r);
        if (p.x < r.left || p.x >= r.right || p.y < r.top || p.y >= r.bottom) continue;
        /* descend into enabled windows */
        ui_win *cur = w;
        for (;;) {
            if (cur->style & WS_DISABLED) break;
            POINT cp = p; ui_screen_to_client(cur, &cp);
            if (cp.x < 0 || cp.y < 0 || cp.x >= cur->cr.right - cur->cr.left || cp.y >= cur->cr.bottom - cur->cr.top) break;
            ui_win *found = NULL;
            for (ui_win *c = cur->child; c; c = c->next) {
                if (!(c->style & WS_VISIBLE)) continue;
                RECT cr; ui_GetWindowRect(c->h, &cr);
                if (p.x >= cr.left && p.x < cr.right && p.y >= cr.top && p.y < cr.bottom) { found = c; break; }
            }
            if (!found) break;
            /* transparent children: try siblings below via hit test */
            cur = found;
        }
        /* walk up while the window answers HTTRANSPARENT */
        for (ui_win *x = cur; x; ) {
            ht = hit_test(x, p);
            if (ht != HTTRANSPARENT) { res = x; break; }
            /* try a sibling below the transparent one */
            ui_win *sib = NULL;
            for (ui_win *s = x->next; s; s = s->next) {
                if (!(s->style & WS_VISIBLE)) continue;
                RECT sr; ui_GetWindowRect(s->h, &sr);
                if (p.x >= sr.left && p.x < sr.right && p.y >= sr.top && p.y < sr.bottom) { sib = s; break; }
            }
            if (sib && hit_test(sib, p) != HTTRANSPARENT) { res = sib; ht = hit_test(sib, p); break; }
            if (x == w) { res = w; break; }
            x = x->parent;
        }
        break;
    }
    if (hit) *hit = ht;
    return res;
}
HWND UIAPI ui_WindowFromPoint(LONG x, LONG y)
{
    POINT p = { x, y };
    ui_win *w = ui_window_from_point(ui_desktop(), p, NULL);
    return ui_handle(w);
}
HWND UIAPI ui_ChildWindowFromPointEx(HWND h, LONG x, LONG y, UINT flags)
{
    ui_win *w = ui_get_logged(h, "ChildWindowFromPointEx");
    if (!w) return 0;
    if (x < 0 || y < 0 || x >= w->cr.right - w->cr.left || y >= w->cr.bottom - w->cr.top) return 0;
    for (ui_win *c = w->child; c; c = c->next) {
        if ((flags & 1) && !(c->style & WS_VISIBLE)) continue;       /* CWP_SKIPINVISIBLE */
        if ((flags & 2) && (c->style & WS_DISABLED)) continue;       /* CWP_SKIPDISABLED */
        if ((flags & 4) && (c->exstyle & WS_EX_TRANSPARENT)) continue;
        if (x >= c->wr.left && x < c->wr.right && y >= c->wr.top && y < c->wr.bottom) return c->h;
    }
    return h;
}
HWND UIAPI ui_ChildWindowFromPoint(HWND h, LONG x, LONG y) { return ui_ChildWindowFromPointEx(h, x, y, 0); }
HMENU UIAPI ui_GetMenu(HWND h) { (void)h; return 0; }
DWORD UIAPI ui_GetWindowContextHelpId(HWND h) { (void)h; return 0; }
BOOL UIAPI ui_LockWindowUpdate(HWND h) { ui_trace("LockWindowUpdate %08x", h); return 1; }

/* ---- rectangles ---- */
BOOL UIAPI ui_SetRect(RECT *r, INT l, INT t, INT ri, INT b)
{
    if (!r) return 0;
    r->left = l; r->top = t; r->right = ri; r->bottom = b; return 1;
}
BOOL UIAPI ui_IntersectRect(RECT *d, const RECT *a, const RECT *b)
{
    if (!d || !a || !b) return 0;
    if (a->left >= a->right || a->top >= a->bottom || b->left >= b->right || b->top >= b->bottom ||
        a->left >= b->right || b->left >= a->right || a->top >= b->bottom || b->top >= a->bottom) {
        d->left = d->top = d->right = d->bottom = 0; return 0;
    }
    d->left = UI_MAX(a->left, b->left); d->right = UI_MIN(a->right, b->right);
    d->top = UI_MAX(a->top, b->top); d->bottom = UI_MIN(a->bottom, b->bottom);
    return 1;
}
