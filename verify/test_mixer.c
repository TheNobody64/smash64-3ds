#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECL(p) \
  void p##aSetBufferImpl(uint8_t, uint16_t, uint16_t, uint16_t); \
  void p##aLoadBufferImpl(uintptr_t); void p##aSaveBufferImpl(uintptr_t); \
  void p##aSetVolumeImpl(uint16_t, uint16_t, uint16_t, uint16_t); \
  void p##aEnvMixerImpl(uint8_t, int16_t*); \
  void p##aLoadADPCMImpl(int, uintptr_t); void p##aSetLoopImpl(uintptr_t); \
  void p##aADPCMdecImpl(uint8_t, int16_t*); \
  void p##aResampleImpl(uint8_t, uint16_t, int16_t*);
DECL(ref_) DECL(opt_)

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

#define IN_ADDR 0x4f0
#define DL 0x9d0
#define DR 0xb40
#define WL 0xcb0
#define WR 0xe20
#define OUT_ADDR 0x1000 - 0x600  /* ADPCM / resample output region */

typedef struct { void (*setbuf)(uint8_t,uint16_t,uint16_t,uint16_t); void (*load)(uintptr_t); void (*save)(uintptr_t);
  void (*setvol)(uint16_t,uint16_t,uint16_t,uint16_t); void (*env)(uint8_t,int16_t*);
  void (*loadadpcm)(int,uintptr_t); void (*setloop)(uintptr_t); void (*adpcm)(uint8_t,int16_t*);
  void (*resample)(uint8_t,uint16_t,int16_t*); } Impl;
static Impl REF = { ref_aSetBufferImpl, ref_aLoadBufferImpl, ref_aSaveBufferImpl, ref_aSetVolumeImpl, ref_aEnvMixerImpl,
                    ref_aLoadADPCMImpl, ref_aSetLoopImpl, ref_aADPCMdecImpl, ref_aResampleImpl };
static Impl OPT = { opt_aSetBufferImpl, opt_aLoadBufferImpl, opt_aSaveBufferImpl, opt_aSetVolumeImpl, opt_aEnvMixerImpl,
                    opt_aLoadADPCMImpl, opt_aSetLoopImpl, opt_aADPCMdecImpl, opt_aResampleImpl };

static void put(Impl *I, uint16_t addr, const void *p, int n) { I->setbuf(0, 0, addr, n); I->load((uintptr_t)p); }
static void get(Impl *I, uint16_t addr, void *p, int n)       { I->setbuf(0, addr, 0, n); I->save((uintptr_t)p); }

static int16_t rnd16(void) {
  switch (rnd() % 6) { case 0: return (int16_t)(rnd() % 3 ? 32767 : -32768); case 1: return 0;
                       case 2: return (int16_t)((int)(rnd() % 400) - 200); default: return (int16_t)rnd(); }
}

