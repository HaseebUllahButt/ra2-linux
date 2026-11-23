#include "ra_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    unsigned char pcm[]={0xe8,3,0x18,0xfc,0xd0,7,0x30,0xf8};
    int16_t out[16];
    ra_audio_voice v={0};
    v.data=pcm; v.size=sizeof(pcm); v.rate=v.frequency=22050; v.channels=1; v.bits=16; v.playing=1;
    ra_audio_gain(&v,0,0); ra_audio_mix(&v,out,8,44100);
    int16_t expected[]={1000,1000,1000,1000,-1000,-1000,-1000,-1000,2000,2000,2000,2000,-2000,-2000,-2000,-2000};
    assert(!memcmp(out,expected,sizeof(expected)) && !v.playing);
    v.playing=1; v.looping=1; v.frequency=44100; v.phase=0;
    ra_audio_gain(&v,-602,0); ra_audio_mix(&v,out,8,44100);
    assert(out[0]==500 && out[2]==-500 && out[8]==500 && v.playing);
    ra_audio_gain(&v,0,10000); ra_audio_mix(&v,out,1,44100);
    assert(out[0]==0 && out[1]==1000);
    unsigned char unsigned_pcm[]={0,128,255};
    v.data=unsigned_pcm; v.size=3; v.bits=8; v.phase=0;
    ra_audio_gain(&v,0,0); ra_audio_mix(&v,out,3,44100);
    assert(out[0]==-32768 && out[2]==0 && out[4]==32512);
    puts("PASS: PCM8/16, mono/stereo mix, sample rate, looping, stop, volume, and pan");
    return 0;
}
