#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    HMODULE module=LoadLibraryA("ddraw.dll");
    HRESULT (WINAPI *create)(GUID *,IDirectDraw **,IUnknown *);
    IDirectDraw *draw;
    IDirectDrawSurface *primary;
    DDSURFACEDESC desc={0},locked={0};
    WNDCLASSA wc={0};
    wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="AdapterColorTest";
    assert(RegisterClassA(&wc));
    HWND window=CreateWindowA(wc.lpszClassName,"Adapter color test",WS_POPUP|WS_VISIBLE,0,0,1024,768,NULL,NULL,wc.hInstance,NULL);
    assert(window && module);
    create=(void *)GetProcAddress(module,"DirectDrawCreate"); assert(create);
    assert(create(NULL,&draw,NULL)==DD_OK);
    assert(IDirectDraw_SetCooperativeLevel(draw,window,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)==DD_OK);
    assert(IDirectDraw_SetDisplayMode(draw,1024,768,16)==DD_OK);
    desc.dwSize=sizeof(desc); desc.dwFlags=DDSD_CAPS; desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
    assert(IDirectDraw_CreateSurface(draw,&desc,&primary,NULL)==DD_OK);
    locked.dwSize=sizeof(locked);
    assert(IDirectDrawSurface_Lock(primary,NULL,&locked,DDLOCK_WAIT,NULL)==DD_OK);
    fprintf(stderr,"Pattern pitch=%ld size=%lux%lu bits=%lu masks=%lx,%lx,%lx\n",locked.lPitch,locked.dwWidth,locked.dwHeight,
        locked.ddpfPixelFormat.dwRGBBitCount,locked.ddpfPixelFormat.dwRBitMask,locked.ddpfPixelFormat.dwGBitMask,locked.ddpfPixelFormat.dwBBitMask);
    assert(locked.dwWidth==1024 && locked.dwHeight==768 && locked.ddpfPixelFormat.dwRGBBitCount==16);
    for (unsigned y=0;y<768;y++) for (unsigned x=0;x<1024;x++) {
        uint16_t v=(uint16_t)(((x&31)<<11)|((y&63)<<5)|((x>>5)&31));
        *(uint16_t *)((unsigned char *)locked.lpSurface+y*locked.lPitch+x*2)=v;
    }
    assert(IDirectDrawSurface_Unlock(primary,locked.lpSurface)==DD_OK);
    /* Let Wine map the window and process its first paint before capture. */
    for (unsigned frame=0;frame<5;frame++) {
        MSG message;
        while (PeekMessageA(&message,NULL,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageA(&message);
        }
        Sleep(100);
    }
    assert(IDirectDrawSurface_Lock(primary,NULL,&locked,DDLOCK_WAIT,NULL)==DD_OK);
    assert(IDirectDrawSurface_Unlock(primary,locked.lpSurface)==DD_OK);
    Sleep(200);
    puts("READY"); fflush(stdout);
    Sleep(3000);
    assert(IDirectDrawSurface_Release(primary)==0);
    assert(IDirectDraw_Release(draw)==0);
    DestroyWindow(window); FreeLibrary(module);
    puts("PASS: all 65536 RGB565 pixel values presented"); return 0;
}
