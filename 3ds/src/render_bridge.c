#include <ssb_types.h>
#include "native_profile.h"
#include <PR/gbi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gfx_pc.h"
#include "rsp_platform.h"
#include "native_diag.h"
extern void* portRelocTryResolvePointer(uint32_t);
extern bool portRelocFindContainingFile(const void*,uintptr_t*,size_t*);
extern void port_log(const char*,...);
extern void nativeRenderInit(void);
extern void nativeRenderBegin(void);
extern void nativeRenderCapture(void);
extern char __text_start,__end__;
extern void nativeRenderWait(void),nativeRenderIdle(void);
void* native_current_dl;
unsigned native_stereo_backdrop;
static uintptr_t segments[16];
static unsigned dlDepth,dlCommands;
static struct Range {uintptr_t begin,end;} ranges[4096];
static unsigned rangeCount;
static struct Range cached;
/* Runtime endian conversion is idempotent per asset allocation. Keep exact
 * requests cached. A full reset invalidates everything; the frequent ranged
 * evictions (effects and objects freed mid-match) only drop entries that
 * overlap the evicted memory, so static stage and fighter assets stay cached. */
static struct Fixup {const void* address;unsigned count,epoch;unsigned short listed;} fixed[2][4096];
static unsigned fixupEpoch=1;
/* Slots that hold an entry of the current epoch, so a ranged eviction
 * visits only those instead of all 8192. */
