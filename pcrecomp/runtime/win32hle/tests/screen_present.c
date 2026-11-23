/* Hidden tests and a small benchmark of screen.c's actual upload/draw path.
 * Build with -ffunction-sections -fdata-sections -Wl,--gc-sections and SDL2_ttf.
 * SCREEN_SOURCE can select an old screen.c for before/after measurements. */
#define _GNU_SOURCE
#include <time.h>
#ifndef SCREEN_SOURCE
#define SCREEN_SOURCE "../screen.c"
#define SCREEN_CURRENT 1
#endif
#include <SDL2/SDL.h>
static int deny_rgb888, deny_lock, texture_creates;
static SDL_Texture *test_create_texture(SDL_Renderer *ren, Uint32 fmt, int access, int w, int h) {
    texture_creates++;
    if (deny_rgb888 && fmt == SDL_PIXELFORMAT_RGB888) return NULL;
    return SDL_CreateTexture(ren,fmt,access,w,h);
}
static int test_lock_texture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch) {
    return deny_lock ? -1 : SDL_LockTexture(texture,rect,pixels,pitch);
}
#define SDL_CreateTexture test_create_texture
#define SDL_LockTexture test_lock_texture
#include SCREEN_SOURCE
#undef SDL_CreateTexture
#undef SDL_LockTexture

