#include "ra_audio.h"
#include <math.h>
#include <string.h>
int ra_audio_format(uint32_t rate,uint32_t channels,uint32_t bits)
{
    return rate>=100 && rate<=200000 && (channels==1 || channels==2) && (bits==8 || bits==16);
}
void ra_audio_gain(ra_audio_voice *v,int volume,int pan)
{
    double gain=volume<=-10000 ? 0 : pow(10.0,volume/2000.0);
    v->left_gain=gain*(pan>0 ? pow(10.0,-pan/2000.0) : 1);
    v->right_gain=gain*(pan<0 ? pow(10.0,pan/2000.0) : 1);
}
static int sample(const ra_audio_voice *v,uint32_t frame,unsigned channel)
{
    unsigned bytes=v->bits/8;
    const unsigned char *p=v->data+((size_t)frame*v->channels+(v->channels==1 ? 0 : channel))*bytes;
    if (bytes==1) return ((int)*p-128)*256;
    return (int16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
void ra_audio_mix(ra_audio_voice *voices,int16_t *out,size_t frames,uint32_t rate)
{
    if (!rate) { memset(out,0,frames*4); return; }
    for (size_t i=0;i<frames;i++) {
        double left=0,right=0;
        for (ra_audio_voice *v=voices;v;v=v->next) {
            if (!v->playing || !v->data || !ra_audio_format(v->rate,v->channels,v->bits)) continue;
            uint32_t count=v->size/(v->channels*(v->bits/8));
            if (!count) { v->playing=0; continue; }
            uint64_t end=(uint64_t)count<<32;
            if (v->phase>=end) {
                if (v->looping) v->phase%=end;
                else { v->playing=0; v->phase=0; continue; }
            }
            uint32_t index=(uint32_t)(v->phase>>32);
            left+=sample(v,index,0)*v->left_gain;
            right+=sample(v,index,1)*v->right_gain;
            v->phase+=((uint64_t)v->frequency<<32)/rate;
            if (v->phase>=end) {
                if (v->looping) v->phase%=end;
                else { v->playing=0; v->phase=0; }
            }
        }
        out[i*2]=(int16_t)(left>32767 ? 32767 : left< -32768 ? -32768 : left);
        out[i*2+1]=(int16_t)(right>32767 ? 32767 : right< -32768 ? -32768 : right);
    }
}
