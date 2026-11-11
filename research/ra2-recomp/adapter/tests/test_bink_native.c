#include "ra_bink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    FILE *f=fopen(argv[1],"rb"); if (!f) return 1;
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    if (size<44) return 1;
    unsigned char *data=malloc((size_t)size);
    if (!data || fread(data,1,(size_t)size,f)!=(size_t)size) return 1;
    fclose(f);
    ra_bink *movie=ra_bink_open(data,(size_t)size);
    if (!movie) { fprintf(stderr,"%s\n",ra_bink_error()); return 1; }
    size_t pitch=(size_t)movie->Width*2+8,bytes=pitch*movie->Height;
    unsigned char *pixels=malloc(bytes+16); if (!pixels) return 1;
    unsigned decoded=0;
    for (unsigned i=1;i<=movie->Frames;i++) {
        memset(pixels+bytes,0xa5,16);
        if (ra_bink_do_frame(movie) || ra_bink_copy(movie,pixels,(int)pitch,movie->Height,0,0,10)) { fprintf(stderr,"%s\n",ra_bink_error()); return 1; }
        for (unsigned j=0;j<16;j++) if (pixels[bytes+j]!=0xa5) return 1;
        decoded++; if (i<movie->Frames) ra_bink_next(movie);
    }
    printf("MOVIE width=%u height=%u frames=%u decoded=%u fps=%u/%u\n",movie->Width,movie->Height,movie->Frames,decoded,movie->FrameRate,movie->FrameRateDiv);
    ra_bink_close(movie); free(pixels); return 0;
}