static const uint8_t *test_pixels;
static const uint32_t *test_palette;
static int test_w, test_h, test_pitch, test_bpp;
static unsigned test_seq;
static uint32_t test_ticks;
uint32_t hle_ticks_ms(void) { return test_ticks += 16; }
int hle_dd_frame(const uint8_t **p, int *w, int *h, int *pitch, int *bpp,
                 const uint32_t **pal, unsigned *seq) {
    *p = test_pixels; *w = test_w; *h = test_h; *pitch = test_pitch;
    *bpp = test_bpp; *pal = test_palette; *seq = ++test_seq; return 1;
}
int hle_gdi_window_frame(const uint8_t **p, int *w, int *h, int *pitch, int *bpp, unsigned *seq) {
    (void)p; (void)w; (void)h; (void)pitch; (void)bpp; (void)seq; return 0;
}
const char *hle_font_file(const char *face, int bold, int italic) {
    (void)face; (void)bold; (void)italic; return NULL;
}
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#x,SDL_GetError()); fails++; } } while (0)
static double seconds(clockid_t clock) {
    struct timespec t; clock_gettime(clock, &t); return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint32_t pixel(int x, int y) {
    uint32_t value = 0; SDL_Rect r = {x,y,1,1};
    CHECK(SDL_RenderReadPixels(g_ren, &r, SDL_PIXELFORMAT_ARGB8888, &value, 4) == 0);
    return value & 0xFFFFFFu;
}
static void dimensions(int w, int h, int bpp, int padding) {
    test_w=w; test_h=h; test_bpp=bpp; test_pitch=w*(bpp/8)+padding;
}
static int overlay(const uint8_t *px, int pitch, int w, int h, uint32_t *out,
                    int *x, int *y, int *rw, int *rh) {
    (void)px; (void)pitch;
    for(int i=0;i<w*h*4;i++)out[i]=0xFF00FF00u;
    *x=*y=0; *rw=w*2; *rh=h*2; return 2;
}
static int compose(const uint8_t *px, int pitch, int w, int h, uint32_t *out) {
    (void)px; (void)pitch;
    for(int i=0;i<w*h*4;i++)out[i]=0x000000FFu;
    return 1;
}
int main(int argc, char **argv) {
    /* Never use the owner's display. Hardware runs use EGL offscreen. */
    int hardware=argc>1&&!strcmp(argv[1],"hardware");
    setenv("SDL_VIDEODRIVER",hardware?"offscreen":"dummy",1);
    setenv("EGL_PLATFORM","surfaceless",1);
    setenv("SDL_AUDIODRIVER","dummy",1);
    CHECK(SDL_Init(SDL_INIT_VIDEO)==0);
    g_win=SDL_CreateWindow("hidden screen test",0,0,1280,960,SDL_WINDOW_HIDDEN);
    CHECK(g_win!=NULL);
    g_ren=SDL_CreateRenderer(g_win,-1,hardware?SDL_RENDERER_ACCELERATED:SDL_RENDERER_SOFTWARE);
    CHECK(g_ren!=NULL); if(!g_ren)return 1;
    SDL_RendererInfo ri;SDL_GetRendererInfo(g_ren,&ri);
    printf("video=%s renderer=%s flags=%x\n",SDL_GetCurrentVideoDriver(),ri.name,ri.flags);
    if(hardware){const unsigned char *(*gs)(unsigned)=SDL_GL_GetProcAddress("glGetString");if(gs)printf("GL_RENDERER=%s\n",gs(0x1F01));}
    g_bars=0;
    /* All RGB565 values, in rows with padding. Read back a 1:1 target. */
    dimensions(256,256,16,6);
    uint8_t *source=calloc(test_pitch,test_h);test_pixels=source;
    uint32_t *expected=malloc(256*256*4),*actual=malloc(256*256*4);
    for(int y=0;y<256;y++)for(int x=0;x<256;x++){
        uint16_t v=(uint16_t)(y*256+x);memcpy(source+(size_t)y*test_pitch+x*2,&v,2);
    }
    SDL_ConvertPixels(256,256,SDL_PIXELFORMAT_RGB565,source,test_pitch,SDL_PIXELFORMAT_ARGB8888,expected,256*4);
    g_mode=3;present();
    SDL_Texture *target=SDL_CreateTexture(g_ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,256,256);
    CHECK(target!=NULL);CHECK(SDL_SetRenderTarget(g_ren,target)==0);
    CHECK(SDL_SetTextureScaleMode(g_tex,SDL_ScaleModeNearest)==0);
    CHECK(SDL_RenderCopy(g_ren,g_tex,NULL,NULL)==0);
    CHECK(SDL_RenderReadPixels(g_ren,NULL,SDL_PIXELFORMAT_ARGB8888,actual,256*4)==0);
    for(int i=0;i<65536;i++)if((actual[i]&0xFFFFFFu)!=(expected[i]&0xFFFFFFu)){
        fprintf(stderr,"FAIL RGB565 value %04x got %08x expected %08x\n",i,actual[i],expected[i]);fails++;break;
    }
    SDL_SetRenderTarget(g_ren,NULL);SDL_DestroyTexture(target);
    free(source);free(expected);free(actual);
    /* Every scaling mode, letterbox placement, palette and mode switches. */
    uint16_t solid[8*4];for(int i=0;i<32;i++)solid[i]=0xF800;
    test_pixels=(const uint8_t*)solid;dimensions(8,4,16,0);
    for(g_mode=0;g_mode<5;g_mode++){
        present();CHECK(g_dst.x==0&&g_dst.y==160&&g_dst.w==1280&&g_dst.h==640);
        CHECK(pixel(640,400)==0xFF0000u);CHECK(pixel(0,0)==0);
    }
    g_mode=3;
    uint32_t pal[256]={0};uint8_t indexed[8*4];memset(indexed,1,sizeof indexed);pal[1]=0x0000FF00;
    test_pixels=indexed;test_palette=pal;dimensions(8,4,8,0);present();CHECK(pixel(640,400)==0x00FF00u);
    uint32_t rgb[8*4];for(int i=0;i<32;i++)rgb[i]=0x000000FF;
    test_pixels=(const uint8_t*)rgb;dimensions(8,4,32,0);present();CHECK(pixel(640,400)==0x0000FFu);
    test_pixels=(const uint8_t*)solid;dimensions(8,4,16,0);
    hle_screen_compose_hook=compose;present();CHECK(pixel(640,400)==0x0000FFu);
    hle_screen_overlay_hook=overlay;present();CHECK(pixel(640,400)==0x00FF00u);
    hle_screen_compose_hook=NULL;hle_screen_overlay_hook=NULL;
#ifdef SCREEN_CURRENT
    /* Failed native allocation or upload keeps the original RGB565 fallback. */
    SDL_DestroyTexture(g_tex);g_tex=NULL;g_rgb565_format=0;deny_rgb888=1;
    present();CHECK(g_tex&&g_tex_fmt==SDL_PIXELFORMAT_RGB565);
    int creates=texture_creates;present();CHECK(texture_creates==creates);
    CHECK(pixel(640,400)==0xFF0000u);deny_rgb888=0;
    SDL_DestroyTexture(g_tex);g_tex=NULL;g_rgb565_format=0;deny_lock=1;
    present();CHECK(g_tex==NULL);deny_lock=0;present();
    CHECK(g_tex&&g_tex_fmt==SDL_PIXELFORMAT_RGB565);CHECK(pixel(640,400)==0xFF0000u);
    SDL_DestroyTexture(g_tex);g_tex=NULL;g_rgb565_format=0;
#endif
    /* Same source and output sizes as the JOIN report. */
    dimensions(1024,768,16,0);source=malloc(test_pitch*test_h);test_pixels=source;
    for(int i=0;i<1024*768;i++){uint16_t v=(uint16_t)(i*17);memcpy(source+i*2,&v,2);}
    for(g_mode=0;g_mode<5;g_mode++){
        present();double cpu=seconds(CLOCK_PROCESS_CPUTIME_ID),wall=seconds(CLOCK_MONOTONIC);
        for(int i=0;i<50;i++)present();
        printf("mode=%s texture=%s cpu_ms=%.3f wall_ms=%.3f\n",k_modes[g_mode],SDL_GetPixelFormatName(g_tex_fmt),
            (seconds(CLOCK_PROCESS_CPUTIME_ID)-cpu)*20,(seconds(CLOCK_MONOTONIC)-wall)*20);
    }
    free(source);
    SDL_DestroyRenderer(g_ren);SDL_DestroyWindow(g_win);SDL_Quit();
    printf("screen_present: %s\n",fails?"FAILED":"all checks passed");return fails!=0;
}
