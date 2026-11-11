#include <windows.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "win32hle.h"
void ra_hle_native_before(const char *name,const uint32_t *stack) { (void)name; (void)stack; }
void ra_hle_native_after(const char *name,const uint32_t *stack) { (void)name; (void)stack; }
int main(void)
{
    int stream=hle_audio_stream_open(44100,2); assert(stream>=0);
    int16_t samples[8192]={0};
    hle_audio_stream_pause(stream,1);
    hle_audio_stream_push(stream,samples,4096);
    int before=hle_audio_stream_queued(stream); assert(before==4096);
    Sleep(100); assert(hle_audio_stream_queued(stream)==before);
    hle_audio_stream_pause(stream,0);
    Sleep(100); assert(hle_audio_stream_queued(stream)<before);
    hle_audio_stream_pause(stream,1);
    hle_audio_stream_clear(stream); assert(hle_audio_stream_queued(stream)==0);
    hle_audio_stream_close(stream);
    puts("PASS: paused movie audio stays queued; resume consumes it; seek clears it");
    return 0;
}
