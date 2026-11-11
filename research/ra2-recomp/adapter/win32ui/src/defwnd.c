/* defwnd.c: DefWindowProcA, GDI device contexts (no drawing), message boxes, help. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LRESULT nc_hit(ui_win *w, LPARAM lp)
{
    POINT p = { (int16_t)LOWORD(lp), (int16_t)HIWORD(lp) };
    RECT wr; ui_GetWindowRect(w->h, &wr);
    if (p.x < wr.left || p.y < wr.top || p.x >= wr.right || p.y >= wr.bottom) return HTNOWHERE;
    POINT c = p; ui_screen_to_client(w, &c);
    if (c.x >= 0 && c.y >= 0 && c.x < w->cr.right - w->cr.left && c.y < w->cr.bottom - w->cr.top) return HTCLIENT;
    if ((w->style & WS_CAPTION) == WS_CAPTION && c.y < 0) return HTCAPTION;
    return 18; /* HTBORDER */
}
LRESULT UIAPI ui_DefWindowProcA(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    ui_win *w = ui_get(h);
    if (!w) return 0;
    switch (m) {
    case WM_NCCREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)(uintptr_t)lp;
        if (cs && cs->lpszName && !IS_INTRESOURCE(cs->lpszName)) { free(w->text); w->text = ui_strdup(cs->lpszName); }
        return 1;
    }
    case WM_NCCALCSIZE: return 0;
    case WM_NCHITTEST: return nc_hit(w, lp);
    case WM_NCACTIVATE: return 1;
    case WM_NCPAINT: return 0;
    case WM_SETTEXT:
        free(w->text); w->text = ui_strdup(lp ? (const char *)(uintptr_t)lp : "");
        return 1;
    case WM_GETTEXT: {
        char *buf = (char *)(uintptr_t)lp;
        if (!buf || !wp) return 0;
        size_t n = strlen(w->text ? w->text : "");
        if (n > wp - 1) n = wp - 1;
        memcpy(buf, w->text ? w->text : "", n); buf[n] = 0;
        return (LRESULT)n;
    }
    case WM_GETTEXTLENGTH: return (LRESULT)strlen(w->text ? w->text : "");
    case WM_PAINT: { PAINTSTRUCT ps; ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return w->cls && w->cls->bg ? 1 : 0;
    case WM_CLOSE: ui_DestroyWindow(h); return 0;
    case WM_SETCURSOR:
        if ((w->style & WS_CHILD) && w->parent && w->parent != ui_desktop())
            if (ui_send(w->parent, WM_SETCURSOR, wp, lp)) return 1;
        if (LOWORD(lp) == HTCLIENT && w->cls && w->cls->cursor) { ui_SetCursor(w->cls->cursor); return 1; }
        return 0;
    case WM_MOUSEACTIVATE:
        if ((w->style & WS_CHILD) && w->parent && w->parent != ui_desktop()) {
            LRESULT r = ui_send(w->parent, WM_MOUSEACTIVATE, wp, lp);
            if (r) return r;
        }
        return MA_ACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && !(w->style & WS_MINIMIZE)) ui_SetFocus(h);
        return 0;
    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS *p = (WINDOWPOS *)(uintptr_t)lp;
        if (!(p->flags & 0x1000 /* SWP_NOCLIENTMOVE */)) ui_send(w, WM_MOVE, 0, MAKELONG(w->cr.left, w->cr.top));
        if (!(p->flags & 0x800 /* SWP_NOCLIENTSIZE */) || (p->flags & SWP_STATECHANGED))
            ui_send(w, WM_SIZE, 0, MAKELONG(w->cr.right - w->cr.left, w->cr.bottom - w->cr.top));
        return 0;
    }
    case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC:
        return (LRESULT)ui_GetStockObject(0);   /* WHITE_BRUSH */
    case WM_SYSKEYDOWN:
        if (wp == 0x73 && (lp & (1 << 29))) ui_PostMessageA(h, WM_SYSCOMMAND, SC_CLOSE, 0);   /* Alt+F4 */
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_CLOSE) ui_send(w, WM_CLOSE, 0, 0);
        return 0;
    case WM_CANCELMODE:
        if (ui_capture() == w) ui_ReleaseCapture();
        return 0;
    case WM_GETFONT: return 0;
    case WM_QUERYUISTATE: return 0;
    case WM_GETMINMAXINFO: return 0;
    }
    return 0;
}

