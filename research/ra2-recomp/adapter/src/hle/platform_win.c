/* Windows-only interoperability while the original game owns its HWND. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "ra_present.h"
static HWND window;
static DWORD cooperative;
static ra_present *display;
static int presenting,width,height,native_ui;
static void open_display(void)
{
    if (!window || !width || !height) return;
    LONG_PTR proc=GetWindowLongPtrA(window,GWLP_WNDPROC);
    display=ra_present_open(window,(uint32_t)width,(uint32_t)height);
    if (!display) hle_fatal("SDL display: %s",SDL_GetError());
    if (native_ui && proc) SetWindowLongPtrA(window,GWLP_WNDPROC,proc);
    if (cooperative&DDSCL_EXCLUSIVE) ra_present_place_game_window(display,(uint32_t)width,(uint32_t)height);
}
void ra_hle_native_before(const char *name,const uint32_t *stack)
{
    if (strstr(name,"::SetCooperativeLevel") && !strncmp(name,"IDirectDraw",11)) {
        HWND next=(HWND)(uintptr_t)stack[2];
        if (window!=next) { ra_present_close(display); display=NULL; }
        window=next; cooperative=stack[3];
        const char *value=SDL_getenv("RA2_NATIVE_MENU_REPAINT"); native_ui=value && !strcmp(value,"1");
    }
}
void ra_hle_native_after(const char *name,const uint32_t *stack) { (void)name; (void)stack; }
void hle_set_screen_size(int w,int h)
{
    ra_present_close(display); display=NULL; width=w; height=h;
    if (window) open_display();
}
void hle_screen_desktop(int *w,int *h) { *w=GetSystemMetrics(SM_CXSCREEN); *h=GetSystemMetrics(SM_CYSCREEN); }
void ra_hle_present(void)
{
    if (presenting || !window) return;
    const uint8_t *pixels; const uint32_t *palette;
    int w,h,pitch,bits; unsigned seq;
    if (!hle_dd_frame(&pixels,&w,&h,&pitch,&bits,&palette,&seq)) return;
    (void)seq;
    presenting=1;
    if (!display || width!=w || height!=h) { hle_set_screen_size(w,h); }
    ra_surface frame={0}; frame.width=(uint32_t)w; frame.height=(uint32_t)h; frame.pitch=(uint32_t)pitch; frame.bits=(uint32_t)bits; frame.pixels=(unsigned char *)pixels;
    frame.red_mask=bits==16 ? 0xf800 : 0xff0000; frame.green_mask=bits==16 ? 0x7e0 : 0xff00; frame.blue_mask=bits==16 ? 0x1f : 0xff;
    if (palette) for (unsigned i=0;i<256;i++) { uint32_t v=palette[i]; frame.palette[i]=((v&255)<<16)|(v&0xff00)|((v>>16)&255); }
    if (ra_present_frame(display,&frame)) hle_fatal("SDL presentation failed");
    /* TODO-J14: owner-approved native control repaint and procedure bridge. */
    if (native_ui) {
        RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        if (ra_present_frame(display,&frame)) hle_fatal("SDL native menu presentation failed");
    }
    presenting=0;
}
int ra_hle_native_clip(uint32_t raw_window,uint32_t filter,uint32_t output,uint32_t size_address)
{
    HWND hwnd=(HWND)(uintptr_t)raw_window;
    if (!size_address) return (int)DDERR_INVALIDPARAMS;
    if (!hwnd) return (int)DDERR_NOCLIPLIST;
    HDC dc=GetDC(hwnd); if (!dc) return (int)DDERR_GENERIC;
    HRGN region=CreateRectRgn(0,0,0,0);
    if (!region) { ReleaseDC(hwnd,dc); return (int)DDERR_OUTOFMEMORY; }
    int valid=GetRandomRgn(dc,region,4); ReleaseDC(hwnd,dc);
    if (valid!=1) { DeleteObject(region); return (int)DDERR_GENERIC; }
    if (filter) {
        HRGN selected=CreateRectRgnIndirect((RECT *)hle_ptr(filter));
        if (!selected) { DeleteObject(region); return (int)DDERR_OUTOFMEMORY; }
        CombineRgn(region,region,selected,RGN_AND); DeleteObject(selected);
    }
    DWORD required=GetRegionData(region,0,NULL),available=MEM32(size_address);
    MEM32(size_address)=required;
    HRESULT result=DD_OK;
    if (output && available<required) result=DDERR_REGIONTOOSMALL;
    else if (output && !GetRegionData(region,available,(RGNDATA *)hle_ptr(output))) result=DDERR_GENERIC;
    DeleteObject(region); return (int)result;
}
uint32_t hle_gdi_surface_dc(uint8_t *pixels,int w,int h,int pitch,int bits)
{ (void)pixels; (void)w; (void)h; (void)pitch; (void)bits; hle_fatal("native surface GetDC needs the J14 GDI bridge"); return 0; }
void hle_gdi_release_dc(uint32_t dc) { (void)dc; hle_fatal("unregistered surface HDC"); }
int hle_gdi_window_frame(const uint8_t **px,int *w,int *h,int *pitch,int *bpp,unsigned *seq)
{ (void)px; (void)w; (void)h; (void)pitch; (void)bpp; (void)seq; hle_fatal("BinkBuffer needs the J14 GDI bridge"); return 0; }
void hle_gdi_window_touched(void) { hle_fatal("BinkBuffer window bridge not registered"); }
int ra_hle_nanosleep(const struct timespec *delay,struct timespec *remaining)
{ if (remaining) memset(remaining,0,sizeof(*remaining)); SDL_Delay((Uint32)delay->tv_sec*1000+(Uint32)(delay->tv_nsec+999999)/1000000); return 0; }
