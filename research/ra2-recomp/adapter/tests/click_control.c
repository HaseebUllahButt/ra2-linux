#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
static const char *wanted;
static HWND target;
static int same_caption(const char *a,const char *b)
{
    if (!strncmp(a,"GUI:",4)) a+=4;
    if (!strncmp(b,"GUI:",4)) b+=4;
    for (;;) {
        while (*a && !isalnum((unsigned char)*a)) a++;
        while (*b && !isalnum((unsigned char)*b)) b++;
        if (tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0;
        if (!*a) return 1;
        a++; b++;
    }
}
static BOOL CALLBACK find_control(HWND window,LPARAM unused)
{
    char title[256]; (void)unused;
    if (IsWindowVisible(window) && IsWindowEnabled(window) && GetWindowTextA(window,title,sizeof(title)) && same_caption(title,wanted)) { target=window; return FALSE; }
    return TRUE;
}
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    wanted=argv[1];
    for (unsigned attempt=0;attempt<240;attempt++) {
        HWND root=FindWindowA("Red Alert 2",NULL);
        if (root) { EnumChildWindows(root,find_control,0); if (target) { SetForegroundWindow(root); break; } }
        if (root && !strcmp(wanted,"GUI:SinglePlayer") && attempt%10==0) {
            keybd_event(VK_ESCAPE,(BYTE)MapVirtualKeyA(VK_ESCAPE,MAPVK_VK_TO_VSC),0,0);
            Sleep(50);
            keybd_event(VK_ESCAPE,(BYTE)MapVirtualKeyA(VK_ESCAPE,MAPVK_VK_TO_VSC),KEYEVENTF_KEYUP,0);
        }
        Sleep(500);
    }
    if (!target) { fprintf(stderr,"Control not found: %s\n",wanted); return 1; }
    RECT rect; GetWindowRect(target,&rect);
    SetCursorPos((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
    Sleep(150); mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);
    Sleep(100); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
    printf("CLICK %s hwnd=%p\n",wanted,target); return 0;
}
