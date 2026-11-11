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
    typedef int (WINAPI *Pause)(ra_bink *,int);
    typedef int (WINAPI *Wait)(ra_bink *);
    typedef void (WINAPI *Goto)(ra_bink *,unsigned,int);
    LOAD(Pause,BinkPause,8); LOAD(Wait,BinkWait,4); LOAD(Goto,BinkGoto,12);
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
    unsigned char *first=malloc(size); assert(first);
    for (unsigned i=1;i<=frames;i++) {
        assert(h->FrameNum==i);
        memset(pixels+size,0xa5,16);
        BinkDoFrame(h);
        BinkCopyToBuffer(h,pixels,(int)pitch,height,0,0,0x8000000au);
        for (unsigned j=0;j<16;j++) assert(pixels[size+j]==0xa5);
        if (i==1) {
            memcpy(first,pixels,size); BinkDoFrame(h);
            BinkCopyToBuffer(h,pixels,(int)pitch,height,0,0,0x8000000au);
            assert(memcmp(first,pixels,size)==0);
            int p1=BinkPause(h,1),p2=BinkPause(h,1),p3=BinkPause(h,0);
            printf("PAUSE returns=%d,%d,%d\n",p1,p2,p3);
        }
        decoded++;
        if (i<frames) BinkNextFrame(h);
    }
    printf("MOVIE width=%u height=%u frames=%u decoded=%u fps=%u/%u\n",width,height,frames,decoded,h->FrameRate,h->FrameRateDiv);
    BinkNextFrame(h);
    printf("END frame=%u last=%u wait=%d\n",h->FrameNum,h->LastFrameNum,BinkWait(h));
    BinkGoto(h,1,0); assert(h->FrameNum==1);
    BinkDoFrame(h); BinkCopyToBuffer(h,pixels,(int)pitch,height,0,0,0x8000000au);
    assert(memcmp(first,pixels,size)==0);
    puts("SEEK first frame matches after full movie decode");
    BinkClose(h); CloseHandle(file); free(first); free(pixels); FreeLibrary(dll); return 0;
}
