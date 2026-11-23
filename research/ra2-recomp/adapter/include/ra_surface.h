#ifndef RA_SURFACE_H
#define RA_SURFACE_H
#include <stddef.h>
#include <stdint.h>
typedef struct { int32_t left, top, right, bottom; } ra_rect;
typedef struct {
    uint32_t width, height, pitch, bits;
    uint32_t red_mask, green_mask, blue_mask;
    uint32_t palette[256]; /* 0x00RRGGBB */
    unsigned char *pixels;
} ra_surface;
enum { RA_OK=0, RA_INVALID=-1, RA_NOMEM=-2, RA_UNSUPPORTED=-3 };
enum { RA_KEY_SOURCE=1, RA_KEY_DEST=2 };
int ra_surface_init(ra_surface *, uint32_t width, uint32_t height, uint32_t bits);
void ra_surface_destroy(ra_surface *);
int ra_surface_fill(ra_surface *, const ra_rect *, uint32_t pixel);
/* Rectangles are half-open. Destination clipping keeps the original scale. */
int ra_surface_blt(ra_surface *dst, const ra_rect *dr, const ra_surface *src,
                   const ra_rect *sr, unsigned flags, uint32_t source_low,
                   uint32_t source_high, uint32_t dest_low, uint32_t dest_high);
int ra_surface_rgba(const ra_surface *, unsigned char *out, size_t out_size);
#endif