/* ---- device contexts: handles with state, no pixels ---- */
typedef struct { int used; HWND h; DWORD text, bk; int bkmode, gmode; HGDIOBJ font; POINT worg, vorg; } ui_dc;
static ui_dc dcs[64];
static ui_dc *get_dc(HDC dc)
{
    if ((dc & 0xffff0000u) != 0x00d10000u) return NULL;
    unsigned i = dc & 0xffff;
    return i < 64 && dcs[i].used ? &dcs[i] : NULL;
}
HDC UIAPI ui_GetDC(HWND h)
{
    for (unsigned i = 1; i < 64; i++) if (!dcs[i].used) {
        memset(&dcs[i], 0, sizeof dcs[i]);
        dcs[i].used = 1; dcs[i].h = h; dcs[i].text = 0; dcs[i].bk = 0xffffff; dcs[i].bkmode = 2; dcs[i].gmode = 1; dcs[i].font = 0x00f0000du;
        return 0x00d10000u | i;
    }
    ui_fatal("win32ui: out of device contexts");
}
INT UIAPI ui_ReleaseDC(HWND h, HDC dc) { (void)h; ui_dc *d = get_dc(dc); if (!d) return 0; d->used = 0; return 1; }
static void gdi_bad(const char *fn, HDC dc) { ui_fatal("win32ui: %s on unknown device context %08x (caller DC from DirectDraw?)", fn, dc); }
#define DC_OR_DIE(fn) ui_dc *d = get_dc(dc); if (!d) gdi_bad(fn, dc)
DWORD UIAPI ui_SetTextColor(HDC dc, DWORD c) { DC_OR_DIE("SetTextColor"); DWORD o = d->text; d->text = c; return o; }
DWORD UIAPI ui_GetTextColor(HDC dc) { DC_OR_DIE("GetTextColor"); return d->text; }
DWORD UIAPI ui_SetBkColor(HDC dc, DWORD c) { DC_OR_DIE("SetBkColor"); DWORD o = d->bk; d->bk = c; return o; }
DWORD UIAPI ui_GetBkColor(HDC dc) { DC_OR_DIE("GetBkColor"); return d->bk; }
INT UIAPI ui_SetBkMode(HDC dc, INT m) { DC_OR_DIE("SetBkMode"); INT o = d->bkmode; d->bkmode = m; return o; }
INT UIAPI ui_GetBkMode(HDC dc) { DC_OR_DIE("GetBkMode"); return d->bkmode; }
INT UIAPI ui_SetGraphicsMode(HDC dc, INT m) { DC_OR_DIE("SetGraphicsMode"); INT o = d->gmode; d->gmode = m; return o; }
HGDIOBJ UIAPI ui_SelectObject(HDC dc, HGDIOBJ o)
{
    DC_OR_DIE("SelectObject");
    unsigned k = o & 0xffff;
    int is_font = (o & 0xffff0000u) == 0x00e00000u || ((o & 0xffff0000u) == 0x00f00000u && k >= 10 && k <= 17 && k != 15);
    if (!is_font) return 0x00f00000u;   /* brushes, pens, bitmaps: not tracked */
    HGDIOBJ old = d->font; d->font = o; return old;
}
BOOL UIAPI ui_TextOutA(HDC dc, INT x, INT y, const char *s, INT n)
{
    DC_OR_DIE("TextOutA");
    ui_log("TextOutA(%d,%d,\"%.*s\"): not drawn", x, y, n, s ? s : "");
    return 1;
}
BOOL UIAPI ui_DPtoLP(HDC dc, POINT *p, INT n) { DC_OR_DIE("DPtoLP"); (void)p; (void)n; return 1; }
BOOL UIAPI ui_SetWindowOrgEx(HDC dc, INT x, INT y, POINT *old) { DC_OR_DIE("SetWindowOrgEx"); if (old) *old = d->worg; d->worg.x = x; d->worg.y = y; return 1; }
BOOL UIAPI ui_SetViewportOrgEx(HDC dc, INT x, INT y, POINT *old) { DC_OR_DIE("SetViewportOrgEx"); if (old) *old = d->vorg; d->vorg.x = x; d->vorg.y = y; return 1; }
INT UIAPI ui_SaveDC(HDC dc) { DC_OR_DIE("SaveDC"); return 1; }
BOOL UIAPI ui_RestoreDC(HDC dc, INT i) { DC_OR_DIE("RestoreDC"); (void)i; return 1; }
BOOL UIAPI ui_ModifyWorldTransform(HDC dc, const void *x, DWORD m) { DC_OR_DIE("ModifyWorldTransform"); (void)x; (void)m; return 1; }
#include "wine_metrics.inc"
INT UIAPI ui_GetDeviceCaps(HDC dc, INT i)
{
    (void)dc;
    if (i == 8) return ui_GetSystemMetrics(SM_CXSCREEN);    /* HORZRES */
    if (i == 10) return ui_GetSystemMetrics(SM_CYSCREEN);   /* VERTRES */
    if (i >= 0 && i < 120) return wine_caps[i];
    ui_log("GetDeviceCaps(%d): 0", i);
    return 0;
}
/* ---- GDI objects: fonts keep their LOGFONT values; metrics come from the Wine table ---- */
typedef struct { int used; int height, weight; char face[32]; } ui_font;
static ui_font fonts[512];
static DWORD next_obj = 0x00e10000;
HFONT UIAPI ui_CreateFontIndirectA(const void *lf)
{
    for (unsigned i = 1; i < 512; i++) if (!fonts[i].used) {
        ui_font *f = &fonts[i]; memset(f, 0, sizeof *f); f->used = 1; f->weight = 400;
        if (lf) {
            const BYTE *b = lf; int32_t h, w; memcpy(&h, b, 4); memcpy(&w, b + 16, 4);
            f->height = h; if (w) f->weight = w;
            memcpy(f->face, b + 28, 31); f->face[31] = 0;
        }
        return 0x00e00000u | i;
    }
    ui_fatal("win32ui: out of font objects");
}
HFONT ui_create_font(int height, int weight, const char *face)
{
    BYTE lf[60] = {0}; int32_t h = height, w = weight;
    memcpy(lf, &h, 4); memcpy(lf + 16, &w, 4);
    snprintf((char *)lf + 28, 32, "%s", face ? face : "");
    return ui_CreateFontIndirectA(lf);
}
/* the 20 TEXTMETRIC values of a font handle (stock object, our font, or 0 = system font) */
void ui_font_tm(HGDIOBJ obj, int tm[20])
{
    char stock[12] = "#13"; const char *face = stock; int h = 0, w = 0;
    if ((obj & 0xffff0000u) == 0x00f00000u) snprintf(stock, sizeof stock, "#%u", obj & 0xffff);
    else if ((obj & 0xffff0000u) == 0x00e00000u && (obj & 0xffff) < 512 && fonts[obj & 0xffff].used) {
        ui_font *f = &fonts[obj & 0xffff]; face = f->face; h = f->height; w = f->weight;
    }
    int best = -1, bestscore = 1 << 30, n = (int)(sizeof wine_fonts / sizeof wine_fonts[0]);
    for (int pass = 0; pass < 2 && best < 0; pass++)
        for (int i = 0; i < n; i++) {
            if (ui_stricmp(wine_fonts[i].face, pass ? "" : face)) continue;
            if (face[0] == '#') { best = i; break; }
            int want = h ? h : -11;
            int dh = wine_fonts[i].h - want; if (dh < 0) dh = -dh;
            int dw = wine_fonts[i].w - (w >= 550 ? 700 : 400); if (dw < 0) dw = -dw;
            int sc = dh * 1000 + dw;
            if ((wine_fonts[i].h < 0) != (want < 0)) sc += 100000;
            if (sc < bestscore) { bestscore = sc; best = i; }
        }
    if (best < 0) best = 0;
    for (int k = 0; k < 20; k++) tm[k] = wine_fonts[best].tm[k];
}
HGDIOBJ ui_dc_font(HDC dc) { ui_dc *d = get_dc(dc); return d ? d->font : 0; }
BOOL UIAPI ui_GetTextMetricsA(HDC dc, void *out)
{
    DC_OR_DIE("GetTextMetricsA");
    int tm[20]; ui_font_tm(d->font, tm);
    BYTE *b = out; memset(b, 0, 56);
    for (int i = 0; i < 11; i++) { int32_t v = tm[i]; memcpy(b + 4 * i, &v, 4); }
    for (int i = 0; i < 9; i++) b[44 + i] = (BYTE)tm[11 + i];
    return 1;
}
HBRUSH UIAPI ui_CreateSolidBrush(DWORD c) { (void)c; return next_obj++; }
BOOL UIAPI ui_DeleteObject(HGDIOBJ o)
{
    if ((o & 0xffff0000u) == 0x00e00000u && (o & 0xffff) < 512) fonts[o & 0xffff].used = 0;
    return o != 0;
}
HGDIOBJ UIAPI ui_GetStockObject(INT i) { return 0x00f00000u + (DWORD)i; }
HBITMAP UIAPI ui_CreateDIBSection(HDC dc, const void *bmi, UINT usage, void **bits, uint32_t sec, DWORD off)
{
    (void)dc; (void)bmi; (void)usage; (void)bits; (void)sec; (void)off;
    UI_UNIMPL("CreateDIBSection");
}
DWORD UIAPI ui_GetSysColor(INT i) { return i >= 0 && i < 31 ? wine_color[i] : 0; }