static unsigned short liveSlots[2*4096];
static unsigned liveCount;
unsigned native_fix_calls,native_fix_hits,native_fix_outside,native_fix_invalidations;
volatile uint32_t native_test_dump_textures;
void nativeDumpTexture(const uint8_t* pixels,unsigned w,unsigned h,const void* address,unsigned fmt,unsigned siz,unsigned line,unsigned masks,unsigned shifts){
    extern volatile uint32_t ssb_frame_count;
    static unsigned count;
    if(!native_test_dump_textures||ssb_frame_count!=850||count>=256)return;
    char path[128];snprintf(path,sizeof(path),SSB_DATA_DIR "/texture-%03u.ppm",count);
    FILE* f=fopen(path,"wb");if(!f)return;
    fprintf(f,"P6\n%u %u\n255\n",w,h);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)fwrite(pixels+(y*w+x)*4,1,3,f);
    fclose(f);
    snprintf(path,sizeof(path),SSB_DATA_DIR "/texture-%03u.rgba",count);
    f=fopen(path,"wb");if(f){fwrite(pixels,4,w*h,f);fclose(f);}
    port_log("TEXTURE id=%u addr=%p size=%ux%u fmt=%u siz=%u line=%u masks=%04x shifts=%04x\n",count++,address,w,h,fmt,siz,line,masks,shifts);
}
static void invalidateFixups(void){
    native_fix_invalidations++;
    for(unsigned i=0;i<liveCount;i++)fixed[liveSlots[i]>>12][liveSlots[i]&4095].listed=0;
    liveCount=0;
    if(++fixupEpoch==0){memset(fixed,0,sizeof(fixed));fixupEpoch=1;}
}
static void invalidateFixupRange(const void* p,size_t n){
    uintptr_t lo=(uintptr_t)p,hi=lo+n;
    native_fix_invalidations++;
    for(unsigned i=0;i<liveCount;){
        unsigned kind=liveSlots[i]>>12;
        struct Fixup* f=&fixed[kind][liveSlots[i]&4095];
        uintptr_t a=(uintptr_t)f->address,end=a+(kind?f->count*16u:f->count);
        if(f->epoch==fixupEpoch&&a<hi&&end>lo)f->epoch=0;
        if(f->epoch!=fixupEpoch){f->listed=0;liveSlots[i]=liveSlots[--liveCount];}
        else i++;
    }
}
extern void __real_portResetStructFixups(void);
extern void __real_portEvictStructFixupsInRange(const void*,size_t);
void __wrap_portResetStructFixups(void){nativeRenderWait();invalidateFixups();__real_portResetStructFixups();}
#ifdef SSB_BENCH
uint64_t native_evict_ticks[3];
extern unsigned long long nativeProfTick(void);
#define EVICT_TIMED(slot,call) do{unsigned long long t0_=nativeProfTick();call;native_evict_ticks[slot]+=nativeProfTick()-t0_;}while(0)
#else
#define EVICT_TIMED(slot,call) call
#endif
void __wrap_portEvictStructFixupsInRange(const void* p,size_t n){
    nativeRenderWait();
    EVICT_TIMED(0,invalidateFixupRange(p,n));
    EVICT_TIMED(1,__real_portEvictStructFixupsInRange(p,n));
}
static void fixAsset(const void* p,unsigned count,unsigned kind){
    unsigned hash=(((uintptr_t)p>>4)^(count*2654435761u))&4095;
    struct Fixup* f=&fixed[kind][hash];
    native_fix_calls++;
    if(f->epoch==fixupEpoch&&f->address==p&&f->count==count){native_fix_hits++;return;}
    if(kind)portRelocFixupVertexAtRuntime(p,count);else portRelocFixupTextureAtRuntime(p,count);
    uintptr_t base;size_t size;
    if(portRelocFindContainingFile(p,&base,&size)){
        unsigned listed=f->listed;
        *f=(struct Fixup){p,count,fixupEpoch,1};
        if(!listed)liveSlots[liveCount++]=(unsigned short)(kind<<12|hash);
    }
    else native_fix_outside++;
}
void nativeFixVertices(const void* p,unsigned n){fixAsset(p,n,1);}
void nativeFixTexture(const void* p,unsigned n){fixAsset(p,n,0);}
void port_dl_range_register(const void*p,size_t n,const char*l) {
    nativeRenderWait();
    uintptr_t lo=(uintptr_t)p;
    if(!p||!n||lo+n<lo)return;
    cached.begin=cached.end=0;
    for(unsigned i=0;i<rangeCount;i++)if(ranges[i].begin==lo){ranges[i].end=lo+n;return;}
    if(rangeCount==4096)abort();
    ranges[rangeCount++]=(struct Range){lo,lo+n};
}
void port_dl_range_unregister(const void*p) {
    nativeRenderWait();
    cached.begin=cached.end=0;
    for(unsigned i=0;i<rangeCount;i++)if(ranges[i].begin==(uintptr_t)p){ranges[i]=ranges[--rangeCount];return;}
}
int port_dl_check_addr(uintptr_t p) {
    if(p>=cached.begin&&p+8<=cached.end)return 1;
    for(unsigned i=0;i<rangeCount;i++)if(p>=ranges[i].begin&&p+8<=ranges[i].end){cached=ranges[i];return 1;}
    return 0;
}
static void fail(const char* why,uintptr_t value,const void* cmd) {
    port_log("GFX FAILURE %s value=%08lx command=%p depth=%u count=%u\n",why,(unsigned long)value,cmd,dlDepth,dlCommands);abort();
}
void* nativeResolveGfxAddress(uintptr_t word,const void* command,bool branch) {
    if(!word)return NULL;
    void* ptr=portRelocTryResolvePointer(word);
    if(ptr)return ptr;
    unsigned segment=word>>24;uintptr_t offset=word&0xffffff;
    if(segment==0x0e) {
        if(branch&&segments[segment])return (void*)(segments[segment]+offset);
        uintptr_t base=0;size_t size=0;
        if(portRelocFindContainingFile(command,&base,&size)&&offset<size)return (void*)(base+offset);
    }
    if(segment<16&&segments[segment]&&segment!=8)return (void*)(segments[segment]+offset);
    return (void*)word;
}
void nativeSetSegment(unsigned segment,uintptr_t base) {
    if(segment>=16)fail("segment",segment,native_current_dl);
    void* p=portRelocTryResolvePointer(base);
    segments[segment]=p?(uintptr_t)p:base;
}
void nativeDlFrameBegin(void) {
    dlDepth=dlCommands=0;native_stereo_backdrop=0;
    extern volatile uint32_t ssb_frame_count;
    extern void portTextureCacheDeleteRange(const void*,size_t);
    if(native_test_dump_textures&&ssb_frame_count==850)portTextureCacheDeleteRange(NULL,0xffffffffu);
}
void native_dl_enter(void) {if(++dlDepth>64)fail("DL recursion",dlDepth,native_current_dl);}
void native_dl_leave(void) {dlDepth--;}
void native_dl_check(const void*p) {
    if(++dlCommands>500000 || !port_dl_check_addr((uintptr_t)p))fail("DL bounds",(uintptr_t)p,p);
}
void nativeUnknownOpcode(unsigned opcode,const void*p) {fail("unsupported opcode",opcode,p);}
void nativeRenderAssertion(const char* expression,int line,const void* p) {port_log("Renderer assertion line=%d expression=%s\n",line,expression);fail("assert",line,p);}
/* Runs for every triangle, so it has its own switch rather than following
 * general logging (which bench builds enable). */
