/* Test-only observer. Both oracle runs use the same seed and measured FPS.
   The original game executable on disk stays unchanged. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static HRESULT (WINAPI *real_create)(GUID *,IDirectDraw **,IUnknown *);
static HRESULT (WINAPI *real_surface)(IDirectDraw *,DDSURFACEDESC *,IDirectDrawSurface **,IUnknown *);
static HRESULT (WINAPI *real_blt)(IDirectDrawSurface *,RECT *,IDirectDrawSurface *,RECT *,DWORD,DDBLTFX *);
static HRESULT (WINAPI *real_fast)(IDirectDrawSurface *,DWORD,DWORD,IDirectDrawSurface *,RECT *,DWORD);
static HRESULT (WINAPI *real_unlock)(IDirectDrawSurface *,void *);
static char directory[1024];
static unsigned char *pending;
static DWORD pending_frame,pending_width,pending_height,first_frame=200,count=20,written;
static FILE *manifest;
static int busy;
static void trace(const char *message)
{
    char file[1200]; snprintf(file,sizeof(file),"%s\\observer.log",directory);
    FILE *f=fopen(file,"a"); if (f) { fprintf(f,"%s frame=%lu\n",message,*(volatile DWORD *)0xa40d2c); fclose(f); }
}
static void stop(const char *message)
{ trace(message); fprintf(stderr,"CAPTURE FAIL: %s\n",message); fflush(stderr); ExitProcess(2); }
static void patch(void *slot,const void *value)
{
    DWORD old;
    if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old)) stop("protect vtable");
    memcpy(slot,&value,sizeof(value)); VirtualProtect(slot,sizeof(void *),old,&old);
}
#include "auto_menu.inc"
static void inputs(void)
{
    if (*(volatile DWORD *)0xa40d2c==0) *(volatile DWORD *)0xa40d40=0x12345678;
    *(volatile DWORD *)0xa6eb1c=20; *(volatile BYTE *)0xa6eb28=0;
}
static void flush_pending(void)
{
    if (!pending) return;
    char file[1200],name[80];
    snprintf(name,sizeof(name),"frame_%06lu.rgb565",pending_frame);
    snprintf(file,sizeof(file),"%s\\%s",directory,name);
    FILE *f=fopen(file,"wb"); if (!f) stop("open frame");
    size_t bytes=(size_t)pending_width*pending_height*2;
    if (fwrite(pending,1,bytes,f)!=bytes || fclose(f)) stop("write frame");
    if (written) fputs(",\n",manifest);
    fprintf(manifest,"{\"frame\":%lu,\"width\":%lu,\"height\":%lu,\"bits\":16,\"file\":\"%s\"}",pending_frame,pending_width,pending_height,name);
    written++; free(pending); pending=NULL;
    if (written==count) {
        fputs("\n]\n",manifest); fclose(manifest);
        fprintf(stderr,"CAPTURE PASS: %lu frames seed=%08lx\n",written,*(volatile DWORD *)0xa40d3c);
        fflush(stderr); ExitProcess(0);
    }
}
static void observe(IDirectDrawSurface *surface)
{
    inputs();
    if (busy) return;
    DWORD frame=*(volatile DWORD *)0xa40d2c;
    if (pending && frame!=pending_frame) flush_pending();
    if (frame<first_frame || frame>=first_frame+count) return;
    if (*(volatile DWORD *)0xa40d3c!=0x12345678) stop("game did not use the fixed seed");
    DDSURFACEDESC desc={0}; desc.dwSize=sizeof(desc);
    if (IDirectDrawSurface_GetSurfaceDesc(surface,&desc)!=DD_OK || !(desc.ddsCaps.dwCaps&DDSCAPS_PRIMARYSURFACE)) return;
    busy=1;
    DDSURFACEDESC locked={0}; locked.dwSize=sizeof(locked);
    if (IDirectDrawSurface_Lock(surface,NULL,&locked,DDLOCK_WAIT|DDLOCK_READONLY,NULL)!=DD_OK) { busy=0; return; }
    if (locked.ddpfPixelFormat.dwRGBBitCount!=16 || locked.ddpfPixelFormat.dwRBitMask!=0xf800 || locked.ddpfPixelFormat.dwGBitMask!=0x7e0 || locked.ddpfPixelFormat.dwBBitMask!=0x1f || locked.lPitch<(LONG)locked.dwWidth*2) stop("not RGB565");
    size_t bytes=(size_t)locked.dwWidth*locked.dwHeight*2;
    if (pending && (locked.dwWidth!=pending_width || locked.dwHeight!=pending_height)) stop("frame dimensions changed");
    if (!pending) { pending=malloc(bytes); if (!pending) stop("allocate frame"); }
    for (DWORD y=0;y<locked.dwHeight;y++) memcpy(pending+(size_t)y*locked.dwWidth*2,(unsigned char *)locked.lpSurface+(size_t)y*locked.lPitch,(size_t)locked.dwWidth*2);
    pending_frame=frame; pending_width=locked.dwWidth; pending_height=locked.dwHeight;
    real_unlock(surface,locked.lpSurface); busy=0;
}
static HRESULT WINAPI hook_blt(IDirectDrawSurface *s,RECT *dr,IDirectDrawSurface *source,RECT *sr,DWORD flags,DDBLTFX *fx)
{ inputs(); HRESULT hr=real_blt(s,dr,source,sr,flags,fx); if (SUCCEEDED(hr)) observe(s); return hr; }
static HRESULT WINAPI hook_fast(IDirectDrawSurface *s,DWORD x,DWORD y,IDirectDrawSurface *source,RECT *sr,DWORD flags)
{ inputs(); HRESULT hr=real_fast(s,x,y,source,sr,flags); if (SUCCEEDED(hr)) observe(s); return hr; }
static HRESULT WINAPI hook_unlock(IDirectDrawSurface *s,void *data)
{ HRESULT hr=real_unlock(s,data); if (SUCCEEDED(hr)) observe(s); return hr; }
static HRESULT WINAPI hook_surface(IDirectDraw *draw,DDSURFACEDESC *desc,IDirectDrawSurface **out,IUnknown *outer)
{
    inputs(); HRESULT hr=real_surface(draw,desc,out,outer);
    if (SUCCEEDED(hr)) {
        IDirectDrawSurfaceVtbl *v=(IDirectDrawSurfaceVtbl *)(*out)->lpVtbl;
        if (v->Blt!=hook_blt) {
            real_blt=v->Blt; real_fast=v->BltFast; real_unlock=v->Unlock;
            patch(&v->Blt,hook_blt); patch(&v->BltFast,hook_fast); patch(&v->Unlock,hook_unlock);
        }
    }
    return hr;
}
static HRESULT WINAPI hook_create(GUID *guid,IDirectDraw **out,IUnknown *outer)
{
    trace("DirectDrawCreate begin");
    inputs(); HRESULT hr=real_create(guid,out,outer);
    trace("DirectDrawCreate end");
    if (SUCCEEDED(hr)) {
        IDirectDrawVtbl *v=(IDirectDrawVtbl *)(*out)->lpVtbl;
        if (v->CreateSurface!=hook_surface) { real_surface=v->CreateSurface; patch(&v->CreateSurface,hook_surface); }
    }
    return hr;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    (void)instance; (void)reserved;
    if (reason!=DLL_PROCESS_ATTACH) return TRUE;
    if (!GetEnvironmentVariableA("ADAPTER_CAPTURE_DIR",directory,sizeof(directory))) stop("set ADAPTER_CAPTURE_DIR");
    CreateDirectoryA(directory,NULL);
    char path[1200]; snprintf(path,sizeof(path),"%s\\frames.json",directory);
    manifest=fopen(path,"w"); if (!manifest) stop("open manifest"); fputs("[\n",manifest);
    const unsigned char expected[]={0xa3,0x1c,0xeb,0xa6,0x00};
    if (memcmp((void *)0x540bb5,expected,5)) stop("unexpected FPS store instruction");
    DWORD old; VirtualProtect((void *)0x540bb5,5,PAGE_EXECUTE_READWRITE,&old); memset((void *)0x540bb5,0x90,5); VirtualProtect((void *)0x540bb5,5,old,&old);
    real_create=*(void **)0x79a048; patch((void *)0x79a048,hook_create);
    menu_install(); inputs(); trace("observer attached"); fflush(manifest); return TRUE;
}
