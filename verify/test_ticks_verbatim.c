#include <stdint.h>
#include <stdio.h>
#define SYSCLOCK_ARM11 268111856ull
static uint64_t g_t;
static uint64_t svcGetSystemTick(void){ return g_t; }
static inline uint64_t ssbMulHi64(uint64_t a,uint64_t b){
    uint32_t a0=(uint32_t)a,a1=(uint32_t)(a>>32),b0=(uint32_t)b,b1=(uint32_t)(b>>32);
    uint64_t p00=(uint64_t)a0*b0,p01=(uint64_t)a0*b1,p10=(uint64_t)a1*b0,p11=(uint64_t)a1*b1;
    uint64_t mid=(p00>>32)+(uint32_t)p01+(uint32_t)p10;
    return p11+(p01>>32)+(p10>>32)+(mid>>32);
}
uint64_t native_time_ticks(void) {
    _Static_assert(SYSCLOCK_ARM11==268111856ull,"tick conversion constants assume 268111856 Hz");
    uint64_t t=svcGetSystemTick();
    uint64_t q=ssbMulHi64(t,0x2cc1e78333645f2cull);   /* floor(2^64*5859375/33513982) */
    uint64_t r=t*5859375ull-q*33513982ull;            /* remainder, or remainder+C if q is one low */
    if(r>=33513982ull)q++;
    return q;
}
static uint64_t orig(uint64_t t){ return (t/SYSCLOCK_ARM11)*46875000ull+(t%SYSCLOCK_ARM11)*46875000ull/SYSCLOCK_ARM11; }
static uint64_t rs=0x9E3779B97F4A7C15ull; static uint64_t rnd(void){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;return rs;}
int main(void){ long bad=0,n=0;
  for(int bits=1;bits<=50;bits++) for(int k=0;k<300000;k++){ g_t=rnd()&((1ull<<bits)-1); n++; if(native_time_ticks()!=orig(g_t)) bad++; }
  for(uint64_t q=1;q<2000000;q+=3){ uint64_t t0=(q*33513982ull+5859374ull)/5859375ull; for(int d=-2;d<=2;d++){ g_t=t0+d; n++; if(native_time_ticks()!=orig(g_t)) bad++; } }
  printf("verbatim patched function: %ld values, mismatches=%ld -> %s\n",n,bad,bad?"FAIL":"identical"); return bad!=0; }