volatile uint32_t native_diag_until;
void port_diag_open(const char* what){
    native_diag_until=ssb_frame_count+NATIVE_DIAG_FRAMES;
    port_log("DIAG open %s frame=%u\n",what,(unsigned)ssb_frame_count);
}
/* Names of the RDP color-combiner inputs, by field. An input the frontend does
 * not implement (it reads as zero, so a multiply by it renders black) is marked. */
static const char* diagA(unsigned v){static const char* n[8]={"COMBINED","TEXEL0","TEXEL1","PRIM","SHADE","ENV","ONE","NOISE"};return v<8?n[v]:"0";}
static const char* diagB(unsigned v){static const char* n[8]={"COMBINED","TEXEL0","TEXEL1","PRIM","SHADE","ENV","CENTER","K4"};return v<8?n[v]:"0";}
static const char* diagC(unsigned v){static const char* n[16]={"COMBINED","TEXEL0","TEXEL1","PRIM","SHADE","ENV","SCALE","COMBINED_A","TEXEL0_A","TEXEL1_A","PRIM_A","SHADE_A","ENV_A","LOD_FRAC","PRIM_LOD_FRAC","K5"};return v<16?n[v]:"0";}
static const char* diagD(unsigned v){static const char* n[8]={"COMBINED","TEXEL0","TEXEL1","PRIM","SHADE","ENV","ONE","0"};return n[v&7];}
static const char* diagAlpha(unsigned v){static const char* n[8]={"COMBINED_A","TEXEL0_A","TEXEL1_A","PRIM_A","SHADE_A","ENV_A","ONE","0"};return n[v&7];}
static const char* diagAlphaC(unsigned v){static const char* n[8]={"LOD_FRAC","TEXEL0_A","TEXEL1_A","PRIM_A","SHADE_A","ENV_A","PRIM_LOD_FRAC","0"};return n[v&7];}
void nativeDiagDraw(const NativeDiagState* s){
    static struct{uint32_t a,b,gm,ol,oh,t0,t1;}seen[24];static unsigned count;static int full;
    uint32_t t0=(uint32_t)(uintptr_t)s->tile[0].origin,t1=(uint32_t)(uintptr_t)s->tile[1].origin;
    /* One entry per combiner/mode/texture set: the first draw of each is logged
     * with the colors it had, not every later animation step. */
    for(unsigned i=0;i<count;i++)if(seen[i].a==s->cc0&&seen[i].b==s->cc1&&seen[i].gm==s->geometry&&seen[i].ol==s->om_l&&seen[i].oh==s->om_h&&
        seen[i].t0==t0&&seen[i].t1==t1)return;
    if(count==24){if(!full){full=1;port_log("DIAG draw table full\n");}return;}
    seen[count].a=s->cc0;seen[count].b=s->cc1;seen[count].gm=s->geometry;seen[count].ol=s->om_l;seen[count].oh=s->om_h;
    seen[count].t0=t0;seen[count++].t1=t1;
    uint32_t w0=s->cc0,w1=s->cc1;
    unsigned a0=(w0>>20)&15,c0=(w0>>15)&31,Aa0=(w0>>12)&7,Ac0=(w0>>9)&7,a1=(w0>>5)&15,c1=w0&31;
    unsigned b0=(w1>>28)&15,b1=(w1>>24)&15,Aa1=(w1>>21)&7,Ac1=(w1>>18)&7,d0=(w1>>15)&7,Ab0=(w1>>12)&7,Ad0=(w1>>9)&7,d1=(w1>>6)&7,Ab1=(w1>>3)&7,Ad1=w1&7;
    /* mapped by the frontend: 0..5 inputs, 6 as ONE for A and D, 8 TEXEL0_A, 13 LOD_FRAC, 14 PRIM_LOD_FRAC */
    int bad=(a0==7)||(b0==6||b0==7)||c0==6||c0==7||c0==9||c0==10||c0==11||c0==12||c0==15;
    port_log("DIAG draw#%u gm=%08x om=%08x,%08x cycle=%u tex=%u,%u prim=%u,%u,%u,%u env=%u,%u,%u,%u lod=%u shade0=%u,%u,%u,%u lights=%u ambient=%u,%u,%u light0=%u,%u,%u\n",
        count,(unsigned)s->geometry,(unsigned)s->om_h,(unsigned)s->om_l,(unsigned)(((s->om_h>>20)&3)+1),s->used[0],s->used[1],
        s->prim[0],s->prim[1],s->prim[2],s->prim[3],s->env[0],s->env[1],s->env[2],s->env[3],s->prim_lod,s->shade[0],s->shade[1],s->shade[2],s->shade[3],
        s->lights,s->ambient[0],s->ambient[1],s->ambient[2],s->light0[0],s->light0[1],s->light0[2]);
    port_log("DIAG   cc %08x %08x  cycle1 rgb=(%s-%s)*%s+%s alpha=(%s-%s)*%s+%s%s\n",(unsigned)w0,(unsigned)w1,
        diagA(a0),diagB(b0),diagC(c0),diagD(d0),diagAlpha(Aa0),diagAlpha(Ab0),diagAlphaC(Ac0),diagAlpha(Ad0),
        bad?"  <-- input the frontend reads as ZERO":"");
    port_log("DIAG   cc cycle2 rgb=(%s-%s)*%s+%s alpha=(%s-%s)*%s+%s\n",diagA(a1),diagB(b1),diagC(c1),diagD(d1),diagAlpha(Aa1),diagAlpha(Ab1),diagAlphaC(Ac1),diagAlpha(Ad1));
    for(unsigned u=0;u<2;u++)if(s->used[u])
        port_log("DIAG   tile%u fmt=%u siz=%u pal=%u cms=%u cmt=%u tmem=%u line=%u size=%ux%u loaded_from=%p first8=%02x%02x%02x%02x%02x%02x%02x%02x\n",
            u,s->tile[u].fmt,s->tile[u].siz,s->tile[u].palette,s->tile[u].cms,s->tile[u].cmt,s->tile[u].tmem,s->tile[u].line,
            (unsigned)s->tile[u].width,(unsigned)s->tile[u].height,s->tile[u].origin,
            s->tile[u].bytes[0],s->tile[u].bytes[1],s->tile[u].bytes[2],s->tile[u].bytes[3],s->tile[u].bytes[4],s->tile[u].bytes[5],s->tile[u].bytes[6],s->tile[u].bytes[7]);
    port_log("DIAG   palette0=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",s->palette[0],s->palette[1],s->palette[2],s->palette[3],
        s->palette[4],s->palette[5],s->palette[6],s->palette[7],s->palette[8],s->palette[9],s->palette[10],s->palette[11],s->palette[12],s->palette[13],s->palette[14],s->palette[15]);
}
void nativeDiagTexture(const uint8_t* px,unsigned pitch,unsigned w,unsigned h,unsigned fmt,unsigned siz,unsigned pal,unsigned tlut,const uint8_t* palette,const void* source){
    static unsigned lines;
    if(lines>=40)return;
    lines++;
    unsigned clear=0,black=0,visible=0;unsigned long sr=0,sg=0,sb=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        const uint8_t* p=px+((size_t)y*pitch+x)*4;
        if(!p[3]){clear++;continue;}
        visible++;sr+=p[0];sg+=p[1];sb+=p[2];
        if(!(p[0]|p[1]|p[2]))black++;
    }
    port_log("DIAG tex fmt=%u siz=%u pal=%u tlut=%u %ux%u transparent=%u visible=%u black=%u mean=%u,%u,%u pal=%02x%02x%02x%02x%02x%02x%02x%02x src=%p%s\n",
        fmt,siz,pal,tlut,w,h,clear,visible,black,visible?(unsigned)(sr/visible):0,visible?(unsigned)(sg/visible):0,visible?(unsigned)(sb/visible):0,
        palette[0],palette[1],palette[2],palette[3],palette[4],palette[5],palette[6],palette[7],source,
        visible&&black*10>=visible*9?"  <-- BLACKTEX":"");
}
volatile uint32_t native_test_combine_trace;
void nativeCombineTrace(uint32_t a,uint32_t b,uint32_t l,uint32_t h){
    if(!native_test_combine_trace)return;
    static uint32_t seen[256][4];static unsigned count;
    for(unsigned i=0;i<count;i++)if(seen[i][0]==a&&seen[i][1]==b&&seen[i][2]==l&&seen[i][3]==h)return;
    if(count==256)return;
    seen[count][0]=a;seen[count][1]=b;seen[count][2]=l;seen[count++][3]=h;
    port_log("COMBINE %08x %08x %08x %08x\n",a,b,l,h);
}
void nativeTextureLoadDiagnostic(unsigned tile,unsigned slot,unsigned bits,unsigned x,unsigned y,unsigned w,unsigned h,unsigned stride,unsigned bytes,unsigned pitch){port_log("TILE tile=%u slot=%u bits=%u origin=%u,%u size=%u,%u stride=%u bytes=%u pitch=%u\n",tile,slot,bits,x,y,w,h,stride,bytes,pitch);}
void nativeRenderDisplayList(void* dl) {
    static int initialized;
    if(!initialized){port_dl_range_register(&__text_start,(uintptr_t)&__end__-(uintptr_t)&__text_start,"native image");nativeRenderInit();initialized=1;}
    nativeRenderBegin();gfx_start_frame();gfx_run(dl);gfx_end_frame();nativeRenderCapture();
}
/* Scene arenas are recycled by the game thread; the renderer's texture cache
 * must not change under a frame that is still being translated. */
