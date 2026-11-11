/* Native Windows ABI view for the MIT win32hle modules. */
#ifndef WIN32HLE_H
#define WIN32HLE_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
typedef void (*recomp_func_t)(void);
typedef struct { const char *name; recomp_func_t fn; } win32hle_shim;
typedef uint32_t win_handle;
extern _Thread_local uint32_t g_eax,g_esp;
#define MEM8(a) (*(uint8_t *)(uintptr_t)(uint32_t)(a))
#define MEM16(a) (*(uint16_t *)(uintptr_t)(uint32_t)(a))
#define MEM32(a) (*(uint32_t *)(uintptr_t)(uint32_t)(a))
static inline void *hle_ptr(uint32_t p) { return (void *)(uintptr_t)p; }
static inline uint32_t hle_va(const void *p) { return (uint32_t)(uintptr_t)p; }
#define A32(i) MEM32(g_esp+4u+4u*(uint32_t)(i))
#define APTR(i) hle_ptr(A32(i))
#define ASTR(i) ((char *)APTR(i))
#define RET(value,n) do { g_eax=(uint32_t)(value); g_esp+=4u+4u*(n); return; } while(0)
#define RETV(n) RET(0,n)
#define RETP(value,n) RET(hle_va(value),n)
void *hle_alloc(uint32_t n);
void hle_free(void *p);
void hle_fatal(const char *,...);
uint32_t hle_guest_strdup(const char *s);
int win32hle_register(const win32hle_shim *);
uint32_t hle_resolve(const char *);
uint32_t hle_com_vtable(const win32hle_shim *);
uint32_t hle_com_new(uint32_t,uint32_t);
void hle_com_free(uint32_t);
void hle_com_QueryInterface(void);
void hle_com_AddRef(void);
void hle_com_register_class(const uint8_t *,uint32_t (*)(const uint8_t *,uint32_t *));
uint32_t hle_call_guest(uint32_t,int,const uint32_t *);
uint32_t hle_scratch_mark(void);
uint32_t hle_scratch(uint32_t);
void hle_scratch_release(uint32_t);
uint32_t hle_ticks_ms(void);
void hle_block_begin(void);
void hle_block_end(void);
void hle_set_screen_size(int,int);
void hle_screen_desktop(int *,int *);
void hle_guid_string(const uint8_t *,char *);
uint32_t hle_gdi_surface_dc(uint8_t *,int,int,int,int);
void hle_gdi_release_dc(uint32_t);
int hle_gdi_window_frame(const uint8_t **,int *,int *,int *,int *,unsigned *);
void hle_gdi_window_touched(void);
int hle_host_path(const char *,char *,size_t);
int hle_file_fd(uint32_t);
int hle_audio_stream_open(int,int);
void hle_audio_stream_push(int,const int16_t *,int);
void hle_audio_stream_close(int);
void hle_audio_stream_gain(int,float);
int hle_audio_stream_queued(int);
int hle_dd_surface_pixels(uint32_t,uint8_t **,int *,int *,int *,int *);
int hle_dd_frame(const uint8_t **,int *,int *,int *,int *,const uint32_t **,unsigned *);
void hle_dd_mode(int *,int *,int *);
extern void (*hle_dd_primary_hook)(void);
extern uint32_t (*hle_dd_qi_hook)(const uint8_t *);
extern uint32_t (*hle_dd_surface_qi_hook)(uint32_t,const uint8_t *);
#define HLE_DD_MAX_MODES 32
void ra_hle_native_before(const char *,const uint32_t *);
void ra_hle_native_after(const char *,const uint32_t *);
void ra_hle_present(void);
uint32_t ra_hle_invoke(const char *,unsigned,const uint32_t *);
int ra_hle_native_clip(uint32_t,uint32_t,uint32_t,uint32_t);
int64_t ra_hle_lseek(int,int64_t,int);
int ra_hle_open(const char *,int,...);
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
int ra_hle_nanosleep(const struct timespec *,struct timespec *);
#define nanosleep ra_hle_nanosleep
#define lseek ra_hle_lseek
#define open ra_hle_open
#endif
