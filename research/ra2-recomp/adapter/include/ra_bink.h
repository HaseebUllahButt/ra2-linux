#ifndef RA_BINK_H
#define RA_BINK_H
#include <stdint.h>
#include <stddef.h>
/* Public fields used by game.exe. Their offsets are part of the 32-bit ABI. */
typedef struct ra_bink {
    uint32_t Width,Height,Frames,FrameNum,LastFrameNum,FrameRate,FrameRateDiv;
} ra_bink;
ra_bink *ra_bink_open(unsigned char *owned_bytes,size_t size);
void ra_bink_close(ra_bink *);
int ra_bink_do_frame(ra_bink *);
int ra_bink_copy(ra_bink *,void *destination,int pitch,uint32_t height,uint32_t x,uint32_t y,uint32_t flags);
void ra_bink_next(ra_bink *);
int ra_bink_goto(ra_bink *,uint32_t frame);
int ra_bink_pause(ra_bink *,int pause);
int ra_bink_wait(ra_bink *);
void ra_bink_volume(ra_bink *,int volume);
const char *ra_bink_error(void);
void ra_bink_sound(int enabled);
#endif
