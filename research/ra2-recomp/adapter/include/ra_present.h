#ifndef RA_PRESENT_H
#define RA_PRESENT_H
#include "ra_surface.h"
typedef struct ra_present ra_present;
/* native_window is an existing HWND on Windows; NULL creates a new window. */
ra_present *ra_present_open(void *native_window, uint32_t width, uint32_t height);
int ra_present_place_game_window(ra_present *, uint32_t width, uint32_t height);
int ra_present_frame(ra_present *, const ra_surface *);
void ra_present_close(ra_present *);
#endif
