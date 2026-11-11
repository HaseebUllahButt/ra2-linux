#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <dsound.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
extern const win32hle_shim win32hle_ddraw[],win32hle_dsound[],win32hle_bink[];
static SDL_SpinLock lock;
static int initialized;
static void initialize(void)
{
    SDL_AtomicLock(&lock);
    if (!initialized) { win32hle_register(win32hle_ddraw); win32hle_register(win32hle_dsound); win32hle_register(win32hle_bink); hle_dd_primary_hook=ra_hle_present; initialized=1; }
    SDL_AtomicUnlock(&lock);
}
#define U(p) ((uint32_t)(uintptr_t)(p))
HRESULT WINAPI DirectDrawCreate(GUID *id,IDirectDraw **out,IUnknown *outer)
{ initialize(); uint32_t args[]={U(id),U(out),U(outer)}; return (HRESULT)ra_hle_invoke("DirectDrawCreate",3,args); }
HRESULT WINAPI DirectSoundCreate(LPCGUID id,IDirectSound **out,IUnknown *outer)
{ initialize(); uint32_t args[]={U(id),U(out),U(outer)}; return (HRESULT)ra_hle_invoke("DirectSoundCreate",3,args); }
void *WINAPI BinkOpen(const char *name,unsigned flags)
{ initialize(); uint32_t args[]={U(name),flags}; return hle_ptr(ra_hle_invoke("_BinkOpen@8",2,args)); }
void WINAPI BinkClose(void *h) { uint32_t args[]={U(h)}; ra_hle_invoke("_BinkClose@4",1,args); }
int WINAPI BinkDoFrame(void *h) { uint32_t args[]={U(h)}; return (int)ra_hle_invoke("_BinkDoFrame@4",1,args); }
int WINAPI BinkCopyToBuffer(void *h,void *dest,int pitch,unsigned height,unsigned x,unsigned y,unsigned flags)
{ uint32_t args[]={U(h),U(dest),(uint32_t)pitch,height,x,y,flags}; return (int)ra_hle_invoke("_BinkCopyToBuffer@28",7,args); }
void WINAPI BinkNextFrame(void *h) { uint32_t args[]={U(h)}; ra_hle_invoke("_BinkNextFrame@4",1,args); }
void WINAPI BinkGoto(void *h,unsigned frame,int flags) { uint32_t args[]={U(h),frame,(uint32_t)flags}; ra_hle_invoke("_BinkGoto@12",3,args); }
int WINAPI BinkPause(void *h,int paused) { uint32_t args[]={U(h),(uint32_t)paused}; return (int)ra_hle_invoke("_BinkPause@8",2,args); }
int WINAPI BinkWait(void *h) { uint32_t args[]={U(h)}; return (int)ra_hle_invoke("_BinkWait@4",1,args); }
void WINAPI BinkSetVolume(void *h,int volume) { uint32_t args[]={U(h),(uint32_t)volume}; ra_hle_invoke("_BinkSetVolume@8",2,args); }
int WINAPI BinkSetSoundSystem(void *cb,void *sound) { initialize(); uint32_t args[]={U(cb),U(sound)}; return (int)ra_hle_invoke("_BinkSetSoundSystem@8",2,args); }
void *WINAPI BinkOpenDirectSound(void *sound) { initialize(); uint32_t args[]={U(sound)}; return hle_ptr(ra_hle_invoke("_BinkOpenDirectSound@4",1,args)); }
const char *WINAPI BinkGetError(void) { initialize(); return hle_ptr(ra_hle_invoke("_BinkGetError@0",0,NULL)); }
unsigned WINAPI BinkDDSurfaceType(void *surface)
{
    /* Wine surfaces are not win32hle objects while graphics is under test. */
    DDPIXELFORMAT pf={0}; pf.dwSize=sizeof(pf);
    if (!surface || IDirectDrawSurface_GetPixelFormat((IDirectDrawSurface *)surface,&pf)!=DD_OK) return 0xffffffffu;
    return pf.dwRGBBitCount==16 ? (pf.dwGBitMask==0x7e0 ? 10u : 9u) : pf.dwRGBBitCount==24 ? 1u : pf.dwRGBBitCount==32 ? 3u : 0xffffffffu;
}
