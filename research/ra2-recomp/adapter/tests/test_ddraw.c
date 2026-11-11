#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(void)
{
    HMODULE module=LoadLibraryA(".\\ddraw.dll");
    HRESULT (WINAPI *create)(GUID *,IDirectDraw **,IUnknown *);
    IDirectDraw *draw;
    IDirectDrawSurface *a,*b,*primary,*back;
    IDirectDrawPalette *palette;
    IDirectDrawClipper *clipper,*attached_clipper;
    HWND clip_window;
    DDSURFACEDESC desc={0},locked={0};
    DDBLTFX fx={0};
    DDCOLORKEY key={0x7e0,0x7e0};
    PALETTEENTRY entries[256]={{0}};
    void *same=NULL;
    assert(module);
    create=(void *)GetProcAddress(module,"DirectDrawCreate");
    assert(create);
    assert(create(NULL,&draw,NULL)==DD_OK);
    assert(IDirectDraw_QueryInterface(draw,&IID_IUnknown,&same)==DD_OK && same==draw);
    assert(IDirectDraw_Release((IDirectDraw *)same)==1);
    assert(IDirectDraw_SetDisplayMode(draw,32,24,16)==DD_OK);
    desc.dwSize=sizeof(desc);
    assert(IDirectDraw_GetDisplayMode(draw,&desc)==DD_OK && desc.dwWidth==32 && desc.dwHeight==24);
    memset(&desc,0,sizeof(desc));
    desc.dwSize=sizeof(desc); desc.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_CAPS;
    desc.dwWidth=4; desc.dwHeight=3; desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
    assert(IDirectDraw_CreateSurface(draw,&desc,&a,NULL)==DD_OK);
    assert(IDirectDraw_CreateSurface(draw,&desc,&b,NULL)==DD_OK);
    assert(IDirectDraw_CreateClipper(draw,0,&clipper,NULL)==DD_OK);
    assert(IDirectDrawClipper_SetHWnd(clipper,0,(HWND)(uintptr_t)0x1234)==DD_OK);
    assert(IDirectDrawClipper_GetHWnd(clipper,&clip_window)==DD_OK && clip_window==(HWND)(uintptr_t)0x1234);
    assert(IDirectDrawSurface_SetClipper(a,clipper)==DD_OK);
    assert(IDirectDrawSurface_GetClipper(a,&attached_clipper)==DD_OK && attached_clipper==clipper);
    assert(IDirectDrawClipper_Release(attached_clipper)==2);
    assert(IDirectDrawSurface_SetClipper(a,NULL)==DD_OK);
    assert(IDirectDrawClipper_Release(clipper)==0);
    fx.dwSize=sizeof(fx); fx.dwFillColor=0x7e0;
    assert(IDirectDrawSurface_Blt(a,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)==DD_OK);
    fx.dwFillColor=0xf800;
    assert(IDirectDrawSurface_Blt(b,NULL,NULL,NULL,DDBLT_COLORFILL,&fx)==DD_OK);
    assert(IDirectDrawSurface_SetColorKey(a,DDCKEY_SRCBLT,&key)==DD_OK);
    assert(IDirectDrawSurface_BltFast(b,0,0,a,NULL,DDBLTFAST_SRCCOLORKEY)==DD_OK);
    locked.dwSize=sizeof(locked);
    assert(IDirectDrawSurface_Lock(b,NULL,&locked,0,NULL)==DD_OK);
    assert(*(unsigned short *)locked.lpSurface==0xf800);
    assert(IDirectDrawSurface_Unlock(b,NULL)==DD_OK);
    assert(IDirectDrawSurface_BltFast(b,0,0,a,NULL,0)==DD_OK);
    assert(IDirectDrawSurface_Lock(b,NULL,&locked,0,NULL)==DD_OK);
    assert(*(unsigned short *)locked.lpSurface==0x7e0);
    assert(IDirectDrawSurface_Unlock(b,NULL)==DD_OK);
    entries[1].peRed=123;
    assert(IDirectDraw_CreatePalette(draw,DDPCAPS_8BIT|DDPCAPS_ALLOW256,entries,&palette,NULL)==DD_OK);
    memset(entries,0,sizeof(entries));
    assert(IDirectDrawPalette_GetEntries(palette,0,0,256,entries)==DD_OK && entries[1].peRed==123);
    assert(IDirectDrawSurface_SetPalette(a,palette)==DD_OK);
    assert(IDirectDrawPalette_Release(palette)==1);
    /* Create and release a flip chain without opening a display. */
    memset(&desc,0,sizeof(desc)); desc.dwSize=sizeof(desc);
    desc.dwFlags=DDSD_CAPS|DDSD_BACKBUFFERCOUNT;
    desc.dwBackBufferCount=1; desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP|DDSCAPS_COMPLEX;
    assert(IDirectDraw_CreateSurface(draw,&desc,&primary,NULL)==DD_OK);
    DDSCAPS caps={DDSCAPS_BACKBUFFER};
    assert(IDirectDrawSurface_GetAttachedSurface(primary,&caps,&back)==DD_OK);
    assert(IDirectDrawSurface_Release(back)==1);
    assert(IDirectDrawSurface_Release(primary)==0);
    assert(IDirectDrawSurface_Release(a)==0);
    assert(IDirectDrawSurface_Release(b)==0);
    assert(IDirectDraw_Release(draw)==0);
    FreeLibrary(module);
    puts("PASS: Windows DLL load, COM, mode, surfaces, lock, fill, copy, key, palette, flip-chain ownership");
    return 0;
}