static long env_trials, env_midsettle, env_wetzero, env_alreadysettled, env_nonzero_wet, env_ramping_end;
static int test_env(void) {
  int nbytes = 16 * (1 + rnd() % 23);                  /* <= 368 bytes = 184 samples */
  int16_t in[184], d[4][184], st_ref[40], st_opt[40];
  for (int i = 0; i < 184; i++) in[i] = rnd16();
  for (int b = 0; b < 4; b++) for (int i = 0; i < 184; i++) d[b][i] = (rnd() % 4 == 0) ? rnd16() : (int16_t)((int)(rnd() % 2000) - 1000);
  memset(st_ref, 0, sizeof st_ref); memset(st_opt, 0, sizeof st_opt);

  for (int call = 0; call < 3; call++) {
    uint8_t flags = (call == 0) ? 0x01 : 0x00;        /* INIT then CONTINUE x2 */
    uint16_t vl = rnd16(), vr = rnd16(), tl = rnd16(), tr = rnd16();
    int mode = rnd() % 5;
    uint16_t rml, rll, rmr, rlr;
    #define RATE(m,l) switch (mode) { \
      case 0: m = 0; l = 0; break; \
      case 1: m = 0x7FFF; l = 0xFFFF; break; /* saturate sentinel */ \
      case 2: m = (int16_t)(rnd() % 3) - 1; l = (uint16_t)rnd(); break; \
      case 3: m = (int16_t)rnd(); l = (uint16_t)rnd(); break; \
      default: m = (int16_t)((int)(rnd() % 200) - 100); l = (uint16_t)rnd(); }
    RATE(rml, rll); mode = rnd() % 5; RATE(rmr, rlr);
    int16_t dry = (rnd() % 8 == 0) ? 0 : rnd16();
    int16_t wet = (rnd() % 2) ? 0 : rnd16();
    Impl *Is[2] = { &REF, &OPT };
    int16_t *sts[2] = { st_ref, st_opt };
    int16_t dd[2][4][184];
    for (int w = 0; w < 2; w++) {
      Impl *I = Is[w];
      put(I, IN_ADDR, in, 368); put(I, DL, d[0], 368); put(I, DR, d[1], 368); put(I, WL, d[2], 368); put(I, WR, d[3], 368);
      if (flags & 1) {
        I->setvol(0x08, (uint16_t)dry, 0, (uint16_t)wet);          /* A_AUX */
        I->setvol(0x04 | 0x02, vl, 0, 0);  I->setvol(0x04, vr, 0, 0);   /* A_VOL L/R */
        I->setvol(0x02, tl, rml, rll);     I->setvol(0x00, tr, rmr, rlr);/* A_RATE L/R */
      }
      I->setbuf(0, IN_ADDR, 0, nbytes);
      I->env(flags, sts[w]);
      get(I, DL, dd[w][0], 368); get(I, DR, dd[w][1], 368); get(I, WL, dd[w][2], 368); get(I, WR, dd[w][3], 368);
    }
    if (memcmp(dd[0], dd[1], sizeof dd[0]) || memcmp(st_ref, st_opt, sizeof st_ref)) {
      printf("ENV MISMATCH call=%d nbytes=%d dry=%d wet=%d\n", call, nbytes, dry, wet); return 1; }
    /* coverage bookkeeping from saved state: steps live at save[8..11] */
    int32_t sl, sr; memcpy(&sl, st_opt + 8, 4); memcpy(&sr, st_opt + 10, 4);
    env_trials++; if (wet == 0) env_wetzero++; else env_nonzero_wet++;
    if (sl == 0 && sr == 0) env_alreadysettled++; else env_ramping_end++;
  }
  return 0;
}

static long ad_trials;
static int test_adpcm(void) {
  uint8_t book[256]; for (int i = 0; i < 256; i++) book[i] = rnd();
  if (rnd() % 3 == 0) { int16_t *b = (int16_t*)book; for (int i = 0; i < 128; i++) b[i] = (int16_t)((int)(rnd() % 4000) - 2000); }
  int count = (rnd() % 2) ? 128 : 256;
  uint8_t src[1024]; for (int i = 0; i < 1024; i++) src[i] = rnd();
  int16_t loop[16], state0[16]; for (int i = 0; i < 16; i++) { loop[i] = rnd16(); state0[i] = rnd16(); }
  int nb = 32 * (1 + rnd() % 11);
  uint8_t flags = (uint8_t)((rnd() % 4 == 0 ? 1 : rnd() % 3 == 0 ? 2 : 0) | (rnd() % 4 == 0 ? 4 : 0));
  int16_t out[2][600], st[2][16]; Impl *Is[2] = { &REF, &OPT };
  for (int w = 0; w < 2; w++) {
    Impl *I = Is[w]; memcpy(st[w], state0, 32);
    I->loadadpcm(count, (uintptr_t)book); I->setloop((uintptr_t)loop);
    put(I, IN_ADDR, src, 512);
    I->setbuf(0, IN_ADDR, OUT_ADDR, nb);
    I->adpcm(flags, st[w]);
    get(I, OUT_ADDR, out[w], 2 * (16 + nb / 2) + 0);
  }
  if (memcmp(out[0], out[1], 2 * (16 + nb / 2)) || memcmp(st[0], st[1], 32)) { printf("ADPCM MISMATCH flags=%d nb=%d\n", flags, nb); return 1; }
  ad_trials++; return 0;
}

int main(void) {
  int bad = 0;
  for (int t = 0; t < 400000 && !bad; t++) bad |= test_env();
  for (int t = 0; t < 300000 && !bad; t++) bad |= test_adpcm();
  printf("envmixer calls compared : %ld (wet==0: %ld, wet!=0: %ld)\n", env_trials, env_wetzero, env_nonzero_wet);
  printf("  ended settled (step==0): %ld, still ramping at end: %ld\n", env_alreadysettled, env_ramping_end);
  printf("adpcm calls compared     : %ld\n", ad_trials);
  printf(bad ? "RESULT: MISMATCH\n" : "RESULT: bit-exact on all trials\n");
  return bad;
}
