#pragma once
#include <stddef.h>
#include <stdint.h>
/* Which machine the process is really running on, and the tunables that
 * depend on it (hardware.c).
 *
 * The decision is "which capability profile did the system give this
 * process", not "is this a New 3DS": a CIA built for the original 3DS
 * (SSB_TARGET_OLD3DS) still starts in legacy mode on a New 3DS, with 268 MHz,
 * no L2 cache, 64 MB and no third core. Everything that depends on those
 * resources therefore keys off nativeHwFull() instead of APT_CheckNew3DS(). */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NativeHwTuning {
    /* Core for the display-list translation thread; -1 renders on the main
     * thread. Full New 3DS profile: 2. Legacy profile: -1 until measured. */
    int render_core;
    /* Share of the system core an application may use (APT, percent). Only
     * applied when render_core is 1. */
    unsigned syscore_percent;
    /* Bound on the asset cache that sits in front of the pack file. */
    size_t asset_cache_bytes;
    /* The whole pristine asset pack is kept in memory only if this much
     * ordinary heap would remain afterwards; 0 never keeps it. */
    size_t resident_pack_reserve_bytes;
    /* Size of the game's scene arena on the legacy profile; 0 picks a third
     * of the ordinary heap (6 MiB minimum, 16 MiB maximum). */
    size_t scene_arena_bytes;
} NativeHwTuning;
/* Detects the profile once and, on the legacy profile only, applies
 * SSB_DATA_DIR/tuning.txt. Safe to call repeatedly. */
void nativeHwInit(void);
/* 1 = the full New 3DS profile (804 MHz, L2, 124 MB, core 2 usable). */
int nativeHwFull(void);
/* APT_CheckNew3DS() alone: the console model, whatever mode the title got. */
int nativeHwConsoleIsNew(void);
const NativeHwTuning* nativeHwTuning(void);
/* Called by the game's scene-arena allocation (src/sys/taskman.c). */
size_t port_scene_heap_initial(size_t requested);
size_t port_scene_heap_fallback(size_t failed);
void port_scene_heap_note(const char* what,size_t size,void* result);
#ifdef __cplusplus
}
#endif
