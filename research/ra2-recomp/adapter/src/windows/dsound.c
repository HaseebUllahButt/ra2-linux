#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <dsound.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ra_audio.h"
typedef struct Sound {
    IDirectSound iface;
    ULONG refs;
    SDL_AudioDeviceID device;
    ra_audio_voice *voices;
    FILE *capture;
    uint64_t mixed_frames;
} Sound;
typedef struct Data { unsigned char *bytes; DWORD size; unsigned refs; } Data;
typedef struct Buffer {
    IDirectSoundBuffer iface;
    ULONG refs;
    Sound *sound;
    Data *data;
    ra_audio_voice voice;
    WAVEFORMATEX format;
    DWORD flags;
    LONG volume,pan;
    int locked;
} Buffer;
static IDirectSoundVtbl sound_vtable;
static IDirectSoundBufferVtbl buffer_vtable;
static void missing(const char *name) { fprintf(stderr,"RA2 audio: unsupported %s\n",name); fflush(stderr); abort(); }
static void callback(void *ctx,Uint8 *stream,int length)
{
    Sound *s=ctx;
    ra_audio_mix(s->voices,(int16_t *)stream,(size_t)length/4,44100);
    s->mixed_frames+=(unsigned)length/4;
    if (s->capture && fwrite(stream,1,(size_t)length,s->capture)!=(size_t)length) missing("capture write");
}
static ULONG WINAPI sound_AddRef(IDirectSound *iface) { return ++((Sound *)iface)->refs; }
static ULONG WINAPI sound_Release(IDirectSound *iface)
{
    Sound *s=(Sound *)iface; ULONG refs=--s->refs;
    if (!refs) {
        SDL_CloseAudioDevice(s->device);
        if (s->capture) fclose(s->capture);
        SDL_QuitSubSystem(SDL_INIT_AUDIO); free(s);
    }
    return refs;
}
static HRESULT WINAPI sound_QueryInterface(IDirectSound *iface,REFIID id,void **out)
{
    if (!out) return E_POINTER; *out=NULL;
    if (!id) return E_INVALIDARG;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IDirectSound)) return E_NOINTERFACE;
    *out=iface; sound_AddRef(iface); return DS_OK;
}
static ULONG WINAPI buffer_AddRef(IDirectSoundBuffer *iface) { return ++((Buffer *)iface)->refs; }
static ULONG WINAPI buffer_Release(IDirectSoundBuffer *iface)
{
    Buffer *b=(Buffer *)iface; ULONG refs=--b->refs;
    if (!refs) {
        Sound *s=b->sound;
        SDL_LockAudioDevice(s->device);
        ra_audio_voice **p=&s->voices;
        while (*p && *p!=&b->voice) p=&(*p)->next;
        if (*p) *p=(*p)->next;
        if (b->data && !--b->data->refs) { free(b->data->bytes); free(b->data); }
        SDL_UnlockAudioDevice(s->device);
        sound_Release(&s->iface); free(b);
    }
    return refs;
}
static HRESULT WINAPI buffer_QueryInterface(IDirectSoundBuffer *iface,REFIID id,void **out)
{
    if (!out) return E_POINTER; *out=NULL;
    if (!id) return E_INVALIDARG;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IDirectSoundBuffer)) return E_NOINTERFACE;
    *out=iface; buffer_AddRef(iface); return DS_OK;
}
static HRESULT WINAPI sound_CreateSoundBuffer(IDirectSound *iface,LPCDSBUFFERDESC desc,LPDIRECTSOUNDBUFFER *out,IUnknown *outer)
{
    Sound *s=(Sound *)iface; Buffer *b;
    if (!out) return E_POINTER; *out=NULL;
    if (!desc) return DSERR_INVALIDPARAM;
    if (outer) return CLASS_E_NOAGGREGATION;
    int primary=(desc->dwFlags&DSBCAPS_PRIMARYBUFFER)!=0;
    if (!primary && (!desc->lpwfxFormat || !desc->dwBufferBytes)) return DSERR_INVALIDPARAM;
    WAVEFORMATEX format=primary ? (WAVEFORMATEX){WAVE_FORMAT_PCM,2,44100,176400,4,16,0} : *desc->lpwfxFormat;
    if (format.wFormatTag!=WAVE_FORMAT_PCM || !ra_audio_format(format.nSamplesPerSec,format.nChannels,format.wBitsPerSample) ||
        format.nBlockAlign!=format.nChannels*(format.wBitsPerSample/8)) return DSERR_BADFORMAT;
    b=calloc(1,sizeof(*b)); if (!b) return DSERR_OUTOFMEMORY;
    b->iface.lpVtbl=&buffer_vtable; b->refs=1; b->sound=s; b->flags=desc->dwFlags; b->format=format;
    b->voice.rate=b->voice.frequency=format.nSamplesPerSec; b->voice.channels=format.nChannels; b->voice.bits=format.wBitsPerSample;
    ra_audio_gain(&b->voice,0,0);
    if (!primary) {
        b->data=calloc(1,sizeof(*b->data));
        if (!b->data) { free(b); return DSERR_OUTOFMEMORY; }
        b->data->bytes=malloc(desc->dwBufferBytes);
        if (!b->data->bytes) { free(b->data); free(b); return DSERR_OUTOFMEMORY; }
        memset(b->data->bytes,format.wBitsPerSample==8 ? 128 : 0,desc->dwBufferBytes);
        b->data->size=desc->dwBufferBytes; b->data->refs=1;
        b->voice.data=b->data->bytes; b->voice.size=b->data->size;
    }
    sound_AddRef(iface);
    SDL_LockAudioDevice(s->device); b->voice.next=s->voices; s->voices=&b->voice; SDL_UnlockAudioDevice(s->device);
    fprintf(stderr,"RA2 audio: CreateSoundBuffer bytes=%lu rate=%lu channels=%u bits=%u primary=%d\n",desc->dwBufferBytes,format.nSamplesPerSec,format.nChannels,format.wBitsPerSample,primary);
    *out=&b->iface; return DS_OK;
}
static HRESULT WINAPI sound_DuplicateSoundBuffer(IDirectSound *iface,LPDIRECTSOUNDBUFFER original,LPDIRECTSOUNDBUFFER *out)
{
    if (!original || !out) return DSERR_INVALIDPARAM;
    Buffer *src=(Buffer *)original;
    if (!src->data || src->sound!=(Sound *)iface) return DSERR_INVALIDPARAM;
    DSBUFFERDESC desc={0}; desc.dwSize=sizeof(desc); desc.dwFlags=src->flags; desc.dwBufferBytes=src->data->size; desc.lpwfxFormat=&src->format;
    HRESULT hr=sound_CreateSoundBuffer(iface,&desc,out,NULL); if (FAILED(hr)) return hr;
    Buffer *dst=(Buffer *)*out;
    SDL_LockAudioDevice(src->sound->device);
    free(dst->data->bytes); free(dst->data);
    dst->data=src->data; dst->data->refs++; dst->voice.data=dst->data->bytes;
    SDL_UnlockAudioDevice(src->sound->device); return DS_OK;
}
static HRESULT WINAPI sound_SetCooperativeLevel(IDirectSound *iface,HWND window,DWORD level)
{ (void)iface; (void)window; (void)level; return DS_OK; }
static HRESULT WINAPI sound_GetCaps(IDirectSound *iface,LPDSCAPS caps)
{
    (void)iface;
    if (!caps || caps->dwSize!=sizeof(*caps)) return DSERR_INVALIDPARAM;
    memset(caps,0,sizeof(*caps)); caps->dwSize=sizeof(*caps);
    caps->dwFlags=DSCAPS_PRIMARYMONO|DSCAPS_PRIMARYSTEREO|DSCAPS_PRIMARY8BIT|DSCAPS_PRIMARY16BIT|DSCAPS_SECONDARYMONO|DSCAPS_SECONDARYSTEREO|DSCAPS_SECONDARY8BIT|DSCAPS_SECONDARY16BIT;
    caps->dwMinSecondarySampleRate=100; caps->dwMaxSecondarySampleRate=200000; caps->dwPrimaryBuffers=1; return DS_OK;
}
static HRESULT WINAPI sound_GetSpeakerConfig(IDirectSound *iface,LPDWORD value) { (void)iface; if (!value) return DSERR_INVALIDPARAM; *value=DSSPEAKER_STEREO; return DS_OK; }
static HRESULT WINAPI sound_Initialize(IDirectSound *iface,LPCGUID guid) { (void)iface; (void)guid; return DSERR_ALREADYINITIALIZED; }
static HRESULT WINAPI buffer_GetCaps(IDirectSoundBuffer *iface,LPDSBCAPS caps)
{
    Buffer *b=(Buffer *)iface;
    if (!caps || caps->dwSize!=sizeof(*caps)) return DSERR_INVALIDPARAM;
    memset(caps,0,sizeof(*caps)); caps->dwSize=sizeof(*caps); caps->dwFlags=b->flags;
    caps->dwBufferBytes=b->voice.size; return DS_OK;
}
static HRESULT WINAPI buffer_GetCurrentPosition(IDirectSoundBuffer *iface,LPDWORD play,LPDWORD write)
{
    Buffer *b=(Buffer *)iface; SDL_LockAudioDevice(b->sound->device);
    DWORD pos=(DWORD)(b->voice.phase>>32)*b->format.nBlockAlign;
    if (play) *play=pos;
    if (write) *write=b->voice.size ? (pos+(b->format.nAvgBytesPerSec/1000*30/b->format.nBlockAlign)*b->format.nBlockAlign)%b->voice.size : 0;
    SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_GetFormat(IDirectSoundBuffer *iface,LPWAVEFORMATEX out,DWORD size,LPDWORD written)
{
    Buffer *b=(Buffer *)iface;
    if (written) *written=sizeof(b->format);
    if (!out) return written ? DS_OK : DSERR_INVALIDPARAM;
    if (size<sizeof(b->format)) return DSERR_INVALIDPARAM;
    *out=b->format; return DS_OK;
}
static HRESULT WINAPI buffer_GetVolume(IDirectSoundBuffer *iface,LPLONG out) { if (!out) return DSERR_INVALIDPARAM; *out=((Buffer *)iface)->volume; return DS_OK; }
static HRESULT WINAPI buffer_GetPan(IDirectSoundBuffer *iface,LPLONG out) { if (!out) return DSERR_INVALIDPARAM; *out=((Buffer *)iface)->pan; return DS_OK; }
static HRESULT WINAPI buffer_GetFrequency(IDirectSoundBuffer *iface,LPDWORD out) { if (!out) return DSERR_INVALIDPARAM; *out=((Buffer *)iface)->voice.frequency; return DS_OK; }
static HRESULT WINAPI buffer_GetStatus(IDirectSoundBuffer *iface,LPDWORD status)
{
    Buffer *b=(Buffer *)iface;
    if (!status) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(b->sound->device);
    *status=b->voice.playing ? DSBSTATUS_PLAYING|(b->voice.looping ? DSBSTATUS_LOOPING : 0) : 0;
    SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_Lock(IDirectSoundBuffer *iface,DWORD offset,DWORD bytes,LPVOID *p1,LPDWORD n1,LPVOID *p2,LPDWORD n2,DWORD flags)
{
    Buffer *b=(Buffer *)iface;
    if (!p1 || !n1 || !b->data || b->locked) return DSERR_INVALIDPARAM;
    if (flags&~(DSBLOCK_FROMWRITECURSOR|DSBLOCK_ENTIREBUFFER)) missing("Buffer.Lock.flags");
    SDL_LockAudioDevice(b->sound->device);
    if (flags&DSBLOCK_FROMWRITECURSOR) offset=((DWORD)(b->voice.phase>>32)*b->format.nBlockAlign+b->format.nAvgBytesPerSec/1000*30)%b->voice.size;
    if (flags&DSBLOCK_ENTIREBUFFER) { offset=0; bytes=b->voice.size; }
    if (offset>=b->voice.size || bytes>b->voice.size || (offset+bytes>b->voice.size && (!p2 || !n2))) {
        SDL_UnlockAudioDevice(b->sound->device); return DSERR_INVALIDPARAM;
    }
    *n1=bytes>b->voice.size-offset ? b->voice.size-offset : bytes; *p1=b->voice.data+offset;
    if (p2) *p2=bytes>*n1 ? b->voice.data : NULL;
    if (n2) *n2=bytes-*n1;
    b->locked=1; return DS_OK;
}
static HRESULT WINAPI buffer_Unlock(IDirectSoundBuffer *iface,LPVOID p1,DWORD n1,LPVOID p2,DWORD n2)
{
    Buffer *b=(Buffer *)iface; (void)p1; (void)n1; (void)p2; (void)n2;
    if (!b->locked) return DSERR_INVALIDPARAM;
    b->locked=0; SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_Play(IDirectSoundBuffer *iface,DWORD reserved1,DWORD reserved2,DWORD flags)
{
    Buffer *b=(Buffer *)iface; (void)reserved1; (void)reserved2;
    if (flags&~DSBPLAY_LOOPING) missing("Buffer.Play.flags");
    SDL_LockAudioDevice(b->sound->device); b->voice.playing=1; b->voice.looping=(flags&DSBPLAY_LOOPING)!=0; SDL_UnlockAudioDevice(b->sound->device);
    fprintf(stderr,"RA2 audio: Play bytes=%u looping=%d\n",b->voice.size,b->voice.looping); return DS_OK;
}
static HRESULT WINAPI buffer_Stop(IDirectSoundBuffer *iface)
{ Buffer *b=(Buffer *)iface; SDL_LockAudioDevice(b->sound->device); b->voice.playing=0; SDL_UnlockAudioDevice(b->sound->device); return DS_OK; }
static HRESULT WINAPI buffer_SetCurrentPosition(IDirectSoundBuffer *iface,DWORD pos)
{
    Buffer *b=(Buffer *)iface; if (!b->voice.size || pos>=b->voice.size) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(b->sound->device); b->voice.phase=((uint64_t)(pos/b->format.nBlockAlign)<<32); SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_SetFormat(IDirectSoundBuffer *iface,LPCWAVEFORMATEX format)
{
    Buffer *b=(Buffer *)iface;
    if (!(b->flags&DSBCAPS_PRIMARYBUFFER)) return DSERR_INVALIDCALL;
    if (!format || format->wFormatTag!=WAVE_FORMAT_PCM || !ra_audio_format(format->nSamplesPerSec,format->nChannels,format->wBitsPerSample)) return DSERR_BADFORMAT;
    b->format=*format; return DS_OK;
}
static HRESULT WINAPI buffer_SetVolume(IDirectSoundBuffer *iface,LONG value)
{
    Buffer *b=(Buffer *)iface; if (value< -10000 || value>0) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(b->sound->device); b->volume=value; ra_audio_gain(&b->voice,b->volume,b->pan); SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_SetPan(IDirectSoundBuffer *iface,LONG value)
{
    Buffer *b=(Buffer *)iface; if (value< -10000 || value>10000) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(b->sound->device); b->pan=value; ra_audio_gain(&b->voice,b->volume,b->pan); SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_SetFrequency(IDirectSoundBuffer *iface,DWORD value)
{
    Buffer *b=(Buffer *)iface;
    if (!value) value=b->format.nSamplesPerSec;
    if (value<100 || value>200000) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(b->sound->device); b->voice.frequency=value; SDL_UnlockAudioDevice(b->sound->device); return DS_OK;
}
static HRESULT WINAPI buffer_Restore(IDirectSoundBuffer *iface) { (void)iface; return DS_OK; }
#include "dsound_slots.inc"
HRESULT WINAPI DirectSoundCreate(LPCGUID guid,LPDIRECTSOUND *out,IUnknown *outer)
{
    (void)guid;
    if (!out) return E_POINTER; *out=NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    char driver[128]={0};
    GetEnvironmentVariableA("SDL_AUDIODRIVER",driver,sizeof(driver));
    fprintf(stderr,"RA2 audio: driver native=%s SDL=%s CRT=%s\n",driver,SDL_getenv("SDL_AUDIODRIVER"),getenv("SDL_AUDIODRIVER"));
    if (*driver) SDL_SetHintWithPriority(SDL_HINT_AUDIODRIVER,driver,SDL_HINT_OVERRIDE);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return DSERR_NODRIVER;
    Sound *s=calloc(1,sizeof(*s)); if (!s) { SDL_QuitSubSystem(SDL_INIT_AUDIO); return DSERR_OUTOFMEMORY; }
    s->iface.lpVtbl=&sound_vtable; s->refs=1;
    SDL_AudioSpec desired={0},actual;
    desired.freq=44100; desired.format=AUDIO_S16LSB; desired.channels=2; desired.samples=512; desired.callback=callback; desired.userdata=s;
    s->device=SDL_OpenAudioDevice(NULL,0,&desired,&actual,0);
    if (!s->device) { fprintf(stderr,"RA2 audio SDL: %s\n",SDL_GetError()); free(s); SDL_QuitSubSystem(SDL_INIT_AUDIO); return DSERR_NODRIVER; }
    const char *capture=SDL_getenv("RA2_AUDIO_CAPTURE");
    if (capture && *capture) { s->capture=fopen(capture,"wb"); if (!s->capture) missing("capture open"); }
    SDL_PauseAudioDevice(s->device,0); *out=&s->iface;
    fprintf(stderr,"RA2 audio: DirectSoundCreate driver=%s\n",SDL_GetCurrentAudioDriver()); return DS_OK;
}
