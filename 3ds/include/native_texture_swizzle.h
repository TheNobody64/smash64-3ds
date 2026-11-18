#pragma once
/* PICA200 texture swizzle for the 3DS renderer (included by the adapted
 * gfx_citro3d.c, see tools/prepare_texture.py, and by tools/swizzle_test.py).
 *
 * PICA textures are stored as 8x8 blocks of texels in Morton (Z) order. The
 * previous upload loop looked up the source column and row for every texel,
 * recomputed the block position from a 16-entry table, and, because the
 * stores may alias those lookup tables, reloaded all of it each time. Here
 *   - the source column/row maps are built once per axis, without a division
 *     per entry unless the index really wraps,
 *   - each block row loads its source row pointer once,
 *   - a block whose eight source columns are consecutive (any block of an
 *     unwrapped, unclamped, full-size texture) reads the texels directly,
 *   - the position of texel (x,y) inside the block is a constant table.
 * The bytes written are identical to the old loop for every input (the test
 * compares them), so rendering does not change.
 *
 * A texture may be allocated smaller than the cache-key size when linear
 * memory is short: it is then an averaged reduction of the full image. */
#include <stdint.h>
#include <stddef.h>

/* Position inside an 8x8 block of the texel at (x,y): index y*8+x. */
static const uint8_t nativeMortonPos[64]={
     0, 1, 4, 5,16,17,20,21,
     2, 3, 6, 7,18,19,22,23,
     8, 9,12,13,24,25,28,29,
    10,11,14,15,26,27,30,31,
    32,33,36,37,48,49,52,53,
    34,35,38,39,50,51,54,55,
    40,41,44,45,56,57,60,61,
    42,43,46,47,58,59,62,63,
};

#define NATIVE_PACK5551(c) ((uint16_t)((((c)>>3)&31)<<11|(((c)>>11)&31)<<6|(((c)>>19)&31)<<1|((c)>>31)))

/* map[i] = source index for output index i. `step` > 1 reduces resolution.
 * logical: size of the clamp rectangle; period: source size (wrap period);
 * clamp: index is held at logical-1 beyond the rectangle; mirror: odd
 * repeats of the period run backwards (only meaningful with clamp). */
static void nativeAxisMap(uint16_t* map,unsigned count,unsigned step,unsigned logical,unsigned period,int clamp,int mirror){
    for(unsigned i=0;i<count;i++){
        unsigned s=i*step,q=0;
        if(clamp&&s>=logical)s=logical-1;
        if(s>=period){q=s/period;s-=q*period;}
        map[i]=(uint16_t)((clamp&&mirror&&(q&1))?period-1-s:s);
    }
}

/* run[bx/8] is nonzero when the eight source columns of that block column are
 * consecutive (map[i+1]==map[i]+1 for all eight). */
static void nativeColumnRuns(uint8_t* run,const uint16_t* map,unsigned count){
    for(unsigned bx=0;bx<count;bx+=8){
        unsigned ok=1;
        for(unsigned i=1;i<8;i++)ok&=(unsigned)map[bx+i]==(unsigned)map[bx+i-1]+1;
        run[bx>>3]=(uint8_t)ok;
    }
}

static void nativeSwizzle32(const uint32_t* src,unsigned pitch,const uint16_t* mx,const uint8_t* run,const uint16_t* my,
                            unsigned w,unsigned h,uint32_t* dst){
    for(unsigned by=0;by<h;by+=8)for(unsigned bx=0;bx<w;bx+=8,dst+=64){
        const uint16_t* xs=mx+bx;
        if(run[bx>>3]){
            for(unsigned y=0;y<8;y++){
                const uint32_t* row=src+(size_t)my[by+y]*pitch+xs[0];
                const uint8_t* mp=nativeMortonPos+y*8;
                for(unsigned x=0;x<8;x++)dst[mp[x]]=__builtin_bswap32(row[x]);
            }
        }else{
            for(unsigned y=0;y<8;y++){
                const uint32_t* row=src+(size_t)my[by+y]*pitch;
                const uint8_t* mp=nativeMortonPos+y*8;
                for(unsigned x=0;x<8;x++)dst[mp[x]]=__builtin_bswap32(row[xs[x]]);
            }
        }
    }
}

