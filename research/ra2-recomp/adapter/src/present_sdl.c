#include "ra_present.h"
#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
struct ra_present {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    unsigned char *rgba;
    uint32_t width,height;
    int owns_window;
};
void ra_present_close(ra_present *p)
{
    if (!p) return;
    if (p->texture) SDL_DestroyTexture(p->texture);
    if (p->renderer) SDL_DestroyRenderer(p->renderer);
    if (p->window && p->owns_window) SDL_DestroyWindow(p->window);
    free(p->rgba); free(p);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
ra_present *ra_present_open(void *native, uint32_t w, uint32_t h)
{
    ra_present *p;
    if (!w || !h || w>16384 || h>16384) return NULL;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) return NULL;
    p=calloc(1,sizeof(*p));
    if (!p) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return NULL; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    const char *window_id=SDL_GetHint("RA2UI_SDL_WINDOW_ID");
    if (window_id && *window_id) p->window=SDL_GetWindowFromID((Uint32)strtoul(window_id,NULL,10));
    if (!p->window) {
        p->window=native ? SDL_CreateWindowFrom(native) :
            SDL_CreateWindow("RA2 adapter",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,(int)w,(int)h,0);
        p->owns_window=1;
    }
    if (!p->window) goto fail;
    p->renderer=SDL_CreateRenderer(p->window,-1,SDL_RENDERER_SOFTWARE);
    if (!p->renderer) goto fail;
    if (SDL_RenderSetLogicalSize(p->renderer,(int)w,(int)h)) goto fail;
    p->texture=SDL_CreateTexture(p->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,(int)w,(int)h);
    if (!p->texture) goto fail;
    if (SDL_SetTextureBlendMode(p->texture,SDL_BLENDMODE_NONE)) goto fail;
    p->rgba=malloc((size_t)w*h*4);
    if (!p->rgba) goto fail;
    p->width=w; p->height=h;
    return p;
fail:
    fprintf(stderr,"adapter: SDL display: %s\n",SDL_GetError());
    ra_present_close(p); return NULL;
}
int ra_present_place_game_window(ra_present *p,uint32_t w,uint32_t h)
{
    if (!p || !w || !h || w>16384 || h>16384) return RA_INVALID;
    /* DirectDraw exclusive coordinates start at the display origin. Bink uses
       ClientToScreen before writing into a locked primary surface. */
    SDL_SetWindowBordered(p->window,SDL_FALSE);
    SDL_SetWindowSize(p->window,(int)w,(int)h);
    SDL_SetWindowPosition(p->window,0,0);
    return RA_OK;
}
int ra_present_frame(ra_present *p, const ra_surface *s)
{
    if (!p || !s || p->width!=s->width || p->height!=s->height) return RA_INVALID;
    if (ra_surface_rgba(s,p->rgba,(size_t)s->width*s->height*4)) return RA_INVALID;
    if (SDL_UpdateTexture(p->texture,NULL,p->rgba,(int)s->width*4) ||
        SDL_RenderClear(p->renderer) || SDL_RenderCopy(p->renderer,p->texture,NULL,NULL)) {
        fprintf(stderr,"adapter: SDL present: %s\n",SDL_GetError()); return RA_UNSUPPORTED;
    }
    SDL_RenderPresent(p->renderer);
    /* The original game relies on DirectDraw to dispatch its window messages.
       SDL dispatches native messages for the borrowed game window here. */
    SDL_PumpEvents();
    return RA_OK;
}
