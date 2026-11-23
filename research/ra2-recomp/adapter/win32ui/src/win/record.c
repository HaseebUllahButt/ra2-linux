/* Import-boundary recorder. Included by dllmain.c so the normal make builds it.
 * tools/gen_record.py regenerates the typed wrappers after the marker below. */
#include <stdarg.h>
__declspec(dllimport) void UIAPI SetLastError(DWORD);
INT ui_wvsprintfA(char *, const char *, va_list);
__declspec(dllimport) void *UIAPI GetProcAddress(void *, const char *);
__declspec(dllimport) void *UIAPI VirtualAlloc(void *, size_t, DWORD, DWORD);
__declspec(dllimport) BOOL UIAPI ReadProcessMemory(void *, const void *, void *, size_t, size_t *);
__declspec(dllimport) DWORD UIAPI GetCurrentThreadId(void);
static FILE *rec_file;
static int rec_wine;
static _Thread_local char rec_buf[32768];
static _Thread_local size_t rec_len;
static _Thread_local unsigned rec_depth;
static void rec_add(const char *fmt, ...) {
    if (rec_len >= sizeof rec_buf - 1) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(rec_buf + rec_len, sizeof rec_buf - rec_len, fmt, ap);
    va_end(ap);
    if (n > 0) rec_len += (size_t)n < sizeof rec_buf - rec_len ? (size_t)n : sizeof rec_buf - rec_len - 1;
}
static void rec_line(void) {
    rec_add("\n"); fwrite(rec_buf, 1, rec_len, rec_file); rec_len = 0;
}
static int rec_read(uintptr_t p, void *dst, size_t n) {
    size_t got = 0;
    return p >= 65536 && ReadProcessMemory((void *)-1, (void *)p, dst, n, &got) && got == n;
}
static unsigned rec_ids[3][65536], rec_nids[3];
static void rec_value(const char *type, uintptr_t v) {
    if (!v) { rec_add("0"); return; }
    int kind = !strcmp(type,"HWND") ? 0 : (type[0]=='H' && strcmp(type,"HINSTANCE")) ? 1 : -1;
    if (kind >= 0 && v != (uintptr_t)-1 && v != (uintptr_t)-2) {
        unsigned j; for(j=0;j<rec_nids[kind];j++) if(rec_ids[kind][j]==v) break;
        if(j==rec_nids[kind] && j<65536) rec_ids[kind][rec_nids[kind]++]=v;
        rec_add("%c%u",kind ? 'H':'W',j+1); return;
    }
    uintptr_t top, bottom;
    __asm__("movl %%fs:4,%0; movl %%fs:8,%1" : "=r"(top), "=r"(bottom));
    if(v>=bottom && v<top) {rec_add("STK");return;}
    if(strchr(type,'*') && v>=65536 && !(v>=0x400000 && v<0x800000)) {
        unsigned j;for(j=0;j<rec_nids[2];j++)if(rec_ids[2][j]==v)break;
        if(j==rec_nids[2]&&j<65536)rec_ids[2][rec_nids[2]++]=v;
        rec_add("P%u",j+1);return;
    }
    rec_add("0x%08x",(unsigned)v);
}
static void rec_string(uintptr_t v) {
    if(v<65536) {rec_add("#%u",(unsigned)v);return;}
    rec_add("\"");
    for(unsigned j=0;j<4096;j++) { unsigned char c; if(!rec_read(v+j,&c,1)) {rec_add("<unreadable>");break;} if(!c)break;
        if(c=='"'||c=='\\')rec_add("\\%c",c);else if(c>=32&&c<127)rec_add("%c",c);else rec_add("\\x%02x",c);
    } rec_add("\"");
}
static void rec_message(HWND h, UINT m, WPARAM w, LPARAM l);
static void rec_arg(const char *type, const char *key, uintptr_t v, int contents) {
    rec_add(" %s=",key);
    if(!strcmp(type,"const char *") && contents) {rec_string(v);return;}
    rec_value(type,v);
    if(!contents || !v) return;
    if(strstr(type,"RECT *")) {RECT r;if(rec_read(v,&r,sizeof r))rec_add("{%d,%d,%d,%d}",r.left,r.top,r.right,r.bottom);}
    if(strstr(type,"POINT *")) {POINT p;if(rec_read(v,&p,sizeof p))rec_add("{%d,%d}",p.x,p.y);}
    if(strstr(type,"MSG *")) {MSG m;if(rec_read(v,&m,sizeof m)) {rec_add("{");rec_message(m.hwnd,m.message,m.wParam,m.lParam);rec_add(" time=T pt=%d,%d}",m.pt.x,m.pt.y);}}
    if(strstr(type,"WNDCLASSA *")) {WNDCLASSA c;if(rec_read(v,&c,sizeof c)) {rec_add("{style=%x proc=%08x class=",c.style,(unsigned)c.lpfnWndProc);rec_string((uintptr_t)c.lpszClassName);rec_add(" menu=");rec_string((uintptr_t)c.lpszMenuName);rec_add(" extra=%d,%d}",c.cbClsExtra,c.cbWndExtra);}}
}
static void rec_message(HWND h, UINT m, WPARAM w, LPARAM l) {
    rec_add(" h=");rec_value("HWND",h);rec_add(" msg=0x%04x w=",m);
    rec_value((m==7||m==8||m==0x20||m==0x21||m==0x86||m==0x215)?"HWND":(m==0x14||m==0x30||(m>=0x132&&m<=0x138))?"HDC":"UINT",w);
    rec_add(" l=");
    if(m==0x113) rec_add("T");
    else rec_value((m==0x111||m==0x114||m==0x115||m==0x210)?"HWND":(m==0x1||m==0x81||m==0x24||m==0x46||m==0x47||m==0x83||m==0x2b||m==0x2c||m==0x4e)?"void *":"LONG",(uintptr_t)l);
    if(m==1||m==0x81) {CREATESTRUCTA c;if(rec_read((uintptr_t)l,&c,sizeof c)) {
        rec_add(" create={parent=");rec_value("HWND",c.hwndParent);rec_add(" menu=");rec_value("HMENU",c.hMenu);
        rec_add(" pos=%d,%d,%d,%d style=%x ex=%x name=",c.x,c.y,c.cx,c.cy,c.style,c.dwExStyle);rec_string((uintptr_t)c.lpszName);
        rec_add(" class=");rec_string((uintptr_t)c.lpszClass);rec_add(" param=");rec_value("void *",(uintptr_t)c.lpCreateParams);rec_add("}");}}
    if(m==0x83 || m==0x214 || m==0x216) {RECT r;if(rec_read((uintptr_t)l,&r,sizeof r))rec_add(" rect={%d,%d,%d,%d}",r.left,r.top,r.right,r.bottom);}
    if(m==0xc || m==0x143 || m==0x14a || m==0x180 || m==0x181) {rec_add(" text=");rec_string((uintptr_t)l);}
    if(m==0x46||m==0x47) {WINDOWPOS p;if(rec_read((uintptr_t)l,&p,sizeof p)){rec_add(" pos={after=");rec_value("HWND",p.hwndInsertAfter);rec_add(" %d,%d,%d,%d flags=%x}",p.x,p.y,p.cx,p.cy,p.flags);}}
}
struct rec_proc {uintptr_t original; void *thunk; int timer;};
static struct rec_proc rec_procs[4096];static unsigned rec_nprocs;
static uintptr_t rec_unwrap(uintptr_t p) {for(unsigned j=0;j<rec_nprocs;j++)if((uintptr_t)rec_procs[j].thunk==p)return rec_procs[j].original;return p;}
static LRESULT __cdecl rec_deliver(unsigned index, HWND h, UINT m, WPARAM w, LPARAM l) {
    DWORD entry_error=GetLastError();
    struct rec_proc *p=&rec_procs[index];char cls[256]={0};int id;
    if(rec_wine) {static int(UIAPI *gc)(HWND,char*,int);static int(UIAPI *gi)(HWND);if(!gc){void*u=GetModuleHandleA("USER32.DLL");gc=GetProcAddress(u,"GetClassNameA");gi=GetProcAddress(u,"GetDlgCtrlID");}gc(h,cls,sizeof cls);id=gi(h);}
    else {ui_GetClassNameA(h,cls,sizeof cls);id=ui_GetDlgCtrlID(h);}
    rec_add("MSG depth=%u proc=%08x kind=%s class=",rec_depth++,(unsigned)p->original,p->timer?"timer":"window");rec_string((uintptr_t)cls);rec_add(" id=%d",id);rec_message(h,m,w,l);rec_line();
    SetLastError(entry_error);
    LRESULT result=0;
    if(p->timer)((TIMERPROC)p->original)(h,m,w,l);else result=((WNDPROC)p->original)(h,m,w,l);
    DWORD exit_error=GetLastError();
    rec_add("MRET depth=%u proc=%08x msg=0x%04x result=",--rec_depth,(unsigned)p->original,m);rec_value("LONG",result);rec_line();SetLastError(exit_error);return result;
}
static void *rec_wrap(uintptr_t p,int timer) {
    if(p<0x401000||p>=0x800000)return(void*)p;
    for(unsigned j=0;j<rec_nprocs;j++)if(rec_procs[j].original==p&&rec_procs[j].timer==timer)return rec_procs[j].thunk;
    if(rec_nprocs==4096)ui_fatal("recorder thunk table full");
    unsigned j=rec_nprocs++; BYTE *s=VirtualAlloc(0,64,0x3000,0x40);
    if(!s)ui_fatal("recorder thunk allocation failed");
    /* Copy four stdcall arguments, then add the procedure index for cdecl. */
    int n=0;for(int k=0;k<4;k++){s[n++]=0xff;s[n++]=0x74;s[n++]=0x24;s[n++]=16;}
    s[n++]=0x68;memcpy(s+n,&j,4);n+=4;s[n++]=0xe8;int32_t rel=(BYTE*)rec_deliver-(s+n+4);memcpy(s+n,&rel,4);n+=4;
    s[n++]=0x83;s[n++]=0xc4;s[n++]=20;s[n++]=0xc2;s[n++]=16;s[n++]=0;
    rec_procs[j]=(struct rec_proc){p,s,timer};return s;
}
static void *rec_real(const char *dll,const char *name) {void *m=GetModuleHandleA(dll);if(!m)m=LoadLibraryA(dll);void *p=GetProcAddress(m,name[0]=='#'?(const char*)(uintptr_t)atoi(name+1):name);if(!p)ui_fatal("recorder cannot resolve %s!%s",dll,name);return p;}
static void rec_begin(const char *name,void *caller) {rec_add("CALL depth=%u fn=%s caller=%08x",rec_depth++,name,(unsigned)caller);}
static void rec_end(const char *name,const char *type,uintptr_t result) {rec_add("RET depth=%u fn=%s result=",--rec_depth,name);rec_value(type,result);}
/* GENERATED WRAPPERS */
static LRESULT UIAPI rec_DefWindowProcA(HWND h, UINT m, WPARAM w, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("DefWindowProcA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","m",(uintptr_t)m,1);
 rec_arg("WPARAM","w",(uintptr_t)w,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_message(h,m,w,l);
 rec_line();
 static LRESULT(UIAPI *real)(HWND h, UINT m, WPARAM w, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","DefWindowProcA");
 SetLastError(entry_error);
 LRESULT result=(rec_wine?real:ui_DefWindowProcA)(h,m,w,l);
 DWORD exit_error=GetLastError();
 rec_end("DefWindowProcA","LRESULT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_SetFocus(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("SetFocus",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetFocus");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_SetFocus)(h);
 DWORD exit_error=GetLastError();
 rec_end("SetFocus","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HMENU UIAPI rec_GetMenu(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("GetMenu",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HMENU(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetMenu");
 SetLastError(entry_error);
 HMENU result=(rec_wine?real:ui_GetMenu)(h);
 DWORD exit_error=GetLastError();
 rec_end("GetMenu","HMENU",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_WindowFromPoint(LONG x, LONG y) {
 DWORD entry_error=GetLastError(); rec_begin("WindowFromPoint",__builtin_return_address(0));
 rec_arg("LONG","x",(uintptr_t)x,1);
 rec_arg("LONG","y",(uintptr_t)y,1);
 rec_line();
 static HWND(UIAPI *real)(LONG x, LONG y); if(rec_wine&&!real)real=rec_real("USER32.DLL","WindowFromPoint");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_WindowFromPoint)(x,y);
 DWORD exit_error=GetLastError();
 rec_end("WindowFromPoint","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_GetWindowRect(HWND h, RECT *r) {
 DWORD entry_error=GetLastError(); rec_begin("GetWindowRect",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("RECT *","r",(uintptr_t)r,0);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, RECT *r); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetWindowRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_GetWindowRect)(h,r);
 DWORD exit_error=GetLastError();
 rec_end("GetWindowRect","BOOL",(uintptr_t)result);
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetActiveWindow(void) {
 DWORD entry_error=GetLastError(); rec_begin("GetActiveWindow",__builtin_return_address(0));
 rec_line();
 static HWND(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetActiveWindow");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetActiveWindow)();
 DWORD exit_error=GetLastError();
 rec_end("GetActiveWindow","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_ToAscii(UINT vk, UINT scan, const BYTE *state, WORD *out, UINT flags) {
 DWORD entry_error=GetLastError(); rec_begin("ToAscii",__builtin_return_address(0));
 rec_arg("UINT","vk",(uintptr_t)vk,1);
 rec_arg("UINT","scan",(uintptr_t)scan,1);
 rec_arg("const BYTE *","state",(uintptr_t)state,1);
 rec_arg("WORD *","out",(uintptr_t)out,1);
 rec_arg("UINT","flags",(uintptr_t)flags,1);
 rec_line();
 static INT(UIAPI *real)(UINT vk, UINT scan, const BYTE *state, WORD *out, UINT flags); if(rec_wine&&!real)real=rec_real("USER32.DLL","ToAscii");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_ToAscii)(vk,scan,state,out,flags);
 DWORD exit_error=GetLastError();
 rec_end("ToAscii","INT",(uintptr_t)result);
 rec_arg("WORD *","out",(uintptr_t)out,1);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_ChildWindowFromPointEx(HWND h, LONG x, LONG y, UINT flags) {
 DWORD entry_error=GetLastError(); rec_begin("ChildWindowFromPointEx",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("LONG","x",(uintptr_t)x,1);
 rec_arg("LONG","y",(uintptr_t)y,1);
 rec_arg("UINT","flags",(uintptr_t)flags,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h, LONG x, LONG y, UINT flags); if(rec_wine&&!real)real=rec_real("USER32.DLL","ChildWindowFromPointEx");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_ChildWindowFromPointEx)(h,x,y,flags);
 DWORD exit_error=GetLastError();
 rec_end("ChildWindowFromPointEx","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_GetCursorPos(POINT *p) {
 DWORD entry_error=GetLastError(); rec_begin("GetCursorPos",__builtin_return_address(0));
 rec_arg("POINT *","p",(uintptr_t)p,0);
 rec_line();
 static BOOL(UIAPI *real)(POINT *p); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetCursorPos");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_GetCursorPos)(p);
 DWORD exit_error=GetLastError();
 rec_end("GetCursorPos","BOOL",(uintptr_t)result);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetKeyState(INT vk) {
 DWORD entry_error=GetLastError(); rec_begin("GetKeyState",__builtin_return_address(0));
 rec_arg("INT","vk",(uintptr_t)vk,1);
 rec_line();
 static INT(UIAPI *real)(INT vk); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetKeyState");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetKeyState)(vk);
 DWORD exit_error=GetLastError();
 rec_end("GetKeyState","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_IsWindowEnabled(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("IsWindowEnabled",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","IsWindowEnabled");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_IsWindowEnabled)(h);
 DWORD exit_error=GetLastError();
 rec_end("IsWindowEnabled","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetAsyncKeyState(INT vk) {
 DWORD entry_error=GetLastError(); rec_begin("GetAsyncKeyState",__builtin_return_address(0));
 rec_arg("INT","vk",(uintptr_t)vk,1);
 rec_line();
 static INT(UIAPI *real)(INT vk); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetAsyncKeyState");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetAsyncKeyState)(vk);
 DWORD exit_error=GetLastError();
 rec_end("GetAsyncKeyState","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_IntersectRect(RECT *d, const RECT *a, const RECT *b) {
 DWORD entry_error=GetLastError(); rec_begin("IntersectRect",__builtin_return_address(0));
 rec_arg("RECT *","d",(uintptr_t)d,0);
 rec_arg("const RECT *","a",(uintptr_t)a,1);
 rec_arg("const RECT *","b",(uintptr_t)b,1);
 rec_line();
 static BOOL(UIAPI *real)(RECT *d, const RECT *a, const RECT *b); if(rec_wine&&!real)real=rec_real("USER32.DLL","IntersectRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_IntersectRect)(d,a,b);
 DWORD exit_error=GetLastError();
 rec_end("IntersectRect","BOOL",(uintptr_t)result);
 rec_arg("RECT *","d",(uintptr_t)d,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_EndDialog(HWND h, INT r) {
 DWORD entry_error=GetLastError(); rec_begin("EndDialog",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","r",(uintptr_t)r,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT r); if(rec_wine&&!real)real=rec_real("USER32.DLL","EndDialog");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_EndDialog)(h,r);
 DWORD exit_error=GetLastError();
 rec_end("EndDialog","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetDlgItemTextA(HWND h, INT id, const char *s) {
 DWORD entry_error=GetLastError(); rec_begin("SetDlgItemTextA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","id",(uintptr_t)id,1);
 rec_arg("const char *","s",(uintptr_t)s,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT id, const char *s); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetDlgItemTextA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetDlgItemTextA)(h,id,s);
 DWORD exit_error=GetLastError();
 rec_end("SetDlgItemTextA","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_DialogBoxParamA(HINSTANCE i, const char *t, HWND p, DLGPROC f, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("DialogBoxParamA",__builtin_return_address(0));
 rec_arg("HINSTANCE","i",(uintptr_t)i,1);
 rec_arg("const char *","t",(uintptr_t)t,1);
 rec_arg("HWND","p",(uintptr_t)p,1);
 rec_arg("DLGPROC","f",(uintptr_t)f,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_line();
 f=(DLGPROC)rec_wrap((uintptr_t)f,0);
 static INT(UIAPI *real)(HINSTANCE i, const char *t, HWND p, DLGPROC f, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","DialogBoxParamA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_DialogBoxParamA)(i,t,p,f,l);
 DWORD exit_error=GetLastError();
 rec_end("DialogBoxParamA","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_DialogBoxIndirectParamA(HINSTANCE i, const void *t, HWND p, DLGPROC f, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("DialogBoxIndirectParamA",__builtin_return_address(0));
 rec_arg("HINSTANCE","i",(uintptr_t)i,1);
 rec_arg("const void *","t",(uintptr_t)t,1);
 rec_arg("HWND","p",(uintptr_t)p,1);
 rec_arg("DLGPROC","f",(uintptr_t)f,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_line();
 f=(DLGPROC)rec_wrap((uintptr_t)f,0);
 static INT(UIAPI *real)(HINSTANCE i, const void *t, HWND p, DLGPROC f, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","DialogBoxIndirectParamA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_DialogBoxIndirectParamA)(i,t,p,f,l);
 DWORD exit_error=GetLastError();
 rec_end("DialogBoxIndirectParamA","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_ShowCursor(BOOL show) {
 DWORD entry_error=GetLastError(); rec_begin("ShowCursor",__builtin_return_address(0));
 rec_arg("BOOL","show",(uintptr_t)show,1);
 rec_line();
 static INT(UIAPI *real)(BOOL show); if(rec_wine&&!real)real=rec_real("USER32.DLL","ShowCursor");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_ShowCursor)(show);
 DWORD exit_error=GetLastError();
 rec_end("ShowCursor","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetWindowPos(HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags) {
 DWORD entry_error=GetLastError(); rec_begin("SetWindowPos",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("HWND","after",(uintptr_t)after,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_arg("INT","cx",(uintptr_t)cx,1);
 rec_arg("INT","cy",(uintptr_t)cy,1);
 rec_arg("UINT","flags",(uintptr_t)flags,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetWindowPos");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetWindowPos)(h,after,x,y,cx,cy,flags);
 DWORD exit_error=GetLastError();
 rec_end("SetWindowPos","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_DestroyWindow(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("DestroyWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","DestroyWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_DestroyWindow)(h);
 DWORD exit_error=GetLastError();
 rec_end("DestroyWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static UINT UIAPI rec_MapVirtualKeyA(UINT code, UINT type) {
 DWORD entry_error=GetLastError(); rec_begin("MapVirtualKeyA",__builtin_return_address(0));
 rec_arg("UINT","code",(uintptr_t)code,1);
 rec_arg("UINT","type",(uintptr_t)type,1);
 rec_line();
 static UINT(UIAPI *real)(UINT code, UINT type); if(rec_wine&&!real)real=rec_real("USER32.DLL","MapVirtualKeyA");
 SetLastError(entry_error);
 UINT result=(rec_wine?real:ui_MapVirtualKeyA)(code,type);
 DWORD exit_error=GetLastError();
 rec_end("MapVirtualKeyA","UINT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetSystemMetrics(INT i) {
 DWORD entry_error=GetLastError(); rec_begin("GetSystemMetrics",__builtin_return_address(0));
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_line();
 static INT(UIAPI *real)(INT i); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetSystemMetrics");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetSystemMetrics)(i);
 DWORD exit_error=GetLastError();
 rec_end("GetSystemMetrics","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_MoveWindow(HWND h, INT x, INT y, INT cx, INT cy, BOOL repaint) {
 DWORD entry_error=GetLastError(); rec_begin("MoveWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_arg("INT","cx",(uintptr_t)cx,1);
 rec_arg("INT","cy",(uintptr_t)cy,1);
 rec_arg("BOOL","repaint",(uintptr_t)repaint,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT x, INT y, INT cx, INT cy, BOOL repaint); if(rec_wine&&!real)real=rec_real("USER32.DLL","MoveWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_MoveWindow)(h,x,y,cx,cy,repaint);
 DWORD exit_error=GetLastError();
 rec_end("MoveWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex) {
 DWORD entry_error=GetLastError(); rec_begin("AdjustWindowRectEx",__builtin_return_address(0));
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_arg("DWORD","style",(uintptr_t)style,1);
 rec_arg("BOOL","menu",(uintptr_t)menu,1);
 rec_arg("DWORD","ex",(uintptr_t)ex,1);
 rec_line();
 static BOOL(UIAPI *real)(RECT *r, DWORD style, BOOL menu, DWORD ex); if(rec_wine&&!real)real=rec_real("USER32.DLL","AdjustWindowRectEx");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_AdjustWindowRectEx)(r,style,menu,ex);
 DWORD exit_error=GetLastError();
 rec_end("AdjustWindowRectEx","BOOL",(uintptr_t)result);
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ReleaseCapture(void) {
 DWORD entry_error=GetLastError(); rec_begin("ReleaseCapture",__builtin_return_address(0));
 rec_line();
 static BOOL(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("USER32.DLL","ReleaseCapture");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ReleaseCapture)();
 DWORD exit_error=GetLastError();
 rec_end("ReleaseCapture","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetCapture(void) {
 DWORD entry_error=GetLastError(); rec_begin("GetCapture",__builtin_return_address(0));
 rec_line();
 static HWND(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetCapture");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetCapture)();
 DWORD exit_error=GetLastError();
 rec_end("GetCapture","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_SetCapture(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("SetCapture",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetCapture");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_SetCapture)(h);
 DWORD exit_error=GetLastError();
 rec_end("SetCapture","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetDlgCtrlID(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("GetDlgCtrlID",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static INT(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetDlgCtrlID");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetDlgCtrlID)(h);
 DWORD exit_error=GetLastError();
 rec_end("GetDlgCtrlID","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ValidateRect(HWND h, const RECT *r) {
 DWORD entry_error=GetLastError(); rec_begin("ValidateRect",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("const RECT *","r",(uintptr_t)r,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, const RECT *r); if(rec_wine&&!real)real=rec_real("USER32.DLL","ValidateRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ValidateRect)(h,r);
 DWORD exit_error=GetLastError();
 rec_end("ValidateRect","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_AdjustWindowRect(RECT *r, DWORD style, BOOL menu) {
 DWORD entry_error=GetLastError(); rec_begin("AdjustWindowRect",__builtin_return_address(0));
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_arg("DWORD","style",(uintptr_t)style,1);
 rec_arg("BOOL","menu",(uintptr_t)menu,1);
 rec_line();
 static BOOL(UIAPI *real)(RECT *r, DWORD style, BOOL menu); if(rec_wine&&!real)real=rec_real("USER32.DLL","AdjustWindowRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_AdjustWindowRect)(r,style,menu);
 DWORD exit_error=GetLastError();
 rec_end("AdjustWindowRect","BOOL",(uintptr_t)result);
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_GetSysColor(INT i) {
 DWORD entry_error=GetLastError(); rec_begin("GetSysColor",__builtin_return_address(0));
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_line();
 static DWORD(UIAPI *real)(INT i); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetSysColor");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_GetSysColor)(i);
 DWORD exit_error=GetLastError();
 rec_end("GetSysColor","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_CreateDialogParamA(HINSTANCE i, const char *t, HWND p, DLGPROC f, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("CreateDialogParamA",__builtin_return_address(0));
 rec_arg("HINSTANCE","i",(uintptr_t)i,1);
 rec_arg("const char *","t",(uintptr_t)t,1);
 rec_arg("HWND","p",(uintptr_t)p,1);
 rec_arg("DLGPROC","f",(uintptr_t)f,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_line();
 f=(DLGPROC)rec_wrap((uintptr_t)f,0);
 static HWND(UIAPI *real)(HINSTANCE i, const char *t, HWND p, DLGPROC f, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","CreateDialogParamA");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_CreateDialogParamA)(i,t,p,f,l);
 DWORD exit_error=GetLastError();
 rec_end("CreateDialogParamA","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static UINT UIAPI rec_IsDlgButtonChecked(HWND h, INT id) {
 DWORD entry_error=GetLastError(); rec_begin("IsDlgButtonChecked",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","id",(uintptr_t)id,1);
 rec_line();
 static UINT(UIAPI *real)(HWND h, INT id); if(rec_wine&&!real)real=rec_real("USER32.DLL","IsDlgButtonChecked");
 SetLastError(entry_error);
 UINT result=(rec_wine?real:ui_IsDlgButtonChecked)(h,id);
 DWORD exit_error=GetLastError();
 rec_end("IsDlgButtonChecked","UINT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_CheckDlgButton(HWND h, INT id, UINT check) {
 DWORD entry_error=GetLastError(); rec_begin("CheckDlgButton",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","id",(uintptr_t)id,1);
 rec_arg("UINT","check",(uintptr_t)check,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT id, UINT check); if(rec_wine&&!real)real=rec_real("USER32.DLL","CheckDlgButton");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_CheckDlgButton)(h,id,check);
 DWORD exit_error=GetLastError();
 rec_end("CheckDlgButton","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_WaitForInputIdle(uint32_t process, DWORD ms) {
 DWORD entry_error=GetLastError(); rec_begin("WaitForInputIdle",__builtin_return_address(0));
 rec_arg("uint32_t","process",(uintptr_t)process,1);
 rec_arg("DWORD","ms",(uintptr_t)ms,1);
 rec_line();
 static DWORD(UIAPI *real)(uint32_t process, DWORD ms); if(rec_wine&&!real)real=rec_real("USER32.DLL","WaitForInputIdle");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_WaitForInputIdle)(process,ms);
 DWORD exit_error=GetLastError();
 rec_end("WaitForInputIdle","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetTopWindow(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("GetTopWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetTopWindow");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetTopWindow)(h);
 DWORD exit_error=GetLastError();
 rec_end("GetTopWindow","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetForegroundWindow(void) {
 DWORD entry_error=GetLastError(); rec_begin("GetForegroundWindow",__builtin_return_address(0));
 rec_line();
 static HWND(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetForegroundWindow");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetForegroundWindow)();
 DWORD exit_error=GetLastError();
 rec_end("GetForegroundWindow","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HICON UIAPI rec_LoadIconA(HINSTANCE i, const char *name) {
 DWORD entry_error=GetLastError(); rec_begin("LoadIconA",__builtin_return_address(0));
 rec_arg("HINSTANCE","i",(uintptr_t)i,1);
 rec_arg("const char *","name",(uintptr_t)name,1);
 rec_line();
 static HICON(UIAPI *real)(HINSTANCE i, const char *name); if(rec_wine&&!real)real=rec_real("USER32.DLL","LoadIconA");
 SetLastError(entry_error);
 HICON result=(rec_wine?real:ui_LoadIconA)(i,name);
 DWORD exit_error=GetLastError();
 rec_end("LoadIconA","HICON",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_SetActiveWindow(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("SetActiveWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetActiveWindow");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_SetActiveWindow)(h);
 DWORD exit_error=GetLastError();
 rec_end("SetActiveWindow","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_RedrawWindow(HWND h, const RECT *r, HRGN rgn, UINT flags) {
 DWORD entry_error=GetLastError(); rec_begin("RedrawWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("const RECT *","r",(uintptr_t)r,1);
 rec_arg("HRGN","rgn",(uintptr_t)rgn,1);
 rec_arg("UINT","flags",(uintptr_t)flags,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, const RECT *r, HRGN rgn, UINT flags); if(rec_wine&&!real)real=rec_real("USER32.DLL","RedrawWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_RedrawWindow)(h,r,rgn,flags);
 DWORD exit_error=GetLastError();
 rec_end("RedrawWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_GetWindowContextHelpId(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("GetWindowContextHelpId",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static DWORD(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetWindowContextHelpId");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_GetWindowContextHelpId)(h);
 DWORD exit_error=GetLastError();
 rec_end("GetWindowContextHelpId","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_WinHelpA(HWND h, const char *file, UINT cmd, DWORD data) {
 DWORD entry_error=GetLastError(); rec_begin("WinHelpA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("const char *","file",(uintptr_t)file,1);
 rec_arg("UINT","cmd",(uintptr_t)cmd,1);
 rec_arg("DWORD","data",(uintptr_t)data,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, const char *file, UINT cmd, DWORD data); if(rec_wine&&!real)real=rec_real("USER32.DLL","WinHelpA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_WinHelpA)(h,file,cmd,data);
 DWORD exit_error=GetLastError();
 rec_end("WinHelpA","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_ChildWindowFromPoint(HWND h, LONG x, LONG y) {
 DWORD entry_error=GetLastError(); rec_begin("ChildWindowFromPoint",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("LONG","x",(uintptr_t)x,1);
 rec_arg("LONG","y",(uintptr_t)y,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h, LONG x, LONG y); if(rec_wine&&!real)real=rec_real("USER32.DLL","ChildWindowFromPoint");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_ChildWindowFromPoint)(h,x,y);
 DWORD exit_error=GetLastError();
 rec_end("ChildWindowFromPoint","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HCURSOR UIAPI rec_LoadCursorA(HINSTANCE i, const char *name) {
 DWORD entry_error=GetLastError(); rec_begin("LoadCursorA",__builtin_return_address(0));
 rec_arg("HINSTANCE","i",(uintptr_t)i,1);
 rec_arg("const char *","name",(uintptr_t)name,1);
 rec_line();
 static HCURSOR(UIAPI *real)(HINSTANCE i, const char *name); if(rec_wine&&!real)real=rec_real("USER32.DLL","LoadCursorA");
 SetLastError(entry_error);
 HCURSOR result=(rec_wine?real:ui_LoadCursorA)(i,name);
 DWORD exit_error=GetLastError();
 rec_end("LoadCursorA","HCURSOR",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HCURSOR UIAPI rec_SetCursor(HCURSOR c) {
 DWORD entry_error=GetLastError(); rec_begin("SetCursor",__builtin_return_address(0));
 rec_arg("HCURSOR","c",(uintptr_t)c,1);
 rec_line();
 static HCURSOR(UIAPI *real)(HCURSOR c); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetCursor");
 SetLastError(entry_error);
 HCURSOR result=(rec_wine?real:ui_SetCursor)(c);
 DWORD exit_error=GetLastError();
 rec_end("SetCursor","HCURSOR",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static void UIAPI rec_PostQuitMessage(INT code) {
 DWORD entry_error=GetLastError(); rec_begin("PostQuitMessage",__builtin_return_address(0));
 rec_arg("INT","code",(uintptr_t)code,1);
 rec_line();
 static void(UIAPI *real)(INT code); if(rec_wine&&!real)real=rec_real("USER32.DLL","PostQuitMessage");
 SetLastError(entry_error);
 (rec_wine?real:ui_PostQuitMessage)(code);
 DWORD exit_error=GetLastError();
 rec_end("PostQuitMessage","void",0);
 rec_line();SetLastError(exit_error);
}
static HWND UIAPI rec_FindWindowA(const char *cls, const char *title) {
 DWORD entry_error=GetLastError(); rec_begin("FindWindowA",__builtin_return_address(0));
 rec_arg("const char *","cls",(uintptr_t)cls,1);
 rec_arg("const char *","title",(uintptr_t)title,1);
 rec_line();
 static HWND(UIAPI *real)(const char *cls, const char *title); if(rec_wine&&!real)real=rec_real("USER32.DLL","FindWindowA");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_FindWindowA)(cls,title);
 DWORD exit_error=GetLastError();
 rec_end("FindWindowA","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetCursorPos(INT x, INT y) {
 DWORD entry_error=GetLastError(); rec_begin("SetCursorPos",__builtin_return_address(0));
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_line();
 static BOOL(UIAPI *real)(INT x, INT y); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetCursorPos");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetCursorPos)(x,y);
 DWORD exit_error=GetLastError();
 rec_end("SetCursorPos","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_CreateDialogIndirectParamA(HINSTANCE i, const void *t, HWND p, DLGPROC f, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("CreateDialogIndirectParamA",__builtin_return_address(0));
 rec_arg("HINSTANCE","i",(uintptr_t)i,1);
 rec_arg("const void *","t",(uintptr_t)t,1);
 rec_arg("HWND","p",(uintptr_t)p,1);
 rec_arg("DLGPROC","f",(uintptr_t)f,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_line();
 f=(DLGPROC)rec_wrap((uintptr_t)f,0);
 static HWND(UIAPI *real)(HINSTANCE i, const void *t, HWND p, DLGPROC f, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","CreateDialogIndirectParamA");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_CreateDialogIndirectParamA)(i,t,p,f,l);
 DWORD exit_error=GetLastError();
 rec_end("CreateDialogIndirectParamA","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetKeyNameTextA(LONG l, char *buf, INT n) {
 DWORD entry_error=GetLastError(); rec_begin("GetKeyNameTextA",__builtin_return_address(0));
 rec_arg("LONG","l",(uintptr_t)l,1);
 rec_arg("char *","buf",(uintptr_t)buf,0);
 rec_arg("INT","n",(uintptr_t)n,1);
 rec_line();
 static INT(UIAPI *real)(LONG l, char *buf, INT n); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetKeyNameTextA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetKeyNameTextA)(l,buf,n);
 DWORD exit_error=GetLastError();
 rec_end("GetKeyNameTextA","INT",(uintptr_t)result);
 rec_arg("char *","buf",(uintptr_t)buf,1);
 rec_add(" buf_text=");rec_string((uintptr_t)buf);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ScreenToClient(HWND h, POINT *p) {
 DWORD entry_error=GetLastError(); rec_begin("ScreenToClient",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, POINT *p); if(rec_wine&&!real)real=rec_real("USER32.DLL","ScreenToClient");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ScreenToClient)(h,p);
 DWORD exit_error=GetLastError();
 rec_end("ScreenToClient","BOOL",(uintptr_t)result);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_LockWindowUpdate(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("LockWindowUpdate",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","LockWindowUpdate");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_LockWindowUpdate)(h);
 DWORD exit_error=GetLastError();
 rec_end("LockWindowUpdate","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HDC UIAPI rec_GetDC(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("GetDC",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HDC(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetDC");
 SetLastError(entry_error);
 HDC result=(rec_wine?real:ui_GetDC)(h);
 DWORD exit_error=GetLastError();
 rec_end("GetDC","HDC",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_CloseWindow(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("CloseWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","CloseWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_CloseWindow)(h);
 DWORD exit_error=GetLastError();
 rec_end("CloseWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetFocus(void) {
 DWORD entry_error=GetLastError(); rec_begin("GetFocus",__builtin_return_address(0));
 rec_line();
 static HWND(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetFocus");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetFocus)();
 DWORD exit_error=GetLastError();
 rec_end("GetFocus","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_PeekMessageA(MSG *m, HWND h, UINT lo, UINT hi, UINT flags) {
 DWORD entry_error=GetLastError(); rec_begin("PeekMessageA",__builtin_return_address(0));
 rec_arg("MSG *","m",(uintptr_t)m,0);
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","lo",(uintptr_t)lo,1);
 rec_arg("UINT","hi",(uintptr_t)hi,1);
 rec_arg("UINT","flags",(uintptr_t)flags,1);
 rec_line();
 static BOOL(UIAPI *real)(MSG *m, HWND h, UINT lo, UINT hi, UINT flags); if(rec_wine&&!real)real=rec_real("USER32.DLL","PeekMessageA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_PeekMessageA)(m,h,lo,hi,flags);
 DWORD exit_error=GetLastError();
 rec_end("PeekMessageA","BOOL",(uintptr_t)result);
 if(result)rec_arg("MSG *","m",(uintptr_t)m,1);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_MessageBoxIndirectA(const void *params) {
 DWORD entry_error=GetLastError(); rec_begin("MessageBoxIndirectA",__builtin_return_address(0));
 rec_arg("const void *","params",(uintptr_t)params,1);
 rec_line();
 static INT(UIAPI *real)(const void *params); if(rec_wine&&!real)real=rec_real("USER32.DLL","MessageBoxIndirectA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_MessageBoxIndirectA)(params);
 DWORD exit_error=GetLastError();
 rec_end("MessageBoxIndirectA","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_UpdateWindow(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("UpdateWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","UpdateWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_UpdateWindow)(h);
 DWORD exit_error=GetLastError();
 rec_end("UpdateWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_PostMessageA(HWND h, UINT m, WPARAM w, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("PostMessageA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","m",(uintptr_t)m,1);
 rec_arg("WPARAM","w",(uintptr_t)w,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_message(h,m,w,l);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, UINT m, WPARAM w, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","PostMessageA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_PostMessageA)(h,m,w,l);
 DWORD exit_error=GetLastError();
 rec_end("PostMessageA","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_GetUpdateRect(HWND h, RECT *r, BOOL erase) {
 DWORD entry_error=GetLastError(); rec_begin("GetUpdateRect",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("RECT *","r",(uintptr_t)r,0);
 rec_arg("BOOL","erase",(uintptr_t)erase,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, RECT *r, BOOL erase); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetUpdateRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_GetUpdateRect)(h,r,erase);
 DWORD exit_error=GetLastError();
 rec_end("GetUpdateRect","BOOL",(uintptr_t)result);
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_ReleaseDC(HWND h, HDC dc) {
 DWORD entry_error=GetLastError(); rec_begin("ReleaseDC",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_line();
 static INT(UIAPI *real)(HWND h, HDC dc); if(rec_wine&&!real)real=rec_real("USER32.DLL","ReleaseDC");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_ReleaseDC)(h,dc);
 DWORD exit_error=GetLastError();
 rec_end("ReleaseDC","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_MessageBoxA(HWND h, const char *text, const char *cap, UINT type) {
 DWORD entry_error=GetLastError(); rec_begin("MessageBoxA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("const char *","text",(uintptr_t)text,1);
 rec_arg("const char *","cap",(uintptr_t)cap,1);
 rec_arg("UINT","type",(uintptr_t)type,1);
 rec_line();
 static INT(UIAPI *real)(HWND h, const char *text, const char *cap, UINT type); if(rec_wine&&!real)real=rec_real("USER32.DLL","MessageBoxA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_MessageBoxA)(h,text,cap,type);
 DWORD exit_error=GetLastError();
 rec_end("MessageBoxA","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetWindow(HWND h, UINT cmd) {
 DWORD entry_error=GetLastError(); rec_begin("GetWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","cmd",(uintptr_t)cmd,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h, UINT cmd); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetWindow");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetWindow)(h,cmd);
 DWORD exit_error=GetLastError();
 rec_end("GetWindow","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LRESULT UIAPI rec_SendDlgItemMessageA(HWND h, INT id, UINT m, WPARAM w, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("SendDlgItemMessageA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","id",(uintptr_t)id,1);
 rec_arg("UINT","m",(uintptr_t)m,1);
 rec_arg("WPARAM","w",(uintptr_t)w,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_message(h,m,w,l);
 rec_line();
 static LRESULT(UIAPI *real)(HWND h, INT id, UINT m, WPARAM w, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","SendDlgItemMessageA");
 SetLastError(entry_error);
 LRESULT result=(rec_wine?real:ui_SendDlgItemMessageA)(h,id,m,w,l);
 DWORD exit_error=GetLastError();
 rec_end("SendDlgItemMessageA","LRESULT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LONG UIAPI rec_SetWindowLongA(HWND h, INT i, LONG v) {
 DWORD entry_error=GetLastError(); rec_begin("SetWindowLongA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_arg("LONG","v",(uintptr_t)v,1);
 rec_line();
 if(i==-4||i==4)v=(LONG)rec_wrap((uintptr_t)v,0);
 static LONG(UIAPI *real)(HWND h, INT i, LONG v); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetWindowLongA");
 SetLastError(entry_error);
 LONG result=(rec_wine?real:ui_SetWindowLongA)(h,i,v);
 DWORD exit_error=GetLastError();
 if(i==-4||i==4) result=(LONG)rec_unwrap((uintptr_t)result);
 rec_end("SetWindowLongA","LONG",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LONG UIAPI rec_GetWindowLongA(HWND h, INT i) {
 DWORD entry_error=GetLastError(); rec_begin("GetWindowLongA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_line();
 static LONG(UIAPI *real)(HWND h, INT i); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetWindowLongA");
 SetLastError(entry_error);
 LONG result=(rec_wine?real:ui_GetWindowLongA)(h,i);
 DWORD exit_error=GetLastError();
 if(i==-4||i==4) result=(LONG)rec_unwrap((uintptr_t)result);
 rec_end("GetWindowLongA","LONG",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_EnableWindow(HWND h, BOOL on) {
 DWORD entry_error=GetLastError(); rec_begin("EnableWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("BOOL","on",(uintptr_t)on,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, BOOL on); if(rec_wine&&!real)real=rec_real("USER32.DLL","EnableWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_EnableWindow)(h,on);
 DWORD exit_error=GetLastError();
 rec_end("EnableWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_InvalidateRect(HWND h, const RECT *r, BOOL erase) {
 DWORD entry_error=GetLastError(); rec_begin("InvalidateRect",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("const RECT *","r",(uintptr_t)r,1);
 rec_arg("BOOL","erase",(uintptr_t)erase,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, const RECT *r, BOOL erase); if(rec_wine&&!real)real=rec_real("USER32.DLL","InvalidateRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_InvalidateRect)(h,r,erase);
 DWORD exit_error=GetLastError();
 rec_end("InvalidateRect","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ShowWindow(HWND h, INT cmd) {
 DWORD entry_error=GetLastError(); rec_begin("ShowWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","cmd",(uintptr_t)cmd,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT cmd); if(rec_wine&&!real)real=rec_real("USER32.DLL","ShowWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ShowWindow)(h,cmd);
 DWORD exit_error=GetLastError();
 rec_end("ShowWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetDlgItem(HWND h, INT id) {
 DWORD entry_error=GetLastError(); rec_begin("GetDlgItem",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","id",(uintptr_t)id,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h, INT id); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetDlgItem");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetDlgItem)(h,id);
 DWORD exit_error=GetLastError();
 rec_end("GetDlgItem","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LRESULT UIAPI rec_SendMessageA(HWND h, UINT m, WPARAM w, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("SendMessageA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","m",(uintptr_t)m,1);
 rec_arg("WPARAM","w",(uintptr_t)w,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_message(h,m,w,l);
 rec_line();
 static LRESULT(UIAPI *real)(HWND h, UINT m, WPARAM w, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","SendMessageA");
 SetLastError(entry_error);
 LRESULT result=(rec_wine?real:ui_SendMessageA)(h,m,w,l);
 DWORD exit_error=GetLastError();
 rec_end("SendMessageA","LRESULT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetRect(RECT *r, INT l, INT t, INT ri, INT b) {
 DWORD entry_error=GetLastError(); rec_begin("SetRect",__builtin_return_address(0));
 rec_arg("RECT *","r",(uintptr_t)r,0);
 rec_arg("INT","l",(uintptr_t)l,1);
 rec_arg("INT","t",(uintptr_t)t,1);
 rec_arg("INT","ri",(uintptr_t)ri,1);
 rec_arg("INT","b",(uintptr_t)b,1);
 rec_line();
 static BOOL(UIAPI *real)(RECT *r, INT l, INT t, INT ri, INT b); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetRect)(r,l,t,ri,b);
 DWORD exit_error=GetLastError();
 rec_end("SetRect","BOOL",(uintptr_t)result);
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ClientToScreen(HWND h, POINT *p) {
 DWORD entry_error=GetLastError(); rec_begin("ClientToScreen",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, POINT *p); if(rec_wine&&!real)real=rec_real("USER32.DLL","ClientToScreen");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ClientToScreen)(h,p);
 DWORD exit_error=GetLastError();
 rec_end("ClientToScreen","BOOL",(uintptr_t)result);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_TranslateMessage(const MSG *m) {
 DWORD entry_error=GetLastError(); rec_begin("TranslateMessage",__builtin_return_address(0));
 rec_arg("const MSG *","m",(uintptr_t)m,1);
 rec_line();
 static BOOL(UIAPI *real)(const MSG *m); if(rec_wine&&!real)real=rec_real("USER32.DLL","TranslateMessage");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_TranslateMessage)(m);
 DWORD exit_error=GetLastError();
 rec_end("TranslateMessage","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LRESULT UIAPI rec_DispatchMessageA(const MSG *m) {
 DWORD entry_error=GetLastError(); rec_begin("DispatchMessageA",__builtin_return_address(0));
 rec_arg("const MSG *","m",(uintptr_t)m,1);
 rec_line();
 static LRESULT(UIAPI *real)(const MSG *m); if(rec_wine&&!real)real=rec_real("USER32.DLL","DispatchMessageA");
 SetLastError(entry_error);
 LRESULT result=(rec_wine?real:ui_DispatchMessageA)(m);
 DWORD exit_error=GetLastError();
 rec_end("DispatchMessageA","LRESULT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_GetClientRect(HWND h, RECT *r) {
 DWORD entry_error=GetLastError(); rec_begin("GetClientRect",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("RECT *","r",(uintptr_t)r,0);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, RECT *r); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetClientRect");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_GetClientRect)(h,r);
 DWORD exit_error=GetLastError();
 rec_end("GetClientRect","BOOL",(uintptr_t)result);
 rec_arg("RECT *","r",(uintptr_t)r,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_BringWindowToTop(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("BringWindowToTop",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","BringWindowToTop");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_BringWindowToTop)(h);
 DWORD exit_error=GetLastError();
 rec_end("BringWindowToTop","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LRESULT UIAPI rec_CallWindowProcA(WNDPROC p, HWND h, UINT m, WPARAM w, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("CallWindowProcA",__builtin_return_address(0));
 rec_arg("WNDPROC","p",(uintptr_t)p,1);
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","m",(uintptr_t)m,1);
 rec_arg("WPARAM","w",(uintptr_t)w,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_message(h,m,w,l);
 rec_line();
 p=(WNDPROC)rec_wrap((uintptr_t)p,0);
 static LRESULT(UIAPI *real)(WNDPROC p, HWND h, UINT m, WPARAM w, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","CallWindowProcA");
 SetLastError(entry_error);
 LRESULT result=(rec_wine?real:ui_CallWindowProcA)(p,h,m,w,l);
 DWORD exit_error=GetLastError();
 rec_end("CallWindowProcA","LRESULT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_KillTimer(HWND h, UINT id) {
 DWORD entry_error=GetLastError(); rec_begin("KillTimer",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","id",(uintptr_t)id,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, UINT id); if(rec_wine&&!real)real=rec_real("USER32.DLL","KillTimer");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_KillTimer)(h,id);
 DWORD exit_error=GetLastError();
 rec_end("KillTimer","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_CreateWindowExA(DWORD ex, const char *cls, const char *name, DWORD style, INT x, INT y, INT cx, INT cy, HWND parent, HMENU menu, HINSTANCE inst, void *param) {
 DWORD entry_error=GetLastError(); rec_begin("CreateWindowExA",__builtin_return_address(0));
 rec_arg("DWORD","ex",(uintptr_t)ex,1);
 rec_arg("const char *","cls",(uintptr_t)cls,1);
 rec_arg("const char *","name",(uintptr_t)name,1);
 rec_arg("DWORD","style",(uintptr_t)style,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_arg("INT","cx",(uintptr_t)cx,1);
 rec_arg("INT","cy",(uintptr_t)cy,1);
 rec_arg("HWND","parent",(uintptr_t)parent,1);
 rec_arg("HMENU","menu",(uintptr_t)menu,1);
 rec_arg("HINSTANCE","inst",(uintptr_t)inst,1);
 rec_arg("void *","param",(uintptr_t)param,1);
 rec_line();
 static HWND(UIAPI *real)(DWORD ex, const char *cls, const char *name, DWORD style, INT x, INT y, INT cx, INT cy, HWND parent, HMENU menu, HINSTANCE inst, void *param); if(rec_wine&&!real)real=rec_real("USER32.DLL","CreateWindowExA");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_CreateWindowExA)(ex,cls,name,style,x,y,cx,cy,parent,menu,inst,param);
 DWORD exit_error=GetLastError();
 rec_end("CreateWindowExA","HWND",(uintptr_t)result);
 rec_arg("void *","param",(uintptr_t)param,1);
 rec_line();SetLastError(exit_error);return result;
}
static WORD UIAPI rec_RegisterClassA(const WNDCLASSA *c) {
 DWORD entry_error=GetLastError(); rec_begin("RegisterClassA",__builtin_return_address(0));
 rec_arg("const WNDCLASSA *","c",(uintptr_t)c,1);
 rec_line();
 WNDCLASSA copy; if(c){copy=*c;copy.lpfnWndProc=(WNDPROC)rec_wrap((uintptr_t)c->lpfnWndProc,0);c=&copy;}
 static WORD(UIAPI *real)(const WNDCLASSA *c); if(rec_wine&&!real)real=rec_real("USER32.DLL","RegisterClassA");
 SetLastError(entry_error);
 WORD result=(rec_wine?real:ui_RegisterClassA)(c);
 DWORD exit_error=GetLastError();
 rec_end("RegisterClassA","WORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetForegroundWindow(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("SetForegroundWindow",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetForegroundWindow");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetForegroundWindow)(h);
 DWORD exit_error=GetLastError();
 rec_end("SetForegroundWindow","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static UINT UIAPI rec_SetTimer(HWND h, UINT id, UINT ms, TIMERPROC f) {
 DWORD entry_error=GetLastError(); rec_begin("SetTimer",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("UINT","id",(uintptr_t)id,1);
 rec_arg("UINT","ms",(uintptr_t)ms,1);
 rec_arg("TIMERPROC","f",(uintptr_t)f,1);
 rec_line();
 f=(TIMERPROC)rec_wrap((uintptr_t)f,1);
 static UINT(UIAPI *real)(HWND h, UINT id, UINT ms, TIMERPROC f); if(rec_wine&&!real)real=rec_real("USER32.DLL","SetTimer");
 SetLastError(entry_error);
 UINT result=(rec_wine?real:ui_SetTimer)(h,id,ms,f);
 DWORD exit_error=GetLastError();
 rec_end("SetTimer","UINT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_IsWindowVisible(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("IsWindowVisible",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","IsWindowVisible");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_IsWindowVisible)(h);
 DWORD exit_error=GetLastError();
 rec_end("IsWindowVisible","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_EnumChildWindows(HWND h, WNDENUMPROC f, LPARAM l) {
 DWORD entry_error=GetLastError(); rec_begin("EnumChildWindows",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("WNDENUMPROC","f",(uintptr_t)f,1);
 rec_arg("LPARAM","l",(uintptr_t)l,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, WNDENUMPROC f, LPARAM l); if(rec_wine&&!real)real=rec_real("USER32.DLL","EnumChildWindows");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_EnumChildWindows)(h,f,l);
 DWORD exit_error=GetLastError();
 rec_end("EnumChildWindows","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetClassNameA(HWND h, char *buf, INT n) {
 DWORD entry_error=GetLastError(); rec_begin("GetClassNameA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("char *","buf",(uintptr_t)buf,0);
 rec_arg("INT","n",(uintptr_t)n,1);
 rec_line();
 static INT(UIAPI *real)(HWND h, char *buf, INT n); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetClassNameA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetClassNameA)(h,buf,n);
 DWORD exit_error=GetLastError();
 rec_end("GetClassNameA","INT",(uintptr_t)result);
 rec_arg("char *","buf",(uintptr_t)buf,1);
 rec_add(" buf_text=");rec_string((uintptr_t)buf);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_TranslateAcceleratorA(HWND h, HACCEL a, MSG *m) {
 DWORD entry_error=GetLastError(); rec_begin("TranslateAcceleratorA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("HACCEL","a",(uintptr_t)a,1);
 rec_arg("MSG *","m",(uintptr_t)m,1);
 rec_line();
 static INT(UIAPI *real)(HWND h, HACCEL a, MSG *m); if(rec_wine&&!real)real=rec_real("USER32.DLL","TranslateAcceleratorA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_TranslateAcceleratorA)(h,a,m);
 DWORD exit_error=GetLastError();
 rec_end("TranslateAcceleratorA","INT",(uintptr_t)result);
 rec_arg("MSG *","m",(uintptr_t)m,1);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetWindowTextA(HWND h, char *buf, INT n) {
 DWORD entry_error=GetLastError(); rec_begin("GetWindowTextA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("char *","buf",(uintptr_t)buf,0);
 rec_arg("INT","n",(uintptr_t)n,1);
 rec_line();
 static INT(UIAPI *real)(HWND h, char *buf, INT n); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetWindowTextA");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetWindowTextA)(h,buf,n);
 DWORD exit_error=GetLastError();
 rec_end("GetWindowTextA","INT",(uintptr_t)result);
 rec_arg("char *","buf",(uintptr_t)buf,1);
 rec_add(" buf_text=");rec_string((uintptr_t)buf);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_IsDialogMessageA(HWND h, MSG *m) {
 DWORD entry_error=GetLastError(); rec_begin("IsDialogMessageA",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("MSG *","m",(uintptr_t)m,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, MSG *m); if(rec_wine&&!real)real=rec_real("USER32.DLL","IsDialogMessageA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_IsDialogMessageA)(h,m);
 DWORD exit_error=GetLastError();
 rec_end("IsDialogMessageA","BOOL",(uintptr_t)result);
 rec_arg("MSG *","m",(uintptr_t)m,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_EndPaint(HWND h, const PAINTSTRUCT *ps) {
 DWORD entry_error=GetLastError(); rec_begin("EndPaint",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("const PAINTSTRUCT *","ps",(uintptr_t)ps,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, const PAINTSTRUCT *ps); if(rec_wine&&!real)real=rec_real("USER32.DLL","EndPaint");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_EndPaint)(h,ps);
 DWORD exit_error=GetLastError();
 rec_end("EndPaint","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_CharToOemBuffA(const char *s, char *d, DWORD n) {
 DWORD entry_error=GetLastError(); rec_begin("CharToOemBuffA",__builtin_return_address(0));
 rec_arg("const char *","s",(uintptr_t)s,1);
 rec_arg("char *","d",(uintptr_t)d,0);
 rec_arg("DWORD","n",(uintptr_t)n,1);
 rec_line();
 static BOOL(UIAPI *real)(const char *s, char *d, DWORD n); if(rec_wine&&!real)real=rec_real("USER32.DLL","CharToOemBuffA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_CharToOemBuffA)(s,d,n);
 DWORD exit_error=GetLastError();
 rec_end("CharToOemBuffA","BOOL",(uintptr_t)result);
 rec_arg("char *","d",(uintptr_t)d,1);
 rec_add(" d_text=");rec_string((uintptr_t)d);
 rec_line();SetLastError(exit_error);return result;
}
static HDC UIAPI rec_BeginPaint(HWND h, PAINTSTRUCT *ps) {
 DWORD entry_error=GetLastError(); rec_begin("BeginPaint",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("PAINTSTRUCT *","ps",(uintptr_t)ps,1);
 rec_line();
 static HDC(UIAPI *real)(HWND h, PAINTSTRUCT *ps); if(rec_wine&&!real)real=rec_real("USER32.DLL","BeginPaint");
 SetLastError(entry_error);
 HDC result=(rec_wine?real:ui_BeginPaint)(h,ps);
 DWORD exit_error=GetLastError();
 rec_end("BeginPaint","HDC",(uintptr_t)result);
 rec_arg("PAINTSTRUCT *","ps",(uintptr_t)ps,1);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL prev) {
 DWORD entry_error=GetLastError(); rec_begin("GetNextDlgTabItem",__builtin_return_address(0));
 rec_arg("HWND","dlg",(uintptr_t)dlg,1);
 rec_arg("HWND","ctl",(uintptr_t)ctl,1);
 rec_arg("BOOL","prev",(uintptr_t)prev,1);
 rec_line();
 static HWND(UIAPI *real)(HWND dlg, HWND ctl, BOOL prev); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetNextDlgTabItem");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetNextDlgTabItem)(dlg,ctl,prev);
 DWORD exit_error=GetLastError();
 rec_end("GetNextDlgTabItem","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HWND UIAPI rec_GetParent(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("GetParent",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HWND(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("USER32.DLL","GetParent");
 SetLastError(entry_error);
 HWND result=(rec_wine?real:ui_GetParent)(h);
 DWORD exit_error=GetLastError();
 rec_end("GetParent","HWND",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_RegisterHotKey(HWND h, INT id, UINT mods, UINT vk) {
 DWORD entry_error=GetLastError(); rec_begin("RegisterHotKey",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","id",(uintptr_t)id,1);
 rec_arg("UINT","mods",(uintptr_t)mods,1);
 rec_arg("UINT","vk",(uintptr_t)vk,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT id, UINT mods, UINT vk); if(rec_wine&&!real)real=rec_real("USER32.DLL","RegisterHotKey");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_RegisterHotKey)(h,id,mods,vk);
 DWORD exit_error=GetLastError();
 rec_end("RegisterHotKey","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_DeleteObject(HGDIOBJ o) {
 DWORD entry_error=GetLastError(); rec_begin("DeleteObject",__builtin_return_address(0));
 rec_arg("HGDIOBJ","o",(uintptr_t)o,1);
 rec_line();
 static BOOL(UIAPI *real)(HGDIOBJ o); if(rec_wine&&!real)real=rec_real("GDI32.DLL","DeleteObject");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_DeleteObject)(o);
 DWORD exit_error=GetLastError();
 rec_end("DeleteObject","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HFONT UIAPI rec_CreateFontIndirectA(const void *lf) {
 DWORD entry_error=GetLastError(); rec_begin("CreateFontIndirectA",__builtin_return_address(0));
 rec_arg("const void *","lf",(uintptr_t)lf,1);
 rec_line();
 static HFONT(UIAPI *real)(const void *lf); if(rec_wine&&!real)real=rec_real("GDI32.DLL","CreateFontIndirectA");
 SetLastError(entry_error);
 HFONT result=(rec_wine?real:ui_CreateFontIndirectA)(lf);
 DWORD exit_error=GetLastError();
 rec_end("CreateFontIndirectA","HFONT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetDeviceCaps(HDC dc, INT i) {
 DWORD entry_error=GetLastError(); rec_begin("GetDeviceCaps",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_line();
 static INT(UIAPI *real)(HDC dc, INT i); if(rec_wine&&!real)real=rec_real("GDI32.DLL","GetDeviceCaps");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetDeviceCaps)(dc,i);
 DWORD exit_error=GetLastError();
 rec_end("GetDeviceCaps","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HGDIOBJ UIAPI rec_SelectObject(HDC dc, HGDIOBJ o) {
 DWORD entry_error=GetLastError(); rec_begin("SelectObject",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("HGDIOBJ","o",(uintptr_t)o,1);
 rec_line();
 static HGDIOBJ(UIAPI *real)(HDC dc, HGDIOBJ o); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SelectObject");
 SetLastError(entry_error);
 HGDIOBJ result=(rec_wine?real:ui_SelectObject)(dc,o);
 DWORD exit_error=GetLastError();
 rec_end("SelectObject","HGDIOBJ",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_SetTextColor(HDC dc, DWORD c) {
 DWORD entry_error=GetLastError(); rec_begin("SetTextColor",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("DWORD","c",(uintptr_t)c,1);
 rec_line();
 static DWORD(UIAPI *real)(HDC dc, DWORD c); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SetTextColor");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_SetTextColor)(dc,c);
 DWORD exit_error=GetLastError();
 rec_end("SetTextColor","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_SetBkColor(HDC dc, DWORD c) {
 DWORD entry_error=GetLastError(); rec_begin("SetBkColor",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("DWORD","c",(uintptr_t)c,1);
 rec_line();
 static DWORD(UIAPI *real)(HDC dc, DWORD c); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SetBkColor");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_SetBkColor)(dc,c);
 DWORD exit_error=GetLastError();
 rec_end("SetBkColor","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_TextOutA(HDC dc, INT x, INT y, const char *s, INT n) {
 DWORD entry_error=GetLastError(); rec_begin("TextOutA",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_arg("const char *","s",(uintptr_t)s,1);
 rec_arg("INT","n",(uintptr_t)n,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, INT x, INT y, const char *s, INT n); if(rec_wine&&!real)real=rec_real("GDI32.DLL","TextOutA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_TextOutA)(dc,x,y,s,n);
 DWORD exit_error=GetLastError();
 rec_end("TextOutA","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HBRUSH UIAPI rec_CreateSolidBrush(DWORD c) {
 DWORD entry_error=GetLastError(); rec_begin("CreateSolidBrush",__builtin_return_address(0));
 rec_arg("DWORD","c",(uintptr_t)c,1);
 rec_line();
 static HBRUSH(UIAPI *real)(DWORD c); if(rec_wine&&!real)real=rec_real("GDI32.DLL","CreateSolidBrush");
 SetLastError(entry_error);
 HBRUSH result=(rec_wine?real:ui_CreateSolidBrush)(c);
 DWORD exit_error=GetLastError();
 rec_end("CreateSolidBrush","HBRUSH",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_GetTextColor(HDC dc) {
 DWORD entry_error=GetLastError(); rec_begin("GetTextColor",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_line();
 static DWORD(UIAPI *real)(HDC dc); if(rec_wine&&!real)real=rec_real("GDI32.DLL","GetTextColor");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_GetTextColor)(dc);
 DWORD exit_error=GetLastError();
 rec_end("GetTextColor","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_SetBkMode(HDC dc, INT m) {
 DWORD entry_error=GetLastError(); rec_begin("SetBkMode",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","m",(uintptr_t)m,1);
 rec_line();
 static INT(UIAPI *real)(HDC dc, INT m); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SetBkMode");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_SetBkMode)(dc,m);
 DWORD exit_error=GetLastError();
 rec_end("SetBkMode","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_GetBkMode(HDC dc) {
 DWORD entry_error=GetLastError(); rec_begin("GetBkMode",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_line();
 static INT(UIAPI *real)(HDC dc); if(rec_wine&&!real)real=rec_real("GDI32.DLL","GetBkMode");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_GetBkMode)(dc);
 DWORD exit_error=GetLastError();
 rec_end("GetBkMode","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HGDIOBJ UIAPI rec_GetStockObject(INT i) {
 DWORD entry_error=GetLastError(); rec_begin("GetStockObject",__builtin_return_address(0));
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_line();
 static HGDIOBJ(UIAPI *real)(INT i); if(rec_wine&&!real)real=rec_real("GDI32.DLL","GetStockObject");
 SetLastError(entry_error);
 HGDIOBJ result=(rec_wine?real:ui_GetStockObject)(i);
 DWORD exit_error=GetLastError();
 rec_end("GetStockObject","HGDIOBJ",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_GetBkColor(HDC dc) {
 DWORD entry_error=GetLastError(); rec_begin("GetBkColor",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_line();
 static DWORD(UIAPI *real)(HDC dc); if(rec_wine&&!real)real=rec_real("GDI32.DLL","GetBkColor");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_GetBkColor)(dc);
 DWORD exit_error=GetLastError();
 rec_end("GetBkColor","DWORD",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_DPtoLP(HDC dc, POINT *p, INT n) {
 DWORD entry_error=GetLastError(); rec_begin("DPtoLP",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_arg("INT","n",(uintptr_t)n,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, POINT *p, INT n); if(rec_wine&&!real)real=rec_real("GDI32.DLL","DPtoLP");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_DPtoLP)(dc,p,n);
 DWORD exit_error=GetLastError();
 rec_end("DPtoLP","BOOL",(uintptr_t)result);
 rec_arg("POINT *","p",(uintptr_t)p,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetWindowOrgEx(HDC dc, INT x, INT y, POINT *old) {
 DWORD entry_error=GetLastError(); rec_begin("SetWindowOrgEx",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_arg("POINT *","old",(uintptr_t)old,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, INT x, INT y, POINT *old); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SetWindowOrgEx");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetWindowOrgEx)(dc,x,y,old);
 DWORD exit_error=GetLastError();
 rec_end("SetWindowOrgEx","BOOL",(uintptr_t)result);
 rec_arg("POINT *","old",(uintptr_t)old,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_RestoreDC(HDC dc, INT i) {
 DWORD entry_error=GetLastError(); rec_begin("RestoreDC",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, INT i); if(rec_wine&&!real)real=rec_real("GDI32.DLL","RestoreDC");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_RestoreDC)(dc,i);
 DWORD exit_error=GetLastError();
 rec_end("RestoreDC","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ModifyWorldTransform(HDC dc, const void *xf, DWORD mode) {
 DWORD entry_error=GetLastError(); rec_begin("ModifyWorldTransform",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("const void *","xf",(uintptr_t)xf,1);
 rec_arg("DWORD","mode",(uintptr_t)mode,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, const void *xf, DWORD mode); if(rec_wine&&!real)real=rec_real("GDI32.DLL","ModifyWorldTransform");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ModifyWorldTransform)(dc,xf,mode);
 DWORD exit_error=GetLastError();
 rec_end("ModifyWorldTransform","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_SetGraphicsMode(HDC dc, INT m) {
 DWORD entry_error=GetLastError(); rec_begin("SetGraphicsMode",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","m",(uintptr_t)m,1);
 rec_line();
 static INT(UIAPI *real)(HDC dc, INT m); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SetGraphicsMode");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_SetGraphicsMode)(dc,m);
 DWORD exit_error=GetLastError();
 rec_end("SetGraphicsMode","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_SetViewportOrgEx(HDC dc, INT x, INT y, POINT *old) {
 DWORD entry_error=GetLastError(); rec_begin("SetViewportOrgEx",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_arg("POINT *","old",(uintptr_t)old,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, INT x, INT y, POINT *old); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SetViewportOrgEx");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_SetViewportOrgEx)(dc,x,y,old);
 DWORD exit_error=GetLastError();
 rec_end("SetViewportOrgEx","BOOL",(uintptr_t)result);
 rec_arg("POINT *","old",(uintptr_t)old,1);
 rec_line();SetLastError(exit_error);return result;
}
static INT UIAPI rec_SaveDC(HDC dc) {
 DWORD entry_error=GetLastError(); rec_begin("SaveDC",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_line();
 static INT(UIAPI *real)(HDC dc); if(rec_wine&&!real)real=rec_real("GDI32.DLL","SaveDC");
 SetLastError(entry_error);
 INT result=(rec_wine?real:ui_SaveDC)(dc);
 DWORD exit_error=GetLastError();
 rec_end("SaveDC","INT",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HBITMAP UIAPI rec_CreateDIBSection(HDC dc, const void *bmi, UINT usage, void **bits, uint32_t sec, DWORD off) {
 DWORD entry_error=GetLastError(); rec_begin("CreateDIBSection",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("const void *","bmi",(uintptr_t)bmi,1);
 rec_arg("UINT","usage",(uintptr_t)usage,1);
 rec_arg("void **","bits",(uintptr_t)bits,1);
 rec_arg("uint32_t","sec",(uintptr_t)sec,1);
 rec_arg("DWORD","off",(uintptr_t)off,1);
 rec_line();
 static HBITMAP(UIAPI *real)(HDC dc, const void *bmi, UINT usage, void **bits, uint32_t sec, DWORD off); if(rec_wine&&!real)real=rec_real("GDI32.DLL","CreateDIBSection");
 SetLastError(entry_error);
 HBITMAP result=(rec_wine?real:ui_CreateDIBSection)(dc,bmi,usage,bits,sec,off);
 DWORD exit_error=GetLastError();
 rec_end("CreateDIBSection","HBITMAP",(uintptr_t)result);
 rec_arg("void **","bits",(uintptr_t)bits,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_GetTextMetricsA(HDC dc, void *tm) {
 DWORD entry_error=GetLastError(); rec_begin("GetTextMetricsA",__builtin_return_address(0));
 rec_arg("HDC","dc",(uintptr_t)dc,1);
 rec_arg("void *","tm",(uintptr_t)tm,1);
 rec_line();
 static BOOL(UIAPI *real)(HDC dc, void *tm); if(rec_wine&&!real)real=rec_real("GDI32.DLL","GetTextMetricsA");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_GetTextMetricsA)(dc,tm);
 DWORD exit_error=GetLastError();
 rec_end("GetTextMetricsA","BOOL",(uintptr_t)result);
 rec_arg("void *","tm",(uintptr_t)tm,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ImageList_DragShowNolock(BOOL show) {
 DWORD entry_error=GetLastError(); rec_begin("ImageList_DragShowNolock",__builtin_return_address(0));
 rec_arg("BOOL","show",(uintptr_t)show,1);
 rec_line();
 static BOOL(UIAPI *real)(BOOL show); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","ImageList_DragShowNolock");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImageList_DragShowNolock)(show);
 DWORD exit_error=GetLastError();
 rec_end("ImageList_DragShowNolock","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static void UIAPI rec_InitCommonControls(void) {
 DWORD entry_error=GetLastError(); rec_begin("InitCommonControls",__builtin_return_address(0));
 rec_line();
 static void(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","InitCommonControls");
 SetLastError(entry_error);
 (rec_wine?real:ui_InitCommonControls)();
 DWORD exit_error=GetLastError();
 rec_end("InitCommonControls","void",0);
 rec_line();SetLastError(exit_error);
}
static BOOL UIAPI rec_ImageList_DragMove(INT x, INT y) {
 DWORD entry_error=GetLastError(); rec_begin("ImageList_DragMove",__builtin_return_address(0));
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_line();
 static BOOL(UIAPI *real)(INT x, INT y); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","ImageList_DragMove");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImageList_DragMove)(x,y);
 DWORD exit_error=GetLastError();
 rec_end("ImageList_DragMove","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ImageList_DragEnter(HWND h, INT x, INT y) {
 DWORD entry_error=GetLastError(); rec_begin("ImageList_DragEnter",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_line();
 static BOOL(UIAPI *real)(HWND h, INT x, INT y); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","ImageList_DragEnter");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImageList_DragEnter)(h,x,y);
 DWORD exit_error=GetLastError();
 rec_end("ImageList_DragEnter","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ImageList_BeginDrag(uint32_t il, INT i, INT x, INT y) {
 DWORD entry_error=GetLastError(); rec_begin("ImageList_BeginDrag",__builtin_return_address(0));
 rec_arg("uint32_t","il",(uintptr_t)il,1);
 rec_arg("INT","i",(uintptr_t)i,1);
 rec_arg("INT","x",(uintptr_t)x,1);
 rec_arg("INT","y",(uintptr_t)y,1);
 rec_line();
 static BOOL(UIAPI *real)(uint32_t il, INT i, INT x, INT y); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","ImageList_BeginDrag");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImageList_BeginDrag)(il,i,x,y);
 DWORD exit_error=GetLastError();
 rec_end("ImageList_BeginDrag","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ImageList_Destroy(uint32_t il) {
 DWORD entry_error=GetLastError(); rec_begin("ImageList_Destroy",__builtin_return_address(0));
 rec_arg("uint32_t","il",(uintptr_t)il,1);
 rec_line();
 static BOOL(UIAPI *real)(uint32_t il); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","ImageList_Destroy");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImageList_Destroy)(il);
 DWORD exit_error=GetLastError();
 rec_end("ImageList_Destroy","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static void UIAPI rec_ImageList_EndDrag(void) {
 DWORD entry_error=GetLastError(); rec_begin("ImageList_EndDrag",__builtin_return_address(0));
 rec_line();
 static void(UIAPI *real)(void); if(rec_wine&&!real)real=rec_real("COMCTL32.DLL","ImageList_EndDrag");
 SetLastError(entry_error);
 (rec_wine?real:ui_ImageList_EndDrag)();
 DWORD exit_error=GetLastError();
 rec_end("ImageList_EndDrag","void",0);
 rec_line();SetLastError(exit_error);
}
static BOOL UIAPI rec_ImmNotifyIME(HIMC c, DWORD a, DWORD i, DWORD v) {
 DWORD entry_error=GetLastError(); rec_begin("ImmNotifyIME",__builtin_return_address(0));
 rec_arg("HIMC","c",(uintptr_t)c,1);
 rec_arg("DWORD","a",(uintptr_t)a,1);
 rec_arg("DWORD","i",(uintptr_t)i,1);
 rec_arg("DWORD","v",(uintptr_t)v,1);
 rec_line();
 static BOOL(UIAPI *real)(HIMC c, DWORD a, DWORD i, DWORD v); if(rec_wine&&!real)real=rec_real("IMM32.DLL","ImmNotifyIME");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImmNotifyIME)(c,a,i,v);
 DWORD exit_error=GetLastError();
 rec_end("ImmNotifyIME","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static HIMC UIAPI rec_ImmGetContext(HWND h) {
 DWORD entry_error=GetLastError(); rec_begin("ImmGetContext",__builtin_return_address(0));
 rec_arg("HWND","h",(uintptr_t)h,1);
 rec_line();
 static HIMC(UIAPI *real)(HWND h); if(rec_wine&&!real)real=rec_real("IMM32.DLL","ImmGetContext");
 SetLastError(entry_error);
 HIMC result=(rec_wine?real:ui_ImmGetContext)(h);
 DWORD exit_error=GetLastError();
 rec_end("ImmGetContext","HIMC",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static LONG UIAPI rec_ImmGetCompositionStringA(HIMC c, DWORD i, void *buf, DWORD n) {
 DWORD entry_error=GetLastError(); rec_begin("ImmGetCompositionStringA",__builtin_return_address(0));
 rec_arg("HIMC","c",(uintptr_t)c,1);
 rec_arg("DWORD","i",(uintptr_t)i,1);
 rec_arg("void *","buf",(uintptr_t)buf,1);
 rec_arg("DWORD","n",(uintptr_t)n,1);
 rec_line();
 static LONG(UIAPI *real)(HIMC c, DWORD i, void *buf, DWORD n); if(rec_wine&&!real)real=rec_real("IMM32.DLL","ImmGetCompositionStringA");
 SetLastError(entry_error);
 LONG result=(rec_wine?real:ui_ImmGetCompositionStringA)(c,i,buf,n);
 DWORD exit_error=GetLastError();
 rec_end("ImmGetCompositionStringA","LONG",(uintptr_t)result);
 rec_arg("void *","buf",(uintptr_t)buf,1);
 rec_line();SetLastError(exit_error);return result;
}
static BOOL UIAPI rec_ImmSetOpenStatus(HIMC c, BOOL open) {
 DWORD entry_error=GetLastError(); rec_begin("ImmSetOpenStatus",__builtin_return_address(0));
 rec_arg("HIMC","c",(uintptr_t)c,1);
 rec_arg("BOOL","open",(uintptr_t)open,1);
 rec_line();
 static BOOL(UIAPI *real)(HIMC c, BOOL open); if(rec_wine&&!real)real=rec_real("IMM32.DLL","ImmSetOpenStatus");
 SetLastError(entry_error);
 BOOL result=(rec_wine?real:ui_ImmSetOpenStatus)(c,open);
 DWORD exit_error=GetLastError();
 rec_end("ImmSetOpenStatus","BOOL",(uintptr_t)result);
 rec_line();SetLastError(exit_error);return result;
}
static DWORD UIAPI rec_ImmGetCandidateListA(HIMC c, DWORD i, void *buf, DWORD n) {
 DWORD entry_error=GetLastError(); rec_begin("ImmGetCandidateListA",__builtin_return_address(0));
 rec_arg("HIMC","c",(uintptr_t)c,1);
 rec_arg("DWORD","i",(uintptr_t)i,1);
 rec_arg("void *","buf",(uintptr_t)buf,1);
 rec_arg("DWORD","n",(uintptr_t)n,1);
 rec_line();
 static DWORD(UIAPI *real)(HIMC c, DWORD i, void *buf, DWORD n); if(rec_wine&&!real)real=rec_real("IMM32.DLL","ImmGetCandidateListA");
 SetLastError(entry_error);
 DWORD result=(rec_wine?real:ui_ImmGetCandidateListA)(c,i,buf,n);
 DWORD exit_error=GetLastError();
 rec_end("ImmGetCandidateListA","DWORD",(uintptr_t)result);
 rec_arg("void *","buf",(uintptr_t)buf,1);
 rec_line();SetLastError(exit_error);return result;
}
static INT UICDECL rec_wsprintfA(char *out,const char *fmt,...) {
 DWORD entry_error=GetLastError();va_list ap;va_start(ap,fmt);rec_begin("wsprintfA",__builtin_return_address(0));rec_arg("const char *","fmt",(uintptr_t)fmt,1);rec_line();
 rec_arg("void *","out",(uintptr_t)out,0);
 va_list copy;va_copy(copy,ap);unsigned index=0;
 for(const char *q=fmt;*q;q++){if(*q!='%')continue;q++;if(*q=='%')continue;while(*q&&strchr("-+#0. 123456789hlwI",*q))q++;if(!*q)break;unsigned value=va_arg(copy,unsigned);rec_add(" arg%u=",index++);if(*q=='s'||*q=='S')rec_string(value);else rec_value("UINT",value);}
 va_end(copy);rec_line();SetLastError(entry_error);
 INT result;if(rec_wine){INT(UIAPI *f)(char*,const char*,va_list)=rec_real("USER32.DLL","wvsprintfA");result=f(out,fmt,ap);}else result=ui_wvsprintfA(out,fmt,ap);
 DWORD exit_error=GetLastError();va_end(ap);rec_end("wsprintfA","INT",result);rec_arg("const char *","out",(uintptr_t)out,1);rec_line();SetLastError(exit_error);return result;
}
static void *rec_import(const char *dll,const char *name,void *fallback) {
 if(!rec_file)return rec_wine?rec_real(dll,name):fallback;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"DefWindowProcA"))return rec_DefWindowProcA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetFocus"))return rec_SetFocus;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetMenu"))return rec_GetMenu;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"WindowFromPoint"))return rec_WindowFromPoint;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetWindowRect"))return rec_GetWindowRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetActiveWindow"))return rec_GetActiveWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ToAscii"))return rec_ToAscii;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ChildWindowFromPointEx"))return rec_ChildWindowFromPointEx;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetCursorPos"))return rec_GetCursorPos;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetKeyState"))return rec_GetKeyState;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"IsWindowEnabled"))return rec_IsWindowEnabled;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetAsyncKeyState"))return rec_GetAsyncKeyState;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"IntersectRect"))return rec_IntersectRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"EndDialog"))return rec_EndDialog;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetDlgItemTextA"))return rec_SetDlgItemTextA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"DialogBoxParamA"))return rec_DialogBoxParamA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"DialogBoxIndirectParamA"))return rec_DialogBoxIndirectParamA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ShowCursor"))return rec_ShowCursor;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetWindowPos"))return rec_SetWindowPos;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"DestroyWindow"))return rec_DestroyWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"MapVirtualKeyA"))return rec_MapVirtualKeyA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetSystemMetrics"))return rec_GetSystemMetrics;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"MoveWindow"))return rec_MoveWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"AdjustWindowRectEx"))return rec_AdjustWindowRectEx;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ReleaseCapture"))return rec_ReleaseCapture;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetCapture"))return rec_GetCapture;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetCapture"))return rec_SetCapture;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetDlgCtrlID"))return rec_GetDlgCtrlID;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ValidateRect"))return rec_ValidateRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"AdjustWindowRect"))return rec_AdjustWindowRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetSysColor"))return rec_GetSysColor;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CreateDialogParamA"))return rec_CreateDialogParamA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"IsDlgButtonChecked"))return rec_IsDlgButtonChecked;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CheckDlgButton"))return rec_CheckDlgButton;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"WaitForInputIdle"))return rec_WaitForInputIdle;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetTopWindow"))return rec_GetTopWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetForegroundWindow"))return rec_GetForegroundWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"LoadIconA"))return rec_LoadIconA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetActiveWindow"))return rec_SetActiveWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"RedrawWindow"))return rec_RedrawWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetWindowContextHelpId"))return rec_GetWindowContextHelpId;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"WinHelpA"))return rec_WinHelpA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ChildWindowFromPoint"))return rec_ChildWindowFromPoint;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"LoadCursorA"))return rec_LoadCursorA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetCursor"))return rec_SetCursor;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"PostQuitMessage"))return rec_PostQuitMessage;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"FindWindowA"))return rec_FindWindowA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetCursorPos"))return rec_SetCursorPos;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CreateDialogIndirectParamA"))return rec_CreateDialogIndirectParamA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetKeyNameTextA"))return rec_GetKeyNameTextA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ScreenToClient"))return rec_ScreenToClient;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"LockWindowUpdate"))return rec_LockWindowUpdate;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetDC"))return rec_GetDC;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CloseWindow"))return rec_CloseWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetFocus"))return rec_GetFocus;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"PeekMessageA"))return rec_PeekMessageA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"MessageBoxIndirectA"))return rec_MessageBoxIndirectA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"UpdateWindow"))return rec_UpdateWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"PostMessageA"))return rec_PostMessageA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetUpdateRect"))return rec_GetUpdateRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ReleaseDC"))return rec_ReleaseDC;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"MessageBoxA"))return rec_MessageBoxA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetWindow"))return rec_GetWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SendDlgItemMessageA"))return rec_SendDlgItemMessageA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetWindowLongA"))return rec_SetWindowLongA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetWindowLongA"))return rec_GetWindowLongA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"EnableWindow"))return rec_EnableWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"InvalidateRect"))return rec_InvalidateRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ShowWindow"))return rec_ShowWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetDlgItem"))return rec_GetDlgItem;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SendMessageA"))return rec_SendMessageA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetRect"))return rec_SetRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"ClientToScreen"))return rec_ClientToScreen;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"TranslateMessage"))return rec_TranslateMessage;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"DispatchMessageA"))return rec_DispatchMessageA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetClientRect"))return rec_GetClientRect;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"BringWindowToTop"))return rec_BringWindowToTop;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CallWindowProcA"))return rec_CallWindowProcA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"KillTimer"))return rec_KillTimer;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CreateWindowExA"))return rec_CreateWindowExA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"RegisterClassA"))return rec_RegisterClassA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetForegroundWindow"))return rec_SetForegroundWindow;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"SetTimer"))return rec_SetTimer;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"IsWindowVisible"))return rec_IsWindowVisible;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"EnumChildWindows"))return rec_EnumChildWindows;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetClassNameA"))return rec_GetClassNameA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"TranslateAcceleratorA"))return rec_TranslateAcceleratorA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetWindowTextA"))return rec_GetWindowTextA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"IsDialogMessageA"))return rec_IsDialogMessageA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"EndPaint"))return rec_EndPaint;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"CharToOemBuffA"))return rec_CharToOemBuffA;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"BeginPaint"))return rec_BeginPaint;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetNextDlgTabItem"))return rec_GetNextDlgTabItem;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"GetParent"))return rec_GetParent;
 if(!strcmp(dll,"USER32.DLL")&&!strcmp(name,"RegisterHotKey"))return rec_RegisterHotKey;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"DeleteObject"))return rec_DeleteObject;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"CreateFontIndirectA"))return rec_CreateFontIndirectA;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"GetDeviceCaps"))return rec_GetDeviceCaps;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SelectObject"))return rec_SelectObject;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SetTextColor"))return rec_SetTextColor;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SetBkColor"))return rec_SetBkColor;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"TextOutA"))return rec_TextOutA;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"CreateSolidBrush"))return rec_CreateSolidBrush;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"GetTextColor"))return rec_GetTextColor;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SetBkMode"))return rec_SetBkMode;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"GetBkMode"))return rec_GetBkMode;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"GetStockObject"))return rec_GetStockObject;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"GetBkColor"))return rec_GetBkColor;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"DPtoLP"))return rec_DPtoLP;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SetWindowOrgEx"))return rec_SetWindowOrgEx;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"RestoreDC"))return rec_RestoreDC;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"ModifyWorldTransform"))return rec_ModifyWorldTransform;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SetGraphicsMode"))return rec_SetGraphicsMode;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SetViewportOrgEx"))return rec_SetViewportOrgEx;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"SaveDC"))return rec_SaveDC;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"CreateDIBSection"))return rec_CreateDIBSection;
 if(!strcmp(dll,"GDI32.DLL")&&!strcmp(name,"GetTextMetricsA"))return rec_GetTextMetricsA;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"ImageList_DragShowNolock"))return rec_ImageList_DragShowNolock;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"InitCommonControls"))return rec_InitCommonControls;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"ImageList_DragMove"))return rec_ImageList_DragMove;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"ImageList_DragEnter"))return rec_ImageList_DragEnter;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"ImageList_BeginDrag"))return rec_ImageList_BeginDrag;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"ImageList_Destroy"))return rec_ImageList_Destroy;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"ImageList_EndDrag"))return rec_ImageList_EndDrag;
 if(!strcmp(dll,"IMM32.DLL")&&!strcmp(name,"ImmNotifyIME"))return rec_ImmNotifyIME;
 if(!strcmp(dll,"IMM32.DLL")&&!strcmp(name,"ImmGetContext"))return rec_ImmGetContext;
 if(!strcmp(dll,"IMM32.DLL")&&!strcmp(name,"ImmGetCompositionStringA"))return rec_ImmGetCompositionStringA;
 if(!strcmp(dll,"IMM32.DLL")&&!strcmp(name,"ImmSetOpenStatus"))return rec_ImmSetOpenStatus;
 if(!strcmp(dll,"IMM32.DLL")&&!strcmp(name,"ImmGetCandidateListA"))return rec_ImmGetCandidateListA;
 if(!strcmp(name,"wsprintfA"))return rec_wsprintfA;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"#17"))return rec_InitCommonControls;
 ui_fatal("recorder has no wrapper for %s!%s",dll,name);return 0;
}
static void rec_init(void) {
 const char *path=getenv("RA2UI_RECORD");const char *mode=getenv("RA2UI_MODE");rec_wine=mode&&!strcmp(mode,"wine");
 if(path&&*path){rec_file=fopen(path,"wb");if(!rec_file)ui_fatal("cannot open RA2UI_RECORD: %s",path);setvbuf(rec_file,0,_IONBF,0);}
}