static void nativeSwizzle16(const uint32_t* src,unsigned pitch,const uint16_t* mx,const uint8_t* run,const uint16_t* my,
                            unsigned w,unsigned h,uint16_t* dst){
    for(unsigned by=0;by<h;by+=8)for(unsigned bx=0;bx<w;bx+=8,dst+=64){
        const uint16_t* xs=mx+bx;
        if(run[bx>>3]){
            for(unsigned y=0;y<8;y++){
                const uint32_t* row=src+(size_t)my[by+y]*pitch+xs[0];
                const uint8_t* mp=nativeMortonPos+y*8;
                for(unsigned x=0;x<8;x++)dst[mp[x]]=NATIVE_PACK5551(row[x]);
            }
        }else{
            for(unsigned y=0;y<8;y++){
                const uint32_t* row=src+(size_t)my[by+y]*pitch;
                const uint8_t* mp=nativeMortonPos+y*8;
                for(unsigned x=0;x<8;x++)dst[mp[x]]=NATIVE_PACK5551(row[xs[x]]);
            }
        }
    }
}

/* Whole upload: `src` is width x height RGBA32 texels (the texture as the
 * frontend decoded it; height is only used to fold the vertical index). The
 * cache-key size is w x h (powers of two >= 8); the allocation is sw x sh
 * (w and h shifted right by the same step, still >= 8). dst must hold sw*sh
 * texels of 2 (rgba5551) or 4 (rgba8) bytes. mirror bit 0 is S, bit 1 is T. */
/* Reduced allocation (linear memory exhausted): each output texel is the
 * alpha-weighted average of the stepX*stepY source texels it covers, so a
 * transparent border (black, alpha 0) cannot darken the picture the way a
 * single point sample could. Slow, but only used when memory has run out. */
static uint32_t nativeReduceTexel(const uint32_t* src,unsigned pitch,const uint16_t* mx,const uint16_t* my,
                                  unsigned x,unsigned y,unsigned stepX,unsigned stepY){
    unsigned n=stepX*stepY,sa=0,sr=0,sg=0,sb=0;
    for(unsigned j=0;j<stepY;j++)for(unsigned i=0;i<stepX;i++){
        uint32_t c=src[(size_t)my[y*stepY+j]*pitch+mx[x*stepX+i]];
        unsigned a=c>>24;
        sa+=a;sr+=(c&255)*a;sg+=((c>>8)&255)*a;sb+=((c>>16)&255)*a;
    }
    if(!sa)return 0;
    return (sr+sa/2)/sa|((sg+sa/2)/sa)<<8|((sb+sa/2)/sa)<<16|((sa+n/2)/n)<<24;
}

static void nativeSwizzleReduced(const uint32_t* src,unsigned pitch,const uint16_t* mx,const uint16_t* my,
                                 unsigned sw,unsigned sh,unsigned stepX,unsigned stepY,void* dst,int sixteen){
    for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<sw;x++){
        uint32_t c=nativeReduceTexel(src,pitch,mx,my,x,y,stepX,stepY);
        unsigned pos=((y>>3)*(sw>>3)+(x>>3))*64+nativeMortonPos[(y&7)*8+(x&7)];
        if(sixteen)((uint16_t*)dst)[pos]=NATIVE_PACK5551(c);
        else((uint32_t*)dst)[pos]=__builtin_bswap32(c);
    }
}

static void nativeSwizzleTexture(const uint32_t* src,unsigned width,unsigned height,void* dst,
                                 unsigned w,unsigned h,unsigned sw,unsigned sh,
                                 unsigned logicalW,unsigned logicalH,
                                 int clampS,int clampT,unsigned mirror,int sixteen){
    static uint16_t mapX[1024],mapY[1024];
    static uint8_t run[128];
    if(sw!=w||sh!=h){
        nativeAxisMap(mapX,w,1,logicalW,width,clampS,(int)(mirror&1));
        nativeAxisMap(mapY,h,1,logicalH,height,clampT,(int)((mirror>>1)&1));
        nativeSwizzleReduced(src,width,mapX,mapY,sw,sh,w/sw,h/sh,dst,sixteen);
        return;
    }
    nativeAxisMap(mapX,sw,w/sw,logicalW,width,clampS,(int)(mirror&1));
    nativeAxisMap(mapY,sh,h/sh,logicalH,height,clampT,(int)((mirror>>1)&1));
    nativeColumnRuns(run,mapX,sw);
    if(sixteen)nativeSwizzle16(src,width,mapX,run,mapY,sw,sh,(uint16_t*)dst);
    else nativeSwizzle32(src,width,mapX,run,mapY,sw,sh,(uint32_t*)dst);
}
