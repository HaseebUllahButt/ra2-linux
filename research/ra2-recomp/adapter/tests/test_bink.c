#include <windows.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra_bink.h"
#define LOAD(type,name,bytes) type name=(void *)GetProcAddress(dll,"_" #name "@" #bytes); assert(name)
int main(int argc,char **argv)
{
    if (argc!=3) return 2;
    HMODULE dll=LoadLibraryA(argv[1]); assert(dll);
    typedef ra_bink *(WINAPI *Open)(const char *,unsigned);
    typedef void (WINAPI *Close)(ra_bink *);
    typedef int (WINAPI *DoFrame)(ra_bink *);
    typedef int (WINAPI *Copy)(ra_bink *,void *,int,unsigned,unsigned,unsigned,unsigned);
    typedef void (WINAPI *Next)(ra_bink *);
    typedef const char *(WINAPI *Error)(void);
    typedef int (WINAPI *SoundSystem)(void *,void *);
    LOAD(Open,BinkOpen,8); LOAD(Close,BinkClose,4); LOAD(DoFrame,BinkDoFrame,4);
    LOAD(Copy,BinkCopyToBuffer,28); LOAD(Next,BinkNextFrame,4); LOAD(Error,BinkGetError,0);
    LOAD(SoundSystem,BinkSetSoundSystem,8);
    BinkSetSoundSystem(NULL,NULL);
    /* Match the original game's positioned-HANDLE path, not just filename IO. */
    HANDLE file=CreateFileA(argv[2],GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL); assert(file!=INVALID_HANDLE_VALUE);
    ra_bink *h=BinkOpen((const char *)file,0x800000);
    if (!h) { fprintf(stderr,"BinkOpen failed: %s\n",BinkGetError()); return 1; }
    unsigned width=h->Width,height=h->Height,frames=h->Frames;
    assert(width && height && frames);
    size_t pitch=(size_t)width*2+8,size=pitch*height;
    unsigned char *pixels=malloc(size+16); assert(pixels);
    unsigned decoded=0;
    for (unsigned i=1;i<=frames;i++) {
        assert(h->FrameNum==i);
        memset(pixels+size,0xa5,16);
        BinkDoFrame(h);
        BinkCopyToBuffer(h,pixels,(int)pitch,height,0,0,0x8000000au);
        for (unsigned j=0;j<16;j++) assert(pixels[size+j]==0xa5);
        decoded++;
        if (i<frames) BinkNextFrame(h);
    }
    printf("MOVIE width=%u height=%u frames=%u decoded=%u fps=%u/%u\n",width,height,frames,decoded,h->FrameRate,h->FrameRateDiv);
    BinkClose(h); CloseHandle(file); free(pixels); FreeLibrary(dll); return 0;
}
