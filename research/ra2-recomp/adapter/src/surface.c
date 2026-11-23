#include "ra_surface.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int ra_surface_init(ra_surface *s, uint32_t w, uint32_t h, uint32_t bits)
{
    uint64_t pitch, size;
    if (!s) return RA_INVALID;
    memset(s, 0, sizeof(*s));
    if (!w || !h || w > INT32_MAX || h > INT32_MAX) return RA_INVALID;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return RA_UNSUPPORTED;
    pitch = ((uint64_t)w*(bits/8)+3)&~UINT64_C(3);
    size = pitch*h;
    if (pitch > INT32_MAX || size > SIZE_MAX) return RA_INVALID;
    s->pixels = calloc(1, (size_t)size);
    if (!s->pixels) return RA_NOMEM;
    s->width=w; s->height=h; s->bits=bits; s->pitch=(uint32_t)pitch;
    s->red_mask = bits == 16 ? 0xf800 : 0xff0000;
    s->green_mask = bits == 16 ? 0x07e0 : 0x00ff00;
    s->blue_mask = bits == 16 ? 0x001f : 0x0000ff;
    return RA_OK;
}
void ra_surface_destroy(ra_surface *s)
{
    if (s) { free(s->pixels); memset(s, 0, sizeof(*s)); }
}
static ra_rect full(const ra_surface *s)
{
    ra_rect r={0,0,(int32_t)s->width,(int32_t)s->height}; return r;
}
static int valid(ra_rect r)
{
    return r.left < r.right && r.top < r.bottom;
}
static uint32_t get_pixel(const unsigned char *p, unsigned bytes)
{
    uint32_t v=0;
    for (unsigned i=0;i<bytes;i++) v |= (uint32_t)p[i]<<(8*i);
    return v;
}
static void put_pixel(unsigned char *p, unsigned bytes, uint32_t v)
{
    for (unsigned i=0;i<bytes;i++) p[i]=(unsigned char)(v>>(8*i));
}
int ra_surface_fill(ra_surface *s, const ra_rect *rect, uint32_t pixel)
{
    ra_rect r;
    if (!s || !s->pixels) return RA_INVALID;
    r=rect ? *rect : full(s);
    if (!valid(r)) return RA_INVALID;
    if (r.left<0) r.left=0;
    if (r.top<0) r.top=0;
    if (r.right>(int32_t)s->width) r.right=(int32_t)s->width;
    if (r.bottom>(int32_t)s->height) r.bottom=(int32_t)s->height;
    for (int32_t y=r.top;y<r.bottom;y++)
        for (int32_t x=r.left;x<r.right;x++)
            put_pixel(s->pixels+(size_t)y*s->pitch+(size_t)x*(s->bits/8),s->bits/8,pixel);
    return RA_OK;
}
int ra_surface_blt(ra_surface *d, const ra_rect *dr, const ra_surface *s,
                   const ra_rect *sr, unsigned flags, uint32_t sl, uint32_t sh,
                   uint32_t dl, uint32_t dh)
{
    ra_rect a,b,c;
    unsigned char *copy=NULL;
    const unsigned char *source;
    unsigned bytes;
    if (!d || !s || !d->pixels || !s->pixels) return RA_INVALID;
    if (flags & ~(RA_KEY_SOURCE|RA_KEY_DEST)) return RA_UNSUPPORTED;
    if (s->bits!=d->bits || s->red_mask!=d->red_mask ||
        s->green_mask!=d->green_mask || s->blue_mask!=d->blue_mask)
        return RA_UNSUPPORTED;
    a=dr ? *dr : full(d); b=sr ? *sr : full(s); c=a;
    if (!valid(a) || !valid(b) || b.left<0 || b.top<0 ||
        b.right>(int32_t)s->width || b.bottom>(int32_t)s->height) return RA_INVALID;
    if (c.left<0) c.left=0;
    if (c.top<0) c.top=0;
    if (c.right>(int32_t)d->width) c.right=(int32_t)d->width;
    if (c.bottom>(int32_t)d->height) c.bottom=(int32_t)d->height;
    if (!valid(c)) return RA_OK;
    bytes=s->bits/8;
    source=s->pixels;
    if (s->pixels==d->pixels) {
        size_t size=(size_t)s->pitch*s->height;
        copy=malloc(size);
        if (!copy) return RA_NOMEM;
        memcpy(copy,source,size); source=copy;
    }
    for (int32_t y=c.top;y<c.bottom;y++) {
        int32_t sy=b.top+(int32_t)(((int64_t)y-a.top)*((int64_t)b.bottom-b.top)/((int64_t)a.bottom-a.top));
        for (int32_t x=c.left;x<c.right;x++) {
            int32_t sx=b.left+(int32_t)(((int64_t)x-a.left)*((int64_t)b.right-b.left)/((int64_t)a.right-a.left));
            uint32_t v=get_pixel(source+(size_t)sy*s->pitch+(size_t)sx*bytes,bytes);
            unsigned char *out=d->pixels+(size_t)y*d->pitch+(size_t)x*bytes;
            uint32_t old=get_pixel(out,bytes);
            if ((flags&RA_KEY_SOURCE) && v>=sl && v<=sh) continue;
            if ((flags&RA_KEY_DEST) && !(old>=dl && old<=dh)) continue;
            put_pixel(out,bytes,v);
        }
    }
    free(copy); return RA_OK;
}
static unsigned char channel(uint32_t pixel, uint32_t mask)
{
    uint32_t value;
    if (!mask) return 0;
    while (!(mask&1)) { pixel>>=1; mask>>=1; }
    value=pixel&mask;
    return (unsigned char)(((uint64_t)value*255+mask/2)/mask);
}
int ra_surface_rgba(const ra_surface *s, unsigned char *out, size_t size)
{
    if (!s || !s->pixels || !out || (uint64_t)s->width*s->height*4>size) return RA_INVALID;
    for (uint32_t y=0;y<s->height;y++) for (uint32_t x=0;x<s->width;x++) {
        uint32_t v=get_pixel(s->pixels+(size_t)y*s->pitch+(size_t)x*(s->bits/8),s->bits/8);
        unsigned char *p=out+((size_t)y*s->width+x)*4;
        if (s->bits==8) {
            v=s->palette[v]; p[0]=(unsigned char)(v>>16); p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)v;
        } else {
            p[0]=channel(v,s->red_mask); p[1]=channel(v,s->green_mask); p[2]=channel(v,s->blue_mask);
        }
        p[3]=255;
    }
    return RA_OK;
}
