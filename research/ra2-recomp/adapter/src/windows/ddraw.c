/* Windows ABI boundary. Pixel state and presentation remain portable C. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra_present.h"

typedef struct Draw {
    IDirectDraw iface;
    ULONG refs;
    DWORD width,height,bits;
    HWND window;
    DWORD cooperative_flags;
    int native_menu_repaint,repainting;
    ra_present *display;
} Draw;
typedef struct Palette {
    IDirectDrawPalette iface;
    ULONG refs;
    DWORD flags;
    PALETTEENTRY entries[256];
} Palette;
typedef struct Clipper {
    IDirectDrawClipper iface;
    ULONG refs;
    HWND window;
} Clipper;
typedef struct Surface {
    IDirectDrawSurface iface;
    ULONG refs;
    Draw *owner;
    ra_surface pixels;
    DWORD caps;
    struct Surface *back;
    Palette *palette;
    Clipper *clipper;
    DDCOLORKEY source_key,dest_key;
    int has_source_key,has_dest_key,locked;
} Surface;
static IDirectDrawVtbl draw_vtable;
static IDirectDrawSurfaceVtbl surface_vtable;
static IDirectDrawPaletteVtbl palette_vtable;
static IDirectDrawClipperVtbl clipper_vtable;
static void fail_method(const char *method,void *caller)
{
    fprintf(stderr,"RA2 adapter: unsupported %s caller=%p\n",method,caller);
    fflush(stderr); abort();
}
#define missing(name) fail_method(name,__builtin_return_address(0))
static HRESULT result(int status)
{
    switch (status) {
    case RA_OK: return DD_OK;
    case RA_NOMEM: return DDERR_OUTOFMEMORY;
    case RA_UNSUPPORTED: return DDERR_UNSUPPORTED;
    default: return DDERR_INVALIDPARAMS;
    }
}
static HRESULT present(Surface *s)
{
    if (!(s->caps&DDSCAPS_PRIMARYSURFACE)) return DD_OK;
    Draw *d=s->owner;
    if (!d->display) {
        d->display=ra_present_open(d->window,s->pixels.width,s->pixels.height);
        if (!d->display) return DDERR_GENERIC;
        if ((d->cooperative_flags&DDSCL_EXCLUSIVE) && ra_present_place_game_window(d->display,s->pixels.width,s->pixels.height)) return DDERR_GENERIC;
    }
    if (s->palette) for (unsigned i=0;i<256;i++) {
        PALETTEENTRY c=s->palette->entries[i];
        s->pixels.palette[i]=((uint32_t)c.peRed<<16)|((uint32_t)c.peGreen<<8)|c.peBlue;
    }
    HRESULT status=result(ra_present_frame(d->display,&s->pixels));
    /* TODO-J14: remove native control painting when portable controls replace it.
       The owner permits this Windows-only bridge during the Wine tests. */
    if (status==DD_OK && d->native_menu_repaint && !d->repainting && d->window) {
        d->repainting=1;
        RedrawWindow(d->window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        d->repainting=0;
    }
    return status;
}
static ULONG WINAPI draw_AddRef(IDirectDraw *iface) { return ++((Draw *)iface)->refs; }
static ULONG WINAPI draw_Release(IDirectDraw *iface)
{
    Draw *d=(Draw *)iface;
    ULONG n=--d->refs;
    if (!n) { ra_present_close(d->display); free(d); }
    return n;
}
static HRESULT WINAPI draw_QueryInterface(IDirectDraw *iface, REFIID id, void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!id) return E_INVALIDARG;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IDirectDraw)) return E_NOINTERFACE;
    *out=iface; draw_AddRef(iface); return DD_OK;
}
static HRESULT WINAPI draw_SetCooperativeLevel(IDirectDraw *iface, HWND window, DWORD flags)
{
    Draw *d=(Draw *)iface;
    d->cooperative_flags=flags;
    if (d->window!=window) { ra_present_close(d->display); d->display=NULL; }
    d->window=window; return DD_OK;
}
static HRESULT WINAPI draw_SetDisplayMode(IDirectDraw *iface, DWORD w,DWORD h,DWORD bits)
{
    Draw *d=(Draw *)iface;
    if (!w || !h || w>16384 || h>16384) return DDERR_INVALIDMODE;
    if (bits!=8 && bits!=16 && bits!=24 && bits!=32) return DDERR_INVALIDMODE;
    ra_present_close(d->display); d->display=NULL;
    d->width=w; d->height=h; d->bits=bits;
    fprintf(stderr,"RA2 mode %lux%lu bits=%lu window=%p cooperative=%lx\n",w,h,bits,d->window,d->cooperative_flags);
    if (d->window && (d->cooperative_flags&DDSCL_EXCLUSIVE)) {
        d->display=ra_present_open(d->window,w,h);
        if (!d->display) return DDERR_GENERIC;
        if (ra_present_place_game_window(d->display,w,h)) return DDERR_GENERIC;
    }
    return DD_OK;
}
static void describe(Surface *s,DDSURFACEDESC *desc)
{
    memset(desc,0,sizeof(*desc));
    desc->dwSize=sizeof(*desc);
    desc->dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH|DDSD_PIXELFORMAT|DDSD_CAPS|DDSD_LPSURFACE;
    desc->dwWidth=s->pixels.width; desc->dwHeight=s->pixels.height;
    desc->lPitch=(LONG)s->pixels.pitch; desc->lpSurface=s->pixels.pixels;
    desc->ddsCaps.dwCaps=s->caps;
    desc->ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);
    desc->ddpfPixelFormat.dwFlags=DDPF_RGB|(s->pixels.bits==8 ? DDPF_PALETTEINDEXED8 : 0);
    desc->ddpfPixelFormat.dwRGBBitCount=s->pixels.bits;
    if (s->pixels.bits!=8) {
        desc->ddpfPixelFormat.dwRBitMask=s->pixels.red_mask;
        desc->ddpfPixelFormat.dwGBitMask=s->pixels.green_mask;
        desc->ddpfPixelFormat.dwBBitMask=s->pixels.blue_mask;
    }
    if (s->back) { desc->dwFlags|=DDSD_BACKBUFFERCOUNT; desc->dwBackBufferCount=1; }
}
static HRESULT WINAPI draw_GetDisplayMode(IDirectDraw *iface,DDSURFACEDESC *desc)
{
    Draw *d=(Draw *)iface;
    Surface s={0};
    if (!desc || desc->dwSize!=sizeof(*desc)) return DDERR_INVALIDPARAMS;
    s.pixels.width=d->width; s.pixels.height=d->height; s.pixels.bits=d->bits;
    s.pixels.pitch=(d->width*(d->bits/8)+3)&~3u;
    s.pixels.red_mask=d->bits==16 ? 0xf800 : 0xff0000;
    s.pixels.green_mask=d->bits==16 ? 0x7e0 : 0xff00;
    s.pixels.blue_mask=d->bits==16 ? 0x1f : 0xff;
    describe(&s,desc); desc->dwFlags&=~DDSD_LPSURFACE; return DD_OK;
}
static HRESULT WINAPI draw_GetCaps(IDirectDraw *iface,DDCAPS *driver,DDCAPS *hel)
{
    (void)iface;
    DDCAPS caps={0};
    caps.dwSize=sizeof(caps);
    caps.dwCaps=DDCAPS_BLT|DDCAPS_BLTCOLORFILL|DDCAPS_BLTSTRETCH|DDCAPS_COLORKEY|DDCAPS_PALETTE;
    caps.dwCKeyCaps=DDCKEYCAPS_SRCBLT|DDCKEYCAPS_DESTBLT;
    caps.dwVidMemTotal=caps.dwVidMemFree=64*1024*1024;
    if (!driver && !hel) return DDERR_INVALIDPARAMS;
    DDCAPS *out[2]={driver,hel};
    for (unsigned i=0;i<2;i++) if (out[i]) {
        DWORD size=out[i]->dwSize;
        if (size<sizeof(DWORD) || size>sizeof(caps)) return DDERR_INVALIDPARAMS;
        memcpy(out[i],&caps,size); out[i]->dwSize=size;
    }
    return DD_OK;
}
static HRESULT WINAPI draw_RestoreDisplayMode(IDirectDraw *iface)
{
    Draw *d=(Draw *)iface; ra_present_close(d->display); d->display=NULL; return DD_OK;
}
static HRESULT WINAPI draw_WaitForVerticalBlank(IDirectDraw *iface,DWORD flags,HANDLE event)
{
    (void)iface; (void)flags; (void)event;
    return DDERR_UNSUPPORTED;
}
static ULONG WINAPI surface_AddRef(IDirectDrawSurface *iface) { return ++((Surface *)iface)->refs; }
static ULONG WINAPI palette_AddRef(IDirectDrawPalette *iface) { return ++((Palette *)iface)->refs; }
static ULONG WINAPI palette_Release(IDirectDrawPalette *iface)
{
    Palette *p=(Palette *)iface; ULONG n=--p->refs; if (!n) free(p); return n;
}
static ULONG WINAPI clipper_AddRef(IDirectDrawClipper *iface) { return ++((Clipper *)iface)->refs; }
static ULONG WINAPI clipper_Release(IDirectDrawClipper *iface)
{
    Clipper *c=(Clipper *)iface; ULONG n=--c->refs; if (!n) free(c); return n;
}
static HRESULT WINAPI clipper_QueryInterface(IDirectDrawClipper *iface,REFIID id,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!id) return E_INVALIDARG;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IDirectDrawClipper)) return E_NOINTERFACE;
    *out=iface; clipper_AddRef(iface); return DD_OK;
}
static HRESULT WINAPI draw_CreateClipper(IDirectDraw *iface,DWORD flags,IDirectDrawClipper **out,IUnknown *outer)
{
    Clipper *c; (void)iface;
    if (!out) return E_POINTER;
    *out=NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (flags) return DDERR_INVALIDPARAMS;
    c=calloc(1,sizeof(*c)); if (!c) return DDERR_OUTOFMEMORY;
    c->iface.lpVtbl=&clipper_vtable; c->refs=1; *out=&c->iface; return DD_OK;
}
static HRESULT WINAPI clipper_SetHWnd(IDirectDrawClipper *iface,DWORD flags,HWND window)
{
    if (flags) return DDERR_INVALIDPARAMS;
    ((Clipper *)iface)->window=window; return DD_OK;
}
static HRESULT WINAPI clipper_GetHWnd(IDirectDrawClipper *iface,HWND *window)
{
    if (!window) return DDERR_INVALIDPARAMS;
    *window=((Clipper *)iface)->window; return DD_OK;
}
static HRESULT WINAPI surface_SetClipper(IDirectDrawSurface *iface,IDirectDrawClipper *clipper)
{
    Surface *s=(Surface *)iface;
    if (clipper) clipper_AddRef(clipper);
    if (s->clipper) clipper_Release(&s->clipper->iface);
    s->clipper=(Clipper *)clipper; return DD_OK;
}
static HRESULT WINAPI surface_GetClipper(IDirectDrawSurface *iface,IDirectDrawClipper **out)
{
    Surface *s=(Surface *)iface;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!s->clipper) return DDERR_NOCLIPPERATTACHED;
    *out=&s->clipper->iface; clipper_AddRef(*out); return DD_OK;
}
static ULONG WINAPI surface_Release(IDirectDrawSurface *iface)
{
    Surface *s=(Surface *)iface; ULONG n=--s->refs;
    if (!n) {
        if (s->back) IDirectDrawSurface_Release(&s->back->iface);
        if (s->clipper) clipper_Release(&s->clipper->iface);
        if (s->palette) palette_Release(&s->palette->iface);
        ra_surface_destroy(&s->pixels); draw_Release(&s->owner->iface); free(s);
    }
    return n;
}
static HRESULT WINAPI surface_QueryInterface(IDirectDrawSurface *iface,REFIID id,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!id) return E_INVALIDARG;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IDirectDrawSurface)) return E_NOINTERFACE;
    *out=iface; surface_AddRef(iface); return DD_OK;
}
static HRESULT WINAPI draw_CreateSurface(IDirectDraw *iface,DDSURFACEDESC *desc,IDirectDrawSurface **out,IUnknown *outer)
{
    Draw *d=(Draw *)iface;
    DWORD width,height,bits;
    Surface *s;
    int status;
    if (!out) return E_POINTER;
    *out=NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!desc || desc->dwSize!=sizeof(*desc)) return DDERR_INVALIDPARAMS;
    if ((desc->dwFlags&DDSD_BACKBUFFERCOUNT) && desc->dwBackBufferCount>1) missing("CreateSurface.multiple_backbuffers");
    if (desc->dwFlags&DDSD_LPSURFACE) missing("CreateSurface.external_memory");
    width=desc->dwFlags&DDSD_WIDTH ? desc->dwWidth : d->width;
    height=desc->dwFlags&DDSD_HEIGHT ? desc->dwHeight : d->height;
    bits=desc->dwFlags&DDSD_PIXELFORMAT ? desc->ddpfPixelFormat.dwRGBBitCount : d->bits;
    s=calloc(1,sizeof(*s)); if (!s) return DDERR_OUTOFMEMORY;
    status=ra_surface_init(&s->pixels,width,height,bits);
    if (status) { free(s); return result(status); }
    if ((desc->dwFlags&DDSD_PIXELFORMAT) && bits!=8) {
        s->pixels.red_mask=desc->ddpfPixelFormat.dwRBitMask;
        s->pixels.green_mask=desc->ddpfPixelFormat.dwGBitMask;
        s->pixels.blue_mask=desc->ddpfPixelFormat.dwBBitMask;
    }
    s->iface.lpVtbl=&surface_vtable; s->refs=1; s->owner=d; s->caps=desc->ddsCaps.dwCaps;
    draw_AddRef(iface);
    if ((desc->dwFlags&DDSD_BACKBUFFERCOUNT) && desc->dwBackBufferCount) {
        DDSURFACEDESC back=*desc;
        IDirectDrawSurface *attached=NULL;
        back.dwFlags&=~DDSD_BACKBUFFERCOUNT;
        back.ddsCaps.dwCaps=DDSCAPS_BACKBUFFER|DDSCAPS_SYSTEMMEMORY;
        HRESULT hr=draw_CreateSurface(iface,&back,&attached,NULL);
        if (FAILED(hr)) { surface_Release(&s->iface); return hr; }
        s->back=(Surface *)attached;
    }
    *out=&s->iface; return DD_OK;
}
static HRESULT WINAPI surface_GetSurfaceDesc(IDirectDrawSurface *iface,DDSURFACEDESC *desc)
{
    if (!desc || desc->dwSize!=sizeof(*desc)) return DDERR_INVALIDPARAMS;
    describe((Surface *)iface,desc); return DD_OK;
}
static HRESULT WINAPI surface_GetPixelFormat(IDirectDrawSurface *iface,DDPIXELFORMAT *format)
{
    DDSURFACEDESC desc;
    if (!format || format->dwSize!=sizeof(*format)) return DDERR_INVALIDPARAMS;
    describe((Surface *)iface,&desc); *format=desc.ddpfPixelFormat; return DD_OK;
}
static HRESULT WINAPI surface_GetCaps(IDirectDrawSurface *iface,DDSCAPS *caps)
{
    if (!caps) return DDERR_INVALIDPARAMS;
    caps->dwCaps=((Surface *)iface)->caps; return DD_OK;
}
static HRESULT WINAPI surface_Lock(IDirectDrawSurface *iface,RECT *rect,DDSURFACEDESC *desc,DWORD flags,HANDLE event)
{
    Surface *s=(Surface *)iface;
    (void)flags; (void)event;
    if (s->locked) return DDERR_SURFACEBUSY;
    if (!desc || desc->dwSize!=sizeof(*desc)) return DDERR_INVALIDPARAMS;
    if (rect && (rect->left<0 || rect->top<0 || rect->right>(LONG)s->pixels.width ||
        rect->bottom>(LONG)s->pixels.height || rect->left>=rect->right || rect->top>=rect->bottom)) return DDERR_INVALIDRECT;
    describe(s,desc);
    if (rect) desc->lpSurface=s->pixels.pixels+(size_t)rect->top*s->pixels.pitch+(size_t)rect->left*(s->pixels.bits/8);
    s->locked=1; return DD_OK;
}
static HRESULT WINAPI surface_Unlock(IDirectDrawSurface *iface,void *data)
{
    Surface *s=(Surface *)iface; (void)data;
    if (!s->locked) return DDERR_NOTLOCKED;
    s->locked=0; return present(s);
}
static ra_rect rect_copy(const RECT *r) { return (ra_rect){r->left,r->top,r->right,r->bottom}; }
static HRESULT WINAPI surface_Blt(IDirectDrawSurface *iface,RECT *dst_rect,IDirectDrawSurface *src_iface,RECT *src_rect,DWORD flags,DDBLTFX *fx)
{
    Surface *d=(Surface *)iface,*s=(Surface *)src_iface;
    ra_rect dr,sr;
    unsigned keys=0;
    int status;
    if (d->locked || (s && s->locked)) return DDERR_SURFACEBUSY;
    if (d->clipper && (!(d->owner->cooperative_flags&DDSCL_EXCLUSIVE) ||
        !(d->owner->cooperative_flags&DDSCL_FULLSCREEN) || d->clipper->window!=d->owner->window))
        missing("Surface.Blt.windowed_clipper_region");
    if (flags&~(DDBLT_WAIT|DDBLT_COLORFILL|DDBLT_KEYSRC|DDBLT_KEYDEST)) missing("Surface.Blt.flags");
    if (dst_rect) dr=rect_copy(dst_rect);
    if (src_rect) sr=rect_copy(src_rect);
    if (flags&DDBLT_COLORFILL) {
        if (!fx || fx->dwSize!=sizeof(*fx)) return DDERR_INVALIDPARAMS;
        status=ra_surface_fill(&d->pixels,dst_rect ? &dr : NULL,fx->dwFillColor);
    } else {
        if (!s) return DDERR_INVALIDPARAMS;
        if (flags&DDBLT_KEYSRC) { if (!s->has_source_key) return DDERR_NOCOLORKEY; keys|=RA_KEY_SOURCE; }
        if (flags&DDBLT_KEYDEST) { if (!d->has_dest_key) return DDERR_NOCOLORKEY; keys|=RA_KEY_DEST; }
        status=ra_surface_blt(&d->pixels,dst_rect ? &dr : NULL,&s->pixels,src_rect ? &sr : NULL,keys,
            s->source_key.dwColorSpaceLowValue,s->source_key.dwColorSpaceHighValue,
            d->dest_key.dwColorSpaceLowValue,d->dest_key.dwColorSpaceHighValue);
    }
    return status ? result(status) : present(d);
}
static HRESULT WINAPI surface_BltFast(IDirectDrawSurface *iface,DWORD x,DWORD y,IDirectDrawSurface *src_iface,RECT *source,DWORD flags)
{
    Surface *s=(Surface *)src_iface;
    RECT sr,dr;
    DWORD translated=0;
    if (!s || x>INT32_MAX || y>INT32_MAX) return DDERR_INVALIDPARAMS;
    if (flags&~(DDBLTFAST_WAIT|DDBLTFAST_SRCCOLORKEY|DDBLTFAST_DESTCOLORKEY)) missing("Surface.BltFast.flags");
    sr=source ? *source : (RECT){0,0,(LONG)s->pixels.width,(LONG)s->pixels.height};
    if (sr.left<0 || sr.top<0 || sr.right>(LONG)s->pixels.width || sr.bottom>(LONG)s->pixels.height ||
        sr.right<=sr.left || sr.bottom<=sr.top || (uint64_t)x+sr.right-sr.left>INT32_MAX ||
        (uint64_t)y+sr.bottom-sr.top>INT32_MAX) return DDERR_INVALIDRECT;
    dr=(RECT){(LONG)x,(LONG)y,(LONG)(x+sr.right-sr.left),(LONG)(y+sr.bottom-sr.top)};
    if (flags&DDBLTFAST_SRCCOLORKEY) translated|=DDBLT_KEYSRC;
    if (flags&DDBLTFAST_DESTCOLORKEY) translated|=DDBLT_KEYDEST;
    return surface_Blt(iface,&dr,src_iface,&sr,translated,NULL);
}
static HRESULT WINAPI surface_SetColorKey(IDirectDrawSurface *iface,DWORD flags,DDCOLORKEY *key)
{
    Surface *s=(Surface *)iface;
    if (flags==DDCKEY_SRCBLT) { s->has_source_key=key!=NULL; if (key) s->source_key=*key; }
    else if (flags==DDCKEY_DESTBLT) { s->has_dest_key=key!=NULL; if (key) s->dest_key=*key; }
    else missing("Surface.SetColorKey.flags");
    return DD_OK;
}
static HRESULT WINAPI surface_GetAttachedSurface(IDirectDrawSurface *iface,DDSCAPS *caps,IDirectDrawSurface **out)
{
    Surface *s=(Surface *)iface;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!caps) return DDERR_INVALIDPARAMS;
    if (!s->back || !(caps->dwCaps&DDSCAPS_BACKBUFFER)) return DDERR_NOTFOUND;
    *out=&s->back->iface; surface_AddRef(*out); return DD_OK;
}
static HRESULT WINAPI surface_Flip(IDirectDrawSurface *iface,IDirectDrawSurface *target,DWORD flags)
{
    Surface *s=(Surface *)iface,*b=target ? (Surface *)target : s->back;
    unsigned char *tmp;
    if (flags&~DDFLIP_WAIT) missing("Surface.Flip.flags");
    if (!b) return DDERR_NOTFLIPPABLE;
    if (s->locked || b->locked) return DDERR_SURFACEBUSY;
    if (s->pixels.width!=b->pixels.width || s->pixels.height!=b->pixels.height || s->pixels.bits!=b->pixels.bits) return DDERR_INVALIDPARAMS;
    tmp=s->pixels.pixels; s->pixels.pixels=b->pixels.pixels; b->pixels.pixels=tmp; return present(s);
}
static HRESULT WINAPI surface_IsLost(IDirectDrawSurface *iface) { (void)iface; return DD_OK; }
static HRESULT WINAPI surface_Restore(IDirectDrawSurface *iface) { (void)iface; return DD_OK; }
static HRESULT WINAPI surface_GetBltStatus(IDirectDrawSurface *iface,DWORD flags) { (void)iface; (void)flags; return DD_OK; }
static HRESULT WINAPI surface_GetFlipStatus(IDirectDrawSurface *iface,DWORD flags) { (void)iface; (void)flags; return DD_OK; }
static HRESULT WINAPI palette_QueryInterface(IDirectDrawPalette *iface,REFIID id,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!id) return E_INVALIDARG;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IDirectDrawPalette)) return E_NOINTERFACE;
    *out=iface; palette_AddRef(iface); return DD_OK;
}
static HRESULT WINAPI draw_CreatePalette(IDirectDraw *iface,DWORD flags,PALETTEENTRY *entries,IDirectDrawPalette **out,IUnknown *outer)
{
    Palette *p;
    (void)iface;
    if (!out) return E_POINTER;
    *out=NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!entries) return DDERR_INVALIDPARAMS;
    if (!(flags&DDPCAPS_8BIT)) return DDERR_UNSUPPORTED;
    p=calloc(1,sizeof(*p)); if (!p) return DDERR_OUTOFMEMORY;
    p->iface.lpVtbl=&palette_vtable; p->refs=1; p->flags=flags;
    memcpy(p->entries,entries,sizeof(p->entries)); *out=&p->iface; return DD_OK;
}
static HRESULT WINAPI palette_GetCaps(IDirectDrawPalette *iface,DWORD *caps)
{
    if (!caps) return DDERR_INVALIDPARAMS;
    *caps=((Palette *)iface)->flags; return DD_OK;
}
static HRESULT WINAPI palette_SetEntries(IDirectDrawPalette *iface,DWORD flags,DWORD start,DWORD count,PALETTEENTRY *entries)
{
    (void)flags;
    if (!entries || start>256 || count>256-start) return DDERR_INVALIDPARAMS;
    memcpy(((Palette *)iface)->entries+start,entries,count*sizeof(*entries)); return DD_OK;
}
static HRESULT WINAPI palette_GetEntries(IDirectDrawPalette *iface,DWORD flags,DWORD start,DWORD count,PALETTEENTRY *entries)
{
    (void)flags;
    if (!entries || start>256 || count>256-start) return DDERR_INVALIDPARAMS;
    memcpy(entries,((Palette *)iface)->entries+start,count*sizeof(*entries)); return DD_OK;
}
static HRESULT WINAPI surface_SetPalette(IDirectDrawSurface *iface,IDirectDrawPalette *palette)
{
    Surface *s=(Surface *)iface;
    if (palette) palette_AddRef(palette);
    if (s->palette) palette_Release(&s->palette->iface);
    s->palette=(Palette *)palette; return DD_OK;
}
static HRESULT WINAPI surface_GetPalette(IDirectDrawSurface *iface,IDirectDrawPalette **out)
{
    Surface *s=(Surface *)iface;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!s->palette) return DDERR_NOPALETTEATTACHED;
    *out=&s->palette->iface; palette_AddRef(*out); return DD_OK;
}
#include "ddraw_slots.inc"
HRESULT WINAPI DirectDrawCreate(GUID *guid,IDirectDraw **out,IUnknown *outer)
{
    Draw *d; (void)guid;
    if (!out) return E_POINTER;
    *out=NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    d=calloc(1,sizeof(*d)); if (!d) return DDERR_OUTOFMEMORY;
    d->iface.lpVtbl=&draw_vtable; d->refs=1; d->width=640; d->height=480; d->bits=16;
    const char *repaint=SDL_getenv("RA2_NATIVE_MENU_REPAINT");
    d->native_menu_repaint=repaint && !strcmp(repaint,"1");
    *out=&d->iface; return DD_OK;
}
