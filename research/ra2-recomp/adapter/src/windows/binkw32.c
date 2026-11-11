#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra_bink.h"
static char open_error[128];
static void failure(const char *name)
{
    fprintf(stderr,"RA2 Bink: %s: %s\n",name,ra_bink_error()); fflush(stderr); abort();
}
ra_bink *WINAPI BinkOpen(const char *name,unsigned flags)
{
    unsigned char *bytes=NULL; size_t size=0;
    open_error[0]=0;
    if (flags&0x800000) {
        /* ABI-only bridge: the game passes a HANDLE positioned inside a MIX.
           Decoder input below this wrapper is portable, owned memory. */
        HANDLE handle=(HANDLE)name;
        LARGE_INTEGER zero={0},base;
        unsigned char header[8]; DWORD got;
        if (!SetFilePointerEx(handle,zero,&base,FILE_CURRENT) || !ReadFile(handle,header,8,&got,NULL) || got!=8) goto fail;
        size=8+(size_t)header[4]+((size_t)header[5]<<8)+((size_t)header[6]<<16)+((size_t)header[7]<<24);
        if (size<44 || size>512u*1024u*1024u || memcmp(header,"BIK",3)) goto fail;
        bytes=malloc(size); if (!bytes) goto fail;
        if (!SetFilePointerEx(handle,base,NULL,FILE_BEGIN) || !ReadFile(handle,bytes,(DWORD)size,&got,NULL) || got!=size) goto fail;
    } else {
        FILE *file=fopen(name,"rb"); if (!file) goto fail;
        if (fseek(file,0,SEEK_END)) { fclose(file); goto fail; }
        long length=ftell(file); if (length<44 || length>512*1024*1024) { fclose(file); goto fail; }
        size=(size_t)length; rewind(file); bytes=malloc(size);
        if (!bytes || fread(bytes,1,size,file)!=size) { fclose(file); goto fail; }
        fclose(file);
    }
    return ra_bink_open(bytes,size);
fail:
    free(bytes); snprintf(open_error,sizeof(open_error),"Cannot read Bink input, flags=%x",flags); return NULL;
}
const char *WINAPI BinkGetError(void) { return *open_error ? open_error : ra_bink_error(); }
void WINAPI BinkClose(ra_bink *h) { ra_bink_close(h); }
int WINAPI BinkDoFrame(ra_bink *h) { if (ra_bink_do_frame(h)) failure("DoFrame"); return 0; }
int WINAPI BinkCopyToBuffer(ra_bink *h,void *dest,int pitch,unsigned height,unsigned x,unsigned y,unsigned flags)
{ if (ra_bink_copy(h,dest,pitch,height,x,y,flags)) failure("CopyToBuffer"); return 0; }
void WINAPI BinkNextFrame(ra_bink *h) { ra_bink_next(h); }
void WINAPI BinkGoto(ra_bink *h,unsigned frame,int flags) { (void)flags; if (ra_bink_goto(h,frame)) failure("Goto"); }
int WINAPI BinkPause(ra_bink *h,int paused) { return ra_bink_pause(h,paused); }
int WINAPI BinkWait(ra_bink *h) { return ra_bink_wait(h); }
void WINAPI BinkSetVolume(ra_bink *h,int volume) { ra_bink_volume(h,volume); }
void *WINAPI BinkOpenDirectSound(void *sound) { return sound; }
int WINAPI BinkSetSoundSystem(void *callback,void *sound) { (void)sound; ra_bink_sound(callback!=NULL); return 1; }
unsigned WINAPI BinkDDSurfaceType(IDirectDrawSurface *surface)
{
    DDPIXELFORMAT pf={0}; pf.dwSize=sizeof(pf);
    if (!surface || IDirectDrawSurface_GetPixelFormat(surface,&pf)!=DD_OK) return 0;
    if (pf.dwRGBBitCount==16) return pf.dwGBitMask==0x7e0 ? 10 : 9;
    if (pf.dwRGBBitCount==24) return 1;
    if (pf.dwRGBBitCount==32) return 3;
    return 0;
}