/* ---- message boxes: logged, answered without a window ---- */
static INT answer(UINT type)
{
    const char *a = getenv("RA2UI_MSGBOX_ANSWER");
    if (a && *a) return atoi(a);
    switch (type & 0xf) {
    case 0: case 1: return IDOK;
    case 2: return IDABORT;
    case 3: case 4: return IDYES;
    case 5: return IDRETRY;
    }
    return IDOK;
}
INT UIAPI ui_MessageBoxA(HWND h, const char *text, const char *cap, UINT type)
{
    INT r = answer(type);
    ui_log("MessageBoxA(%08x, \"%s\", \"%s\", %x) -> %d (no window)", h, text ? text : "", cap ? cap : "", type, r);
    return r;
}
INT UIAPI ui_MessageBoxIndirectA(const void *p)
{
    const uint32_t *v = p;   /* MSGBOXPARAMSA: size, owner, inst, text, caption, style, ... */
    return ui_MessageBoxA(v[1], (const char *)(uintptr_t)v[3], IS_INTRESOURCE(v[4]) ? "" : (const char *)(uintptr_t)v[4], v[5]);
}
BOOL UIAPI ui_WinHelpA(HWND h, const char *file, UINT cmd, DWORD data)
{
    ui_log("WinHelpA(%08x, %s, %u, %u): ignored", h, file ? file : "", cmd, data);
    return 1;
}
DWORD UIAPI ui_WaitForInputIdle(uint32_t process, DWORD ms) { (void)process; (void)ms; return 0; }
BOOL UIAPI ui_CharToOemBuffA(const char *s, char *d, DWORD n)
{
    if (!s || !d) return 0;
    if (s != d) memmove(d, s, n);
    return 1;
}

