#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
int main(void)
{
    HMODULE module=LoadLibraryA(".\\ddraw.dll");
    HRESULT (WINAPI *create)(GUID *,IDirectDraw **,IUnknown *);
    IDirectDraw *draw;
    IDirectDrawSurface *primary;
    DDSURFACEDESC desc={0};
    DDBLTFX fx={0};
    WNDCLASSA wc={0};
    wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="AdapterDisplayTest";
    assert(RegisterClassA(&wc));
    HWND window=CreateWindowA(wc.lpszClassName,"Adapter display test",WS_POPUP|WS_VISIBLE,120,80,640,480,NULL,NULL,wc.hInstance,NULL);
    assert(window && module);
    create=(void *)GetProcAddress(module,"DirectDrawCreate"); assert(create);
    assert(create(NULL,&draw,NULL)==DD_OK);
    assert(IDirectDraw_SetCooperativeLevel(draw,window,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)==DD_OK);
    assert(IDirectDraw_SetDisplayMode(draw,640,480,16)==DD_OK);
    POINT origin={0,0}; RECT client;
    assert(ClientToScreen(window,&origin));
    assert(GetClientRect(window,&client));
    fprintf(stderr,"Game window origin=%ld,%ld client=%ldx%ld\n",origin.x,origin.y,client.right,client.bottom);
    assert(origin.x==0 && origin.y==0 && client.right==640 && client.bottom==480);
    desc.dwSize=sizeof(desc); desc.dwFlags=DDSD_CAPS; desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
    assert(IDirectDraw_CreateSurface(draw,&desc,&primary,NULL)==DD_OK);
    fx.dwSize=sizeof(fx); fx.dwFillColor=0xf800;
    assert(IDirectDrawSurface_Blt(primary,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)==DD_OK);
    assert(IDirectDrawSurface_Release(primary)==0);
    assert(IDirectDraw_Release(draw)==0);
    DestroyWindow(window); FreeLibrary(module);
    puts("PASS: exclusive window origin, client size, and primary presentation");
    return 0;
}
