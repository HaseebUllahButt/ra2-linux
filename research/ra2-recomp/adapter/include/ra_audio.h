#ifndef RA_AUDIO_H
#define RA_AUDIO_H
#include <stdint.h>
#include <stddef.h>
typedef struct ra_audio_voice {
    struct ra_audio_voice *next;
    unsigned char *data;
    uint32_t size,rate,frequency,channels,bits;
    uint64_t phase;
    int playing,looping;
    double left_gain,right_gain;
} ra_audio_voice;
int ra_audio_format(uint32_t rate,uint32_t channels,uint32_t bits);
void ra_audio_gain(ra_audio_voice *,int volume,int pan);
void ra_audio_mix(ra_audio_voice *,int16_t *output,size_t frames,uint32_t rate);
#endif
