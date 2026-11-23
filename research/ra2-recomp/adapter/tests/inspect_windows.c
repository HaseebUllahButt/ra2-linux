#include <windows.h>
#include <stdio.h>
static BOOL CALLBACK child(HWND window,LPARAM level)
{
    char cls[128],title[256]; RECT r; DWORD pid;
    GetClassNameA(window,cls,sizeof(cls)); GetWindowTextA(window,title,sizeof(title)); GetWindowRect(window,&r); GetWindowThreadProcessId(window,&pid);
    printf("%s hwnd=%p pid=%lu visible=%d rect=%ld,%ld,%ld,%ld class=%s title=%s\n",level ? "CHILD" : "TOP",window,pid,IsWindowVisible(window),r.left,r.top,r.right,r.bottom,cls,title);
    return TRUE;
}
static BOOL CALLBACK top(HWND window,LPARAM unused)
{ (void)unused; child(window,0); EnumChildWindows(window,child,1); return TRUE; }
int main(void) { EnumWindows(top,0); return 0; }
