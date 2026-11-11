#include "ra_surface.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void copies(void)
{
    ra_surface s,d;
    ra_rect rect={1,1,4,3}, clip={-2,-1,4,3}, src={0,0,3,2};
    assert(ra_surface_init(&s,3,2,8)==RA_OK);
    assert(s.pitch==4);
    assert(ra_surface_init(&d,6,4,8)==RA_OK);
    memcpy(s.pixels,"\1\2\3\0\4\5\6\0",8);
    assert(ra_surface_fill(&d,NULL,9)==RA_OK);
    assert(ra_surface_blt(&d,&rect,&s,NULL,0,0,0,0,0)==RA_OK);
    assert(d.pixels[d.pitch+1]==1 && d.pixels[2*d.pitch+3]==6);
    assert(d.pixels[0]==9 && d.pixels[3*d.pitch+5]==9);
    assert(ra_surface_blt(&d,&clip,&s,&src,0,0,0,0,0)==RA_OK);
    assert(d.pixels[0]==2 && d.pixels[3]==3);
    assert(d.pixels[d.pitch]==5 && d.pixels[2*d.pitch+3]==6);
    assert(ra_surface_fill(&d,NULL,9)==RA_OK);
    assert(ra_surface_blt(&d,&rect,&s,NULL,RA_KEY_SOURCE,2,5,0,0)==RA_OK);
    assert(d.pixels[d.pitch+1]==1 && d.pixels[d.pitch+2]==9);
    assert(d.pixels[2*d.pitch+2]==9 && d.pixels[2*d.pitch+3]==6);
    assert(ra_surface_fill(&d,&rect,7)==RA_OK);
    d.pixels[d.pitch+1]=9;
    assert(ra_surface_blt(&d,&rect,&s,NULL,RA_KEY_DEST,0,0,7,7)==RA_OK);
    assert(d.pixels[d.pitch+1]==9 && d.pixels[d.pitch+2]==2);
    /* A forward overlapping copy must read the old source pixels. */
    rect=(ra_rect){1,0,3,2}; src=(ra_rect){0,0,2,2};
    assert(ra_surface_blt(&s,&rect,&s,&src,0,0,0,0,0)==RA_OK);
    assert(s.pixels[0]==1 && s.pixels[1]==1 && s.pixels[2]==2);
    assert(s.pixels[4]==4 && s.pixels[5]==4 && s.pixels[6]==5);
    rect=(ra_rect){-20,-20,-10,-10};
    assert(ra_surface_blt(&d,&rect,&s,NULL,0,0,0,0,0)==RA_OK);
    rect=(ra_rect){0,0,0,1};
    assert(ra_surface_blt(&d,&rect,&s,NULL,0,0,0,0,0)==RA_INVALID);
    assert(ra_surface_blt(&d,NULL,&s,NULL,128,0,0,0,0)==RA_UNSUPPORTED);
    ra_surface_destroy(&s); ra_surface_destroy(&d);
}
static void formats(void)
{
    ra_surface s;
    unsigned char out[16];
    assert(ra_surface_init(&s,1,1,8)==RA_OK);
    s.palette[19]=0x123456; s.pixels[0]=19;
    assert(ra_surface_rgba(&s,out,4)==RA_OK);
    assert(memcmp(out,"\x12\x34\x56\xff",4)==0);
    assert(ra_surface_rgba(&s,out,3)==RA_INVALID);
    ra_surface_destroy(&s);
    assert(ra_surface_init(&s,3,1,16)==RA_OK);
    assert(ra_surface_fill(&s,NULL,0xf800)==RA_OK);
    assert(ra_surface_rgba(&s,out,12)==RA_OK);
    assert(memcmp(out,"\xff\0\0\xff",4)==0);
    s.red_mask=0x7c00; s.green_mask=0x03e0;
    assert(ra_surface_fill(&s,NULL,0x03e0)==RA_OK);
    assert(ra_surface_rgba(&s,out,12)==RA_OK);
    assert(memcmp(out,"\0\xff\0\xff",4)==0);
    ra_surface_destroy(&s);
    for (unsigned bits=24;bits<=32;bits+=8) {
        assert(ra_surface_init(&s,3,1,bits)==RA_OK);
        assert(ra_surface_fill(&s,NULL,0x123456)==RA_OK);
        assert(ra_surface_rgba(&s,out,12)==RA_OK);
        assert(memcmp(out,"\x12\x34\x56\xff",4)==0);
        ra_surface_destroy(&s);
    }
    assert(ra_surface_init(&s,0,1,8)==RA_INVALID);
    assert(ra_surface_init(&s,1,1,15)==RA_UNSUPPORTED);
    assert(ra_surface_init(&s,UINT32_MAX,1,32)==RA_INVALID);
}
int main(void)
{
    copies(); formats(); puts("PASS: copy, clipping, stretch, keys, overlap, pitch, palette, RGB565, RGB555, BGR24, BGR32");
    return 0;
}
