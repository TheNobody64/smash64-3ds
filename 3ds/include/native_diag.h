#pragma once
/* Render-state diagnostics for one-off visual bugs (the black Yoshi egg).
 *
 * port_diag_open() is called where such an object is created; for the next
 * DIAG_FRAMES frames the renderer then writes one DIAG line per distinct draw
 * state and per texture upload to boot.log, which is enough to see the
 * combiner, tiles, palette and lighting the object was really drawn with.
 * Outside those windows the only cost is one comparison per triangle. */
#include <stdint.h>

#define NATIVE_DIAG_FRAMES 240

extern volatile uint32_t native_diag_until;
extern volatile uint32_t ssb_frame_count;
static inline int nativeDiagActive(void) { return (int32_t)(native_diag_until - ssb_frame_count) > 0; }

typedef struct {
    uint32_t cc0, cc1, om_l, om_h, geometry;
    uint8_t prim[4], env[4], shade[4];
    uint8_t prim_lod, lights, ambient[3], light0[3], used[2];
    struct {
        uint8_t fmt, siz, palette, cms, cmt, bytes[8];
        uint16_t tmem, line;
        uint32_t width, height;
        const void* origin; /* where TMEM at this tile was last loaded from; NULL = never loaded */
    } tile[2];
    uint8_t palette[16]; /* first eight entries of the palette tile 0 uses */
} NativeDiagState;

void port_diag_open(const char* what);
void nativeDiagDraw(const NativeDiagState* s);
void nativeDiagTexture(const uint8_t* rgba, unsigned pitch, unsigned w, unsigned h, unsigned fmt, unsigned siz,
                       unsigned palette_index, unsigned tlut_type, const uint8_t* palette, const void* source);
