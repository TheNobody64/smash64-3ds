#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "native_hw.h"
#include "native_profile.h"

extern void port_log(const char*,...);

/* Defaults for the legacy (original 3DS / 2DS) profile. They are deliberately
 * conservative: nothing here has been measured on a console yet, so the
 * optional behaviours (a render thread on the system core, a resident asset
 * pack) are off until tuning.txt turns them on and the logged numbers justify
 * it. The 2 MiB asset cache is twice the New 3DS bound; without the
 * resident pack every miss is a romfs read, and stage loads are where those
 * happen. It stays small because the game's scene arena shares this heap. */
#define LEGACY_ASSET_CACHE (2u*1024*1024)
#define LEGACY_SYSCORE_PERCENT 30u
#define LEGACY_ARENA_MIN (6u*1024*1024)

static NativeHwTuning tuning={2,0,1024*1024,0};
static int initialized,full,consoleNew;

static void applyLine(const char* key,long value) {
    if(!strcmp(key,"render_core")&&(value==-1||value==1))tuning.render_core=(int)value;
    else if(!strcmp(key,"syscore_percent")&&value>=5&&value<=80)tuning.syscore_percent=(unsigned)value;
    else if(!strcmp(key,"asset_cache_kib")&&value>=256&&value<=16384)tuning.asset_cache_bytes=(size_t)value*1024;
    else if(!strcmp(key,"resident_reserve_mib")&&value>=0&&value<=64)tuning.resident_pack_reserve_bytes=(size_t)value*1024*1024;
    else if(!strcmp(key,"scene_arena_mib")&&(value==0||(value>=4&&value<=16)))tuning.scene_arena_bytes=(size_t)value*1024*1024;
    else port_log("HW tuning ignored: %s=%ld\n",key,value);
}

/* Optional SD-card overrides for experiments on a console, one key=value per
 * line. Ignored on the full New 3DS profile so that profile stays exactly as
 * it was released. */
static void loadTuning(void) {
    FILE* f=fopen(SSB_DATA_DIR "/tuning.txt","r");
    if(!f)return;
    char line[96];
    while(fgets(line,sizeof(line),f)){
        char key[40];long value;
        if(line[0]=='#')continue;
        if(sscanf(line," %39[a-z_] = %ld",key,&value)==2)applyLine(key,value);
    }
    fclose(f);
}

void nativeHwInit(void) {
    if(initialized)return;
    initialized=1;
    bool isNew=false;
    if(R_FAILED(APT_CheckNew3DS(&isNew)))isNew=false;
    consoleNew=isNew;
#ifdef SSB_TARGET_OLD3DS
    full=0; /* legacy-mode title: no extra core or clock even on a New 3DS */
#else
    full=isNew;
#endif
    if(full){
        tuning=(NativeHwTuning){2,0,1024*1024,0};
        return;
    }
    tuning=(NativeHwTuning){-1,LEGACY_SYSCORE_PERCENT,LEGACY_ASSET_CACHE,0};
    loadTuning();
}

/* Memory diagnostics around the game's largest allocation. */
extern u32 __ctru_heap_size,__ctru_linear_heap_size;
void port_scene_heap_note(const char* what,size_t size,void* result) {
    struct mallinfo m=mallinfo();
    port_log("MEM %s request=%u result=%p heap=%u linear=%u linear_free=%u malloc_arena=%u in_use=%u free=%u\n",what,(unsigned)size,result,
        (unsigned)__ctru_heap_size,(unsigned)__ctru_linear_heap_size,(unsigned)linearSpaceFree(),
        (unsigned)m.arena,(unsigned)m.uordblks,(unsigned)m.fordblks);
}
/* The scene arena is requested at 16 MiB, four times the N64's whole RAM, which
 * is all the original game ever had for a scene. On the New 3DS that leaves
 * most of an 84 MiB heap free. On the original 3DS the same request can take
 * most of the ordinary heap and leave the asset cache, audio banks and the
 * loader's own containers starving, which shows up in the scenes that load the
 * most (stage select) rather than at boot. So the legacy profile starts from
 * a third of the heap instead of from the largest size that happens to fit.
 * tuning.txt scene_arena_mib overrides it; boot.log's ARENA lines report how
 * much of it each scene really used. The full profile keeps its request. */
size_t port_scene_heap_initial(size_t requested) {
    if(nativeHwFull())return requested;
    size_t size=tuning.scene_arena_bytes;
    if(!size){
        size=((size_t)__ctru_heap_size/3)&~(size_t)0xFFFFF;
        if(size<LEGACY_ARENA_MIN)size=LEGACY_ARENA_MIN;
    }
    if(size>requested)size=requested;
    port_log("MEM scene arena initial request %u KiB (heap=%u KiB, tuning=%u KiB)\n",
        (unsigned)(size>>10),(unsigned)(__ctru_heap_size>>10),(unsigned)(tuning.scene_arena_bytes>>10));
    return size;
}
/* Where even that much contiguous memory is not there, retry smaller rather
 * than stop. 0 means "give up": the full profile keeps its released behaviour. */
size_t port_scene_heap_fallback(size_t failed) {
    if(nativeHwFull())return 0;
    static const unsigned steps[]={12,10,8,6,5,4};
    for(unsigned i=0;i<sizeof(steps)/sizeof(steps[0]);i++)
        if((size_t)steps[i]*1024*1024<failed){port_log("MEM scene arena retry with %u MiB\n",steps[i]);return (size_t)steps[i]*1024*1024;}
    return 0;
}

int nativeHwFull(void) {nativeHwInit();return full;}
int nativeHwConsoleIsNew(void) {nativeHwInit();return consoleNew;}
const NativeHwTuning* nativeHwTuning(void) {nativeHwInit();return &tuning;}
