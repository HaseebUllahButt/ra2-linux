#include "ra_present.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    ra_surface s;
    ra_present *p=ra_present_open(NULL,32,24);
    assert(p);
    assert(ra_surface_init(&s,32,24,16)==RA_OK);
    assert(ra_surface_fill(&s,NULL,0xf800)==RA_OK);
    assert(ra_present_frame(p,&s)==RA_OK);
    ra_surface_destroy(&s); ra_present_close(p);
    puts("PASS: SDL display and frame upload");
    return 0;
}
