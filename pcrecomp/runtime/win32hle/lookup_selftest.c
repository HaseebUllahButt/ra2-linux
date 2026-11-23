/* The table cache must preserve exact addresses, live manual overrides,
 * imports added after a miss, and per-import bridge state. */
#define _GNU_SOURCE
#define recomp_lookup_manual lookup_test_override
#include "win32hle.h"
#undef recomp_lookup_manual
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static void first(void) {}
static void second(void) {}
static void manual(void) {}
const recomp_dispatch_entry_t recomp_dispatch_table[] = {
    {0,first}, {0x00401000,first}, {0x00401010,second}, {0x00402000,second},
    {0x10401000,first}, {0x7EFFFFF0,second}, {0x80000000,first}, {0xFFFFFFFF,second}
};
const uint32_t recomp_dispatch_count = sizeof recomp_dispatch_table / sizeof recomp_dispatch_table[0];
static recomp_func_t override;
recomp_func_t lookup_test_override(uint32_t va) { return va == 0x00401000 ? override : NULL; }
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); fails++; } } while(0)
static recomp_func_t reference(uint32_t va) {
    for (unsigned i=0;i<recomp_dispatch_count;i++)
        if (recomp_dispatch_table[i].address==va) return recomp_dispatch_table[i].func;
    return NULL;
}
static void *sweep(void *unused) {
    (void)unused;
    for (unsigned pass=0;pass<20000;pass++) {
        for (unsigned i=0;i<recomp_dispatch_count;i++) {
            uint32_t va=recomp_dispatch_table[i].address;
            if (recomp_lookup(va)!=reference(va) || recomp_lookup(va+1)!=reference(va+1)) return (void*)1;
        }
        uint32_t va=pass*0x9E3779B9u;
        if(recomp_lookup(va)!=reference(va))return (void*)1;
    }
    return NULL;
}
static unsigned import_a, import_b;
static void import(void) {
    if(!strcmp(g_cur_import,"lookup-test-A"))import_a++;
    else if(!strcmp(g_cur_import,"lookup-test-B"))import_b++;
}
int main(void) {
    CHECK(sweep(NULL)==NULL);
    pthread_t threads[4];int started=0;
    for(int i=0;i<4;i++){int rc=pthread_create(&threads[i],NULL,sweep,NULL);CHECK(rc==0);if(rc)break;started++;}
    for(int i=0;i<started;i++){void *result=NULL;CHECK(pthread_join(threads[i],&result)==0);CHECK(result==NULL);}
    CHECK(recomp_resolve(0x00401000)==first); /* warm the table cache */
    override=manual;CHECK(recomp_resolve(0x00401000)==manual);
    override=second;CHECK(recomp_resolve(0x00401000)==second);
    override=NULL;CHECK(recomp_resolve(0x00401000)==first);
    CHECK(recomp_resolve(0x7F000000)==NULL); /* register after this miss */
    const win32hle_shim shims[]={{"lookup-test-A",import},{"lookup-test-B",import},{NULL,NULL}};
    CHECK(win32hle_register(shims)==2);
    uint32_t a=hle_resolve("lookup-test-A"),b=hle_resolve("lookup-test-B");
    CHECK(a&&b&&a!=b);
    for(int i=0;i<100;i++){
        recomp_func_t f=recomp_resolve(a);CHECK(f!=NULL);if(f)f();
        f=recomp_resolve(b);CHECK(f!=NULL);if(f)f();
    }
    CHECK(import_a==100&&import_b==100);
    CHECK(recomp_lookup(a)==NULL&&recomp_lookup(b)==NULL);
    CHECK(recomp_resolve(a+1)==NULL);
    printf("lookup_selftest: %s\n",fails?"FAILED":"all checks passed");return fails!=0;
}
