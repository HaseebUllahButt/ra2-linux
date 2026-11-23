/* ui.h: portable window and dialog code for game.exe (J14).
 * Types follow the 32-bit Windows ABI. The code needs a 32-bit build (pointers are 4 bytes),
 * because the game passes pointers in WPARAM and LPARAM. No windows.h: the same source
 * builds as a Windows DLL (under Wine) and as a Linux library. */
#ifndef RA2UI_H
#define RA2UI_H
#include <stdint.h>
#include <stddef.h>

_Static_assert(sizeof(void *) == 4, "win32ui needs a 32-bit build");

#if defined(_WIN32)
#define UIAPI __stdcall
#define UICDECL __cdecl
#else
#define UIAPI __attribute__((stdcall))
#define UICDECL __attribute__((cdecl))
#endif

typedef uint32_t HWND, HINSTANCE, HMENU, HICON, HCURSOR, HBRUSH, HFONT, HDC, HGDIOBJ, HBITMAP, HIMC, HACCEL, HRGN;
typedef uint32_t UINT, WPARAM, DWORD, BOOL;
typedef int32_t LPARAM, LRESULT, LONG, INT;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int32_t (UIAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef int32_t (UIAPI *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void (UIAPI *TIMERPROC)(HWND, UINT, UINT, DWORD);
typedef BOOL (UIAPI *WNDENUMPROC)(HWND, LPARAM);

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { LONG cx, cy; } SIZE;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct {
    UINT style; WNDPROC lpfnWndProc; INT cbClsExtra, cbWndExtra; HINSTANCE hInstance;
    HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; const char *lpszMenuName, *lpszClassName;
} WNDCLASSA;
typedef struct {
    void *lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent;
    INT cy, cx, y, x; LONG style; const char *lpszName, *lpszClass; DWORD dwExStyle;
} CREATESTRUCTA;
typedef struct { HWND hwnd, hwndInsertAfter; INT x, y, cx, cy; UINT flags; } WINDOWPOS;
typedef struct { RECT rgrc[3]; WINDOWPOS *lppos; } NCCALCSIZE_PARAMS;
typedef struct { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct { HWND hwndFrom; UINT idFrom; UINT code; } NMHDR;

/* window messages */
enum {
    WM_NULL = 0, WM_CREATE = 1, WM_DESTROY = 2, WM_MOVE = 3, WM_SIZE = 5, WM_ACTIVATE = 6, WM_SETFOCUS = 7,
    WM_KILLFOCUS = 8, WM_ENABLE = 0xa, WM_SETREDRAW = 0xb, WM_SETTEXT = 0xc, WM_GETTEXT = 0xd, WM_GETTEXTLENGTH = 0xe,
    WM_PAINT = 0xf, WM_CLOSE = 0x10, WM_QUIT = 0x12, WM_ERASEBKGND = 0x14, WM_SHOWWINDOW = 0x18, WM_ACTIVATEAPP = 0x1c,
    WM_CANCELMODE = 0x1f, WM_SETCURSOR = 0x20, WM_MOUSEACTIVATE = 0x21, WM_GETMINMAXINFO = 0x24,
    WM_NEXTDLGCTL = 0x28, WM_DRAWITEM = 0x2b, WM_MEASUREITEM = 0x2c, WM_DELETEITEM = 0x2d, WM_VKEYTOITEM = 0x2e,
    WM_CHARTOITEM = 0x2f, WM_SETFONT = 0x30, WM_GETFONT = 0x31, WM_COMPAREITEM = 0x39,
    WM_WINDOWPOSCHANGING = 0x46, WM_WINDOWPOSCHANGED = 0x47, WM_NOTIFY = 0x4e, WM_STYLECHANGING = 0x7c,
    WM_STYLECHANGED = 0x7d, WM_GETICON = 0x7f, WM_NCCREATE = 0x81, WM_NCDESTROY = 0x82, WM_NCCALCSIZE = 0x83,
    WM_NCHITTEST = 0x84, WM_NCPAINT = 0x85, WM_NCACTIVATE = 0x86, WM_GETDLGCODE = 0x87,
    WM_NCMOUSEMOVE = 0xa0, WM_KEYDOWN = 0x100, WM_KEYUP = 0x101, WM_CHAR = 0x102, WM_DEADCHAR = 0x103,
    WM_SYSKEYDOWN = 0x104, WM_SYSKEYUP = 0x105, WM_SYSCHAR = 0x106, WM_SYSDEADCHAR = 0x107,
    WM_INITDIALOG = 0x110, WM_COMMAND = 0x111, WM_SYSCOMMAND = 0x112, WM_TIMER = 0x113, WM_HSCROLL = 0x114,
    WM_VSCROLL = 0x115, WM_INITMENU = 0x116, WM_ENTERIDLE = 0x121, WM_UPDATEUISTATE = 0x128,
    WM_QUERYUISTATE = 0x129, WM_CHANGEUISTATE = 0x127,
    WM_CTLCOLORMSGBOX = 0x132, WM_CTLCOLOREDIT = 0x133, WM_CTLCOLORLISTBOX = 0x134, WM_CTLCOLORBTN = 0x135,
    WM_CTLCOLORDLG = 0x136, WM_CTLCOLORSCROLLBAR = 0x137, WM_CTLCOLORSTATIC = 0x138,
    WM_MOUSEMOVE = 0x200, WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202, WM_LBUTTONDBLCLK = 0x203,
    WM_RBUTTONDOWN = 0x204, WM_RBUTTONUP = 0x205, WM_RBUTTONDBLCLK = 0x206, WM_MBUTTONDOWN = 0x207,
    WM_MBUTTONUP = 0x208, WM_MBUTTONDBLCLK = 0x209, WM_MOUSEWHEEL = 0x20a,
    WM_PARENTNOTIFY = 0x210, WM_ENTERMENULOOP = 0x211, WM_CAPTURECHANGED = 0x215,
    WM_IME_SETCONTEXT = 0x281, WM_IME_NOTIFY = 0x282, WM_HOTKEY = 0x312, WM_USER = 0x400,
};
/* window styles */
enum {
    WS_OVERLAPPED = 0, WS_POPUP = (int)0x80000000u, WS_CHILD = 0x40000000, WS_MINIMIZE = 0x20000000,
    WS_VISIBLE = 0x10000000, WS_DISABLED = 0x08000000, WS_CLIPSIBLINGS = 0x04000000, WS_CLIPCHILDREN = 0x02000000,
    WS_MAXIMIZE = 0x01000000, WS_CAPTION = 0x00c00000, WS_BORDER = 0x00800000, WS_DLGFRAME = 0x00400000,
    WS_VSCROLL = 0x00200000, WS_HSCROLL = 0x00100000, WS_SYSMENU = 0x00080000, WS_THICKFRAME = 0x00040000,
    WS_GROUP = 0x00020000, WS_TABSTOP = 0x00010000,
    WS_EX_DLGMODALFRAME = 1, WS_EX_NOPARENTNOTIFY = 4, WS_EX_TOPMOST = 8, WS_EX_TRANSPARENT = 0x20,
    WS_EX_CLIENTEDGE = 0x200, WS_EX_STATICEDGE = 0x20000, WS_EX_WINDOWEDGE = 0x100, WS_EX_CONTROLPARENT = 0x10000,
    DS_ABSALIGN = 1, DS_SYSMODAL = 2, DS_LOCALEDIT = 0x20, DS_SETFONT = 0x40, DS_MODALFRAME = 0x80,
    DS_NOIDLEMSG = 0x100, DS_SETFOREGROUND = 0x200, DS_CONTROL = 0x400, DS_CENTER = 0x800,
};
enum { GWL_WNDPROC = -4, GWL_HINSTANCE = -6, GWL_HWNDPARENT = -8, GWL_STYLE = -16, GWL_EXSTYLE = -20,
       GWL_USERDATA = -21, GWL_ID = -12, DWL_MSGRESULT = 0, DWL_DLGPROC = 4, DWL_USER = 8, DLGWINDOWEXTRA = 30 };
enum { SW_HIDE = 0, SW_SHOWNORMAL = 1, SW_SHOWMINIMIZED = 2, SW_SHOWMAXIMIZED = 3, SW_SHOWNOACTIVATE = 4,
       SW_SHOW = 5, SW_MINIMIZE = 6, SW_SHOWMINNOACTIVE = 7, SW_SHOWNA = 8, SW_RESTORE = 9, SW_SHOWDEFAULT = 10 };
enum { SWP_NOSIZE = 1, SWP_NOMOVE = 2, SWP_NOZORDER = 4, SWP_NOREDRAW = 8, SWP_NOACTIVATE = 0x10,
       SWP_FRAMECHANGED = 0x20, SWP_SHOWWINDOW = 0x40, SWP_HIDEWINDOW = 0x80, SWP_NOCOPYBITS = 0x100,
       SWP_NOOWNERZORDER = 0x200, SWP_NOSENDCHANGING = 0x400, SWP_DEFERERASE = 0x2000, SWP_ASYNCWINDOWPOS = 0x4000,
       SWP_STATECHANGED = 0x8000 };
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
enum { PM_NOREMOVE = 0, PM_REMOVE = 1, PM_NOYIELD = 2 };
enum { RDW_INVALIDATE = 1, RDW_INTERNALPAINT = 2, RDW_ERASE = 4, RDW_VALIDATE = 8, RDW_NOINTERNALPAINT = 0x10,
       RDW_NOERASE = 0x20, RDW_NOCHILDREN = 0x40, RDW_ALLCHILDREN = 0x80, RDW_UPDATENOW = 0x100,
       RDW_ERASENOW = 0x200, RDW_FRAME = 0x400, RDW_NOFRAME = 0x800 };
enum { GW_HWNDFIRST = 0, GW_HWNDLAST = 1, GW_HWNDNEXT = 2, GW_HWNDPREV = 3, GW_OWNER = 4, GW_CHILD = 5 };
enum { HTERROR = -2, HTTRANSPARENT = -1, HTNOWHERE = 0, HTCLIENT = 1, HTCAPTION = 2 };
enum { MA_ACTIVATE = 1, MA_ACTIVATEANDEAT = 2, MA_NOACTIVATE = 3, MA_NOACTIVATEANDEAT = 4 };
enum { WA_INACTIVE = 0, WA_ACTIVE = 1, WA_CLICKACTIVE = 2 };
enum { MK_LBUTTON = 1, MK_RBUTTON = 2, MK_SHIFT = 4, MK_CONTROL = 8, MK_MBUTTON = 0x10 };
enum { DLGC_WANTARROWS = 1, DLGC_WANTTAB = 2, DLGC_WANTALLKEYS = 4, DLGC_WANTMESSAGE = 4, DLGC_HASSETSEL = 8,
       DLGC_DEFPUSHBUTTON = 0x10, DLGC_UNDEFPUSHBUTTON = 0x20, DLGC_RADIOBUTTON = 0x40, DLGC_WANTCHARS = 0x80,
       DLGC_STATIC = 0x100, DLGC_BUTTON = 0x2000 };
enum { DM_GETDEFID = 0x400, DM_SETDEFID = 0x401 };
enum { IDOK = 1, IDCANCEL = 2, IDABORT = 3, IDRETRY = 4, IDIGNORE = 5, IDYES = 6, IDNO = 7 };
enum { VK_LBUTTON = 1, VK_RBUTTON = 2, VK_MBUTTON = 4, VK_BACK = 8, VK_TAB = 9, VK_RETURN = 0xd, VK_SHIFT = 0x10,
       VK_CONTROL = 0x11, VK_MENU = 0x12, VK_PAUSE = 0x13, VK_CAPITAL = 0x14, VK_ESCAPE = 0x1b, VK_SPACE = 0x20,
       VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24, VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27,
       VK_DOWN = 0x28, VK_SNAPSHOT = 0x2c, VK_INSERT = 0x2d, VK_DELETE = 0x2e, VK_LWIN = 0x5b, VK_RWIN = 0x5c,
       VK_APPS = 0x5d, VK_NUMPAD0 = 0x60, VK_MULTIPLY = 0x6a, VK_ADD = 0x6b, VK_SEPARATOR = 0x6c,
       VK_SUBTRACT = 0x6d, VK_DECIMAL = 0x6e, VK_DIVIDE = 0x6f, VK_F1 = 0x70, VK_NUMLOCK = 0x90,
       VK_SCROLL = 0x91, VK_LSHIFT = 0xa0, VK_RSHIFT = 0xa1, VK_LCONTROL = 0xa2, VK_RCONTROL = 0xa3,
       VK_LMENU = 0xa4, VK_RMENU = 0xa5, VK_OEM_1 = 0xba, VK_OEM_PLUS = 0xbb, VK_OEM_COMMA = 0xbc,
       VK_OEM_MINUS = 0xbd, VK_OEM_PERIOD = 0xbe, VK_OEM_2 = 0xbf, VK_OEM_3 = 0xc0, VK_OEM_4 = 0xdb,
       VK_OEM_5 = 0xdc, VK_OEM_6 = 0xdd, VK_OEM_7 = 0xde };
enum { SC_CLOSE = 0xf060, SC_KEYMENU = 0xf100 };
enum { SM_CXSCREEN = 0, SM_CYSCREEN = 1, SM_CXVSCROLL = 2, SM_CYHSCROLL = 3, SM_CYCAPTION = 4, SM_CXBORDER = 5,
       SM_CYBORDER = 6, SM_CXDLGFRAME = 7, SM_CYDLGFRAME = 8, SM_CXICON = 11, SM_CYICON = 12, SM_CXCURSOR = 13,
       SM_CYCURSOR = 14, SM_CYMENU = 15, SM_CXFRAME = 32, SM_CYFRAME = 33, SM_CXEDGE = 45, SM_CYEDGE = 46,
       SM_CXDOUBLECLK = 36, SM_CYDOUBLECLK = 37 };

/* ---- internal model ---- */
struct ui_class;
typedef struct ui_win ui_win;
struct ui_win {
    HWND h;
    const struct ui_class *cls;
    DWORD style, exstyle;
    RECT wr;            /* window rect, parent client coordinates */
    RECT cr;            /* client rect, parent client coordinates */
    ui_win *parent, *owner;
    ui_win *child, *next, *prev;   /* children in z-order: child = top */
    UINT id;
    WNDPROC proc;
    LONG userdata;
    HINSTANCE inst;
    char *text;
    BYTE *extra; int n_extra;
    void *ctl;          /* built-in control state */
    RECT upd; int has_upd, upd_erase, upd_internal;
    int destroyed, in_destroy;
    unsigned tid;
    void *sdl;          /* SDL_Window for top-level windows that own one */
    HFONT font;         /* for dialogs: the template font */
    /* dialog state */
    int is_dialog, dlg_end, dlg_result; HWND dlg_focus; UINT dlg_defid; int dlg_modal;
};
struct ui_class {
    char name[64];
    UINT style; WNDPROC proc; INT cls_extra, wnd_extra; HINSTANCE inst;
    HICON icon; HCURSOR cursor; HBRUSH bg; int builtin;
    struct ui_class *next;
};

/* log and stop */
void ui_log(const char *fmt, ...);
void ui_trace(const char *fmt, ...);
extern int ui_trace_on;
_Noreturn void ui_fatal(const char *fmt, ...);
#define UI_UNIMPL(name) ui_fatal("win32ui: unsupported call %s (caller %p)", name, __builtin_return_address(0))

/* window table */
ui_win *ui_get(HWND h);
ui_win *ui_get_logged(HWND h, const char *who);
HWND ui_handle(ui_win *w);
ui_win *ui_desktop(void);
LRESULT ui_send(ui_win *w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT ui_send_h(HWND h, UINT msg, WPARAM wp, LPARAM lp);
LRESULT ui_call_proc(WNDPROC p, HWND h, UINT msg, WPARAM wp, LPARAM lp);
ui_win *ui_window_from_point(ui_win *root, POINT screen, int *hit);
void ui_client_to_screen(ui_win *w, POINT *p);
void ui_screen_to_client(ui_win *w, POINT *p);
int ui_is_visible(ui_win *w);
int ui_is_child_of(ui_win *w, ui_win *parent);
ui_win *ui_toplevel(ui_win *w);
void ui_invalidate(ui_win *w, const RECT *r, UINT flags);
void ui_destroy_window(ui_win *w);

/* classes */
const struct ui_class *ui_find_class(const char *name, HINSTANCE inst);
void ui_register_builtin(const char *name, WNDPROC proc, UINT style, int wnd_extra, HCURSOR cur);
void ui_controls_init(void);
void ui_controls_free(ui_win *w);

/* messages, input, timers */
void ui_post(ui_win *w, HWND h, UINT msg, WPARAM wp, LPARAM lp);
DWORD ui_ticks(void);
void ui_set_focus(ui_win *w);
ui_win *ui_focus(void);
ui_win *ui_capture(void);
ui_win *ui_active(void);
void ui_activate(ui_win *w, int how);
void ui_pump_input(void);
void ui_window_shown(ui_win *w);
void ui_remove_window_messages(ui_win *w);
int ui_dialog_message(ui_win *dlg, MSG *m);

/* platform: SDL window for the top-level window */
void *ui_sdl_create(ui_win *w);
void ui_sdl_sync(ui_win *w);
HWND ui_sdl_native(void *sdl);

/* Win32 entry points (exports.c gives them their Windows names) */
#define UIFN(dll, ret, name, args) ret UIAPI ui_##name args;
#include "ui_api.inc"
#undef UIFN

/* GDI fonts (defwnd.c): metrics measured from Wine */
HFONT ui_create_font(int height, int weight, const char *face);
void ui_font_tm(HGDIOBJ font, int tm[20]);   /* tmHeight .. tmCharSet, TEXTMETRICA order */
HGDIOBJ ui_dc_font(HDC dc);

/* text helpers */
char *ui_strdup(const char *s);
int ui_stricmp(const char *a, const char *b);
#define UI_MIN(a, b) ((a) < (b) ? (a) : (b))
#define UI_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LOWORD(x) ((WORD)((DWORD)(x) & 0xffff))
#define HIWORD(x) ((WORD)(((DWORD)(x) >> 16) & 0xffff))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define IS_INTRESOURCE(p) (((uintptr_t)(p) >> 16) == 0)
#endif
