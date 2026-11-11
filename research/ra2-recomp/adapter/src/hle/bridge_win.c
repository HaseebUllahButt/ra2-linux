/* Native stdcall boundary for the MIT win32hle base. No recomp runtime globals. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include "win32hle.h"
#undef open
#undef lseek
_Thread_local uint32_t g_eax,g_esp;
static struct { const char *name; recomp_func_t fn; uint32_t native; } registry[2048];
static unsigned registry_count;
static unsigned char *code;
static size_t code_used;
static _Thread_local void *scratch[128];
static _Thread_local unsigned scratch_count;
void hle_fatal(const char *format,...)
{
    va_list args; va_start(args,format); fputs("ADAPTER HLE: ",stderr); vfprintf(stderr,format,args); fputc('\n',stderr); va_end(args); fflush(stderr); abort();
}
void *hle_alloc(uint32_t n) { void *p=calloc(1,n ? n : 1); if (!p) hle_fatal("allocation of %u bytes failed",n); return p; }
void hle_free(void *p) { free(p); }
uint32_t hle_guest_strdup(const char *s) { size_t n=strlen(s)+1; char *p=hle_alloc((uint32_t)n); memcpy(p,s,n); return hle_va(p); }
static __attribute__((force_align_arg_pointer)) uint64_t dispatch(unsigned id,uint32_t *stack)
{
    if (id>=registry_count) hle_fatal("bad native thunk %u",id);
    uint32_t saved_esp=g_esp,saved_eax=g_eax;
    g_esp=hle_va(stack);
    ra_hle_native_before(registry[id].name,stack);
    registry[id].fn();
    uint32_t value=g_eax,pop=g_esp-hle_va(stack);
    if (pop<4 || pop>256 || (pop&3)) hle_fatal("bad stdcall return in %s: %u bytes",registry[id].name,pop);
    ra_hle_native_after(registry[id].name,stack);
    g_esp=saved_esp; g_eax=saved_eax;
    return ((uint64_t)pop<<32)|value;
}
static uint32_t thunk(unsigned id)
{
    if (!code || code_used+64>65536) { code=VirtualAlloc(NULL,65536,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE); code_used=0; if (!code) hle_fatal("executable thunk allocation"); }
    unsigned char bytes[]={0x55,0x89,0xe5,0x8d,0x45,0x04,0x50,0x68,0,0,0,0,0xb8,0,0,0,0,0xff,0xd0,0x83,0xc4,0x08,0x8b,0x4d,0x04,0x89,0xec,0x5d,0x01,0xd4,0xff,0xe1};
    uint32_t address=hle_va((void *)dispatch);
    memcpy(bytes+8,&id,4); memcpy(bytes+13,&address,4);
    unsigned char *entry=code+code_used; memcpy(entry,bytes,sizeof(bytes)); code_used+=64;
    FlushInstructionCache(GetCurrentProcess(),entry,sizeof(bytes)); return hle_va(entry);
}
int win32hle_register(const win32hle_shim *table)
{
    int added=0;
    for (;table->name;table++) {
        unsigned i; for (i=0;i<registry_count;i++) if (!strcmp(registry[i].name,table->name)) break;
        if (i<registry_count) continue;
        if (registry_count>=2048) hle_fatal("shim registry full");
        registry[i].name=table->name; registry[i].fn=table->fn; registry[i].native=thunk(i); registry_count++; added++;
    }
    return added;
}
uint32_t hle_resolve(const char *name) { for (unsigned i=0;i<registry_count;i++) if (!strcmp(registry[i].name,name)) return registry[i].native; hle_fatal("unregistered %s",name); return 0; }
uint32_t ra_hle_invoke(const char *name,unsigned count,const uint32_t *args)
{
    uint32_t frame[33]={0}; if (count>32) hle_fatal("too many arguments"); memcpy(frame+1,args,count*4);
    for (unsigned i=0;i<registry_count;i++) if (!strcmp(registry[i].name,name)) return (uint32_t)dispatch(i,frame);
    hle_fatal("unknown export %s",name); return 0;
}
/* COM helpers adapted from pcrecomp runtime/win32hle/ole32.c (MIT). */
uint32_t hle_com_vtable(const win32hle_shim *methods)
{
    unsigned n=0; while (methods[n].name) n++;
    uint32_t *v=hle_alloc(n*4+4); win32hle_register(methods);
    for (unsigned i=0;i<n;i++) v[i]=hle_resolve(methods[i].name); return hle_va(v);
}
uint32_t hle_com_new(uint32_t vtable,uint32_t size) { uint32_t *p=hle_alloc(size<8 ? 8 : size); p[0]=vtable; p[1]=1; return hle_va(p); }
void hle_com_free(uint32_t self) { hle_free(hle_ptr(self)); }
void hle_com_QueryInterface(void) { uint32_t self=A32(0); if (!A32(2)) RET(0x80004003u,3); MEM32(A32(2))=self; MEM32(self+4)++; RET(0,3); }
void hle_com_AddRef(void) { RET(++MEM32(A32(0)+4),1); }
static struct { uint8_t id[16]; uint32_t (*create)(const uint8_t *,uint32_t *); } classes[32];
static unsigned class_count;
void hle_com_register_class(const uint8_t *id,uint32_t (*create)(const uint8_t *,uint32_t *))
{
    unsigned i; for (i=0;i<class_count;i++) if (!memcmp(id,classes[i].id,16)) break;
    if (i==32) hle_fatal("class registry full");
    memcpy(classes[i].id,id,16); classes[i].create=create; if (i==class_count) class_count++;
}
uint32_t hle_call_guest(uint32_t va,int n,const uint32_t *a)
{
#define F(...) ((uint32_t (WINAPI *)(__VA_ARGS__))(uintptr_t)va)
    switch (n) {
    case 0: return F(void)();
    case 1: return F(uint32_t)(a[0]);
    case 2: return F(uint32_t,uint32_t)(a[0],a[1]);
    case 3: return F(uint32_t,uint32_t,uint32_t)(a[0],a[1],a[2]);
    case 4: return F(uint32_t,uint32_t,uint32_t,uint32_t)(a[0],a[1],a[2],a[3]);
    case 5: return F(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)(a[0],a[1],a[2],a[3],a[4]);
    default: hle_fatal("callback with %d arguments",n); return 0;
    }
#undef F
}
uint32_t hle_scratch_mark(void) { return scratch_count; }
uint32_t hle_scratch(uint32_t size) { if (scratch_count==128) hle_fatal("scratch stack full"); void *p=hle_alloc(size); scratch[scratch_count++]=p; return hle_va(p); }
void hle_scratch_release(uint32_t mark) { while (scratch_count>mark) hle_free(scratch[--scratch_count]); }
uint32_t hle_ticks_ms(void) { return SDL_GetTicks(); }
void hle_block_begin(void) {}
void hle_block_end(void) {}
void hle_guid_string(const uint8_t *iid,char *out) { snprintf(out,40,"{%08x}",*(const uint32_t *)iid); }
uint32_t hle_d3d_query(const uint8_t *iid) { (void)iid; return 0; }
uint32_t hle_d3d_surface_query(uint32_t obj,const uint8_t *iid) { (void)obj; (void)iid; return 0; }
int hle_host_path(const char *name,char *out,size_t count) { if (strlen(name)>=count) return 0; strcpy(out,name); return 1; }
int hle_file_fd(uint32_t raw)
{
    HANDLE duplicate;
    if (!DuplicateHandle(GetCurrentProcess(),(HANDLE)(uintptr_t)raw,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)) return -1;
    int fd=_open_osfhandle((intptr_t)duplicate,_O_RDONLY|_O_BINARY);
    if (fd<0) CloseHandle(duplicate); return fd;
}
int ra_hle_open(const char *path,int flags,...) { return _open(path,flags|_O_BINARY,0600); }
int64_t ra_hle_lseek(int fd,int64_t offset,int whence) { return _lseeki64(fd,offset,whence); }
