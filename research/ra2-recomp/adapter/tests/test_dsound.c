#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    HMODULE dll=LoadLibraryA(".\\dsound.dll"); assert(dll);
    HRESULT (WINAPI *create)(LPCGUID,LPDIRECTSOUND *,IUnknown *)=(void *)GetProcAddress(dll,(LPCSTR)1); assert(create);
    IDirectSound *s; IDirectSoundBuffer *b,*copy;
    assert(create(NULL,&s,NULL)==DS_OK);
    WAVEFORMATEX format={WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
    DSBUFFERDESC desc={0}; desc.dwSize=sizeof(desc); desc.dwFlags=DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_CTRLFREQUENCY; desc.dwBufferBytes=8; desc.lpwfxFormat=&format;
    assert(IDirectSound_CreateSoundBuffer(s,&desc,&b,NULL)==DS_OK);
    void *p1,*p2; DWORD n1,n2;
    assert(IDirectSoundBuffer_Lock(b,6,4,&p1,&n1,&p2,&n2,0)==DS_OK && n1==2 && n2==2);
    assert(IDirectSoundBuffer_Unlock(b,p1,n1,p2,n2)==DS_OK);
    assert(IDirectSoundBuffer_Lock(b,0,8,&p1,&n1,&p2,&n2,0)==DS_OK && n1==8 && n2==0);
    const unsigned char bytes[]={0xe8,3,0x18,0xfc,0xd0,7,0x30,0xf8}; memcpy(p1,bytes,8);
    assert(IDirectSoundBuffer_Unlock(b,p1,n1,p2,n2)==DS_OK);
    assert(IDirectSound_DuplicateSoundBuffer(s,b,&copy)==DS_OK);
    assert(IDirectSoundBuffer_Release(copy)==0);
    assert(IDirectSoundBuffer_SetVolume(b,-602)==DS_OK);
    LONG volume; assert(IDirectSoundBuffer_GetVolume(b,&volume)==DS_OK && volume==-602);
    assert(IDirectSoundBuffer_SetVolume(b,0)==DS_OK);
    assert(IDirectSoundBuffer_Play(b,0,0,DSBPLAY_LOOPING)==DS_OK);
    Sleep(150);
    DWORD status; assert(IDirectSoundBuffer_GetStatus(b,&status)==DS_OK && (status&DSBSTATUS_PLAYING));
    assert(IDirectSoundBuffer_Stop(b)==DS_OK);
    assert(IDirectSoundBuffer_Release(b)==0);
    assert(IDirectSound_Release(s)==0);
    FreeLibrary(dll); puts("PASS: DirectSound ordinal 1, ring lock, duplicate, volume, loop, status, and release"); return 0;
}
