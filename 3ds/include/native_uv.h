#pragma once
/* Texture-coordinate constants for a triangle (included by the adapted
 * gfx_pc.c, see tools/prepare_render.py, and by tools/uv_test.py).
 *
 * Every textured triangle used to recompute, for both texture units, the S and
 * T scale (1/2^shift), the tile origin, and 1/width and 1/height of the
 * texture. That is up to eight floating-point divisions per triangle, and the
 * ARM11's VFP does not pipeline a divide (about 15 cycles each). The inputs are
 * a tile's shift and origin fields and the texture's size, which change far
 * less often than once per triangle, so the results are kept per unit and
 * recomputed only when an input changes. The expressions are the old ones, so
 * the values are bit-for-bit the same; the powers of two come from a table
 * because 1/2^n is exact in a float. */
#include <stdint.h>

static const float nativeInvPow2[11]={1.0f,0.5f,0.25f,0.125f,0.0625f,0.03125f,0.015625f,0.0078125f,
                                       0.00390625f,0.001953125f,0.0009765625f};

struct NativeUvConstants{
    unsigned ss,st,uls,ult,used,w,h;
    int valid;
    float scaleS,scaleT,offsetS,offsetT,invW,invH;
};

static inline void nativeUvConstants(struct NativeUvConstants* c,unsigned ss,unsigned st,unsigned uls,unsigned ult,
                                     unsigned used,unsigned w,unsigned h){
    if(c->valid&&c->ss==ss&&c->st==st&&c->uls==uls&&c->ult==ult&&c->used==used&&c->w==w&&c->h==h)return;
    c->ss=ss;c->st=st;c->uls=uls;c->ult=ult;c->used=used;c->w=w;c->h=h;c->valid=1;
    c->scaleS=(ss>10?(float)(1u<<(16-ss)):nativeInvPow2[ss])/32.0f;
    c->scaleT=(st>10?(float)(1u<<(16-st)):nativeInvPow2[st])/32.0f;
    c->offsetS=uls/4.0f;c->offsetT=ult/4.0f;
    c->invW=used?1.0f/w:0;
    c->invH=used?1.0f/h:0;
}