extern void __real_portTextureCacheDeleteRange(const void*,size_t);
void __wrap_portTextureCacheDeleteRange(const void* p,size_t n){nativeRenderWait();EVICT_TIMED(2,__real_portTextureCacheDeleteRange(p,n));}
void portResetPackedDisplayListCache(void) {} /* ARM32 Gfx and ROM commands both occupy eight bytes. */
void portPackedDisplayListCacheDeleteRange(const void*p,size_t n) {}
uint32_t native_game_width=320,native_game_height=240;
void GfxSetNativeDimensions(uint32_t w,uint32_t h) {
    nativeRenderWait();
    if(!((w==320&&h==240)||(w==640&&h==480)))fail("game dimensions",w,0);
    native_game_width=w;native_game_height=h;
}
void GfxSetTight4_3ScissorWindow(int enabled) {}
void GfxSetWidescreenFramebufferPersistence(int enabled) {}
int port_capture_register_fb_for_subrect(const void*p,unsigned n,float x,float y,float w,float h) {
    extern int nativeReadbackPhoto(void*,unsigned,float,float,float,float);
    extern void portTextureCacheDeleteRange(const void*,size_t);
    nativeRenderIdle(); /* reads back the last frame through citro3d */
    /* Mark any ROM texture words converted before replacing them with pixels. */
    nativeFixTexture(p,n);
    int result=nativeReadbackPhoto((void*)p,n,x,y,w,h);
    if(!result)portTextureCacheDeleteRange(p,n);
    return result;
}
void port_capture_release_all(void) {}
void portDiagArmImportCapture(int n) {}
