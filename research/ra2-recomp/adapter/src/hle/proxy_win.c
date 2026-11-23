/* Thin native exports. The shared MIT-based backend owns all implementation. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <dsound.h>
#include <stdio.h>
#include <stdlib.h>
static FARPROC backend(const char *name)
{
    static HMODULE module;
    if (!module) module=LoadLibraryA("ra2adapter.dll");
    FARPROC fn=module ? GetProcAddress(module,name) : NULL;
    if (!fn) { fprintf(stderr,"RA2 adapter: cannot resolve %s, error=%lu\n",name,GetLastError()); fflush(stderr); abort(); }
    /* Retain the backend until process exit: the SDL callback uses its code. */
    return fn;
}
#ifdef RA_FORWARD_DDRAW
HRESULT WINAPI DirectDrawCreate(GUID *id,IDirectDraw **out,IUnknown *outer)
{ typedef HRESULT (WINAPI *F)(GUID *,IDirectDraw **,IUnknown *); return ((F)backend("DirectDrawCreate"))(id,out,outer); }
#endif
#ifdef RA_FORWARD_DSOUND
HRESULT WINAPI DirectSoundCreate(LPCGUID id,IDirectSound **out,IUnknown *outer)
{ typedef HRESULT (WINAPI *F)(LPCGUID,IDirectSound **,IUnknown *); return ((F)backend("DirectSoundCreate"))(id,out,outer); }
#endif
#ifdef RA_FORWARD_BINK
#define FN(type,name,bytes) ((type)backend("_" #name "@" #bytes))
void *WINAPI BinkOpen(const char *name,unsigned flags) { typedef void *(WINAPI *F)(const char *,unsigned); return FN(F,BinkOpen,8)(name,flags); }
void WINAPI BinkClose(void *h) { typedef void (WINAPI *F)(void *); FN(F,BinkClose,4)(h); }
int WINAPI BinkDoFrame(void *h) { typedef int (WINAPI *F)(void *); return FN(F,BinkDoFrame,4)(h); }
int WINAPI BinkCopyToBuffer(void *h,void *dest,int pitch,unsigned height,unsigned x,unsigned y,unsigned flags)
{ typedef int (WINAPI *F)(void *,void *,int,unsigned,unsigned,unsigned,unsigned); return FN(F,BinkCopyToBuffer,28)(h,dest,pitch,height,x,y,flags); }
void WINAPI BinkNextFrame(void *h) { typedef void (WINAPI *F)(void *); FN(F,BinkNextFrame,4)(h); }
void WINAPI BinkGoto(void *h,unsigned frame,int flags) { typedef void (WINAPI *F)(void *,unsigned,int); FN(F,BinkGoto,12)(h,frame,flags); }
int WINAPI BinkPause(void *h,int paused) { typedef int (WINAPI *F)(void *,int); return FN(F,BinkPause,8)(h,paused); }
int WINAPI BinkWait(void *h) { typedef int (WINAPI *F)(void *); return FN(F,BinkWait,4)(h); }
void WINAPI BinkSetVolume(void *h,int volume) { typedef void (WINAPI *F)(void *,int); FN(F,BinkSetVolume,8)(h,volume); }
int WINAPI BinkSetSoundSystem(void *cb,void *sound) { typedef int (WINAPI *F)(void *,void *); return FN(F,BinkSetSoundSystem,8)(cb,sound); }
void *WINAPI BinkOpenDirectSound(void *sound) { typedef void *(WINAPI *F)(void *); return FN(F,BinkOpenDirectSound,4)(sound); }
const char *WINAPI BinkGetError(void) { typedef const char *(WINAPI *F)(void); return FN(F,BinkGetError,0)(); }
unsigned WINAPI BinkDDSurfaceType(void *surface) { typedef unsigned (WINAPI *F)(void *); return FN(F,BinkDDSurfaceType,4)(surface); }
#endif