/* ---- comctl32 drag images and IME: not used on the menu to skirmish path ---- */
void UIAPI ui_InitCommonControls(void) { ui_controls_init(); }
BOOL UIAPI ui_ImageList_DragShowNolock(BOOL s) { (void)s; UI_UNIMPL("ImageList_DragShowNolock"); }
BOOL UIAPI ui_ImageList_DragMove(INT x, INT y) { (void)x; (void)y; UI_UNIMPL("ImageList_DragMove"); }
BOOL UIAPI ui_ImageList_DragEnter(HWND h, INT x, INT y) { (void)h; (void)x; (void)y; UI_UNIMPL("ImageList_DragEnter"); }
BOOL UIAPI ui_ImageList_BeginDrag(uint32_t il, INT i, INT x, INT y) { (void)il; (void)i; (void)x; (void)y; UI_UNIMPL("ImageList_BeginDrag"); }
BOOL UIAPI ui_ImageList_Destroy(uint32_t il) { (void)il; UI_UNIMPL("ImageList_Destroy"); }
void UIAPI ui_ImageList_EndDrag(void) { UI_UNIMPL("ImageList_EndDrag"); }
HIMC UIAPI ui_ImmGetContext(HWND h) { (void)h; return 0; }
BOOL UIAPI ui_ImmNotifyIME(HIMC c, DWORD a, DWORD i, DWORD v) { (void)c; (void)a; (void)i; (void)v; return 0; }
LONG UIAPI ui_ImmGetCompositionStringA(HIMC c, DWORD i, void *b, DWORD n) { (void)c; (void)i; (void)b; (void)n; return 0; }
BOOL UIAPI ui_ImmSetOpenStatus(HIMC c, BOOL o) { (void)c; (void)o; return 0; }
DWORD UIAPI ui_ImmGetCandidateListA(HIMC c, DWORD i, void *b, DWORD n) { (void)c; (void)i; (void)b; (void)n; return 0; }
