# Original Nintendo 3DS, 3DS XL and 2DS target

**Status: the original-3DS target compiles, links and packages into a CIA (a
Windows builder run built every source file, the renderer and
`smash64-old3ds.cia`). Its package check then stopped on a New-3DS-only
assumption in `verify_package.py` (the NCCH platform byte), fixed in builder
1.2.1; whether the fixed check passes is not yet known. Nothing has been run on
an original 3DS or in an emulator, and no frame rate has been measured on any
hardware.**

## What was verified, and what was not

| Item | State |
|---|---|
| `tools/old3ds_config_test.py` (RSF differences, title IDs, save folders, SMDH flags) | Ran, passed (needs no toolchain) |
| Python build scripts | Byte-compiled only |
| C/C++ changes (`hardware.c`, `render_thread.c`, `native_assets.cpp`, `platform_3ds.c`, `performance.c`) | Compiled and linked for the New 3DS target by the builder (first revision). The later button-mapping and vertex-copy edits were only reviewed by hand and checked by regenerating the renderer source; the `SSB_TARGET_OLD3DS` branch has never been compiled |
| `tools/assets_cache_test.py` (extended for the legacy profile) | **Not run** after the change (it passed before the change) |
| CIA for the original 3DS | Built by builder 1.2.0 (compile, link, renderer, makerom all succeeded); `verify_package.py` then failed on the platform byte (expected 2, a New 3DS value) and was fixed in 1.2.1. Verification result for 1.2.1 pending |
| Launch on an original 3DS / 2DS | Untested |
| Frame rate on any model, 2D or stereoscopic | Not measured |

The first source ZIP had the two submodules empty, so the renderer could not be
inspected at that point. The builder ZIP carries the backend sources, which
showed that the GPU submission layer (`include/native_render_queue.h`) already
merges adjacent draws, caches viewport, scissor, TEV, texture, depth, blend and
fog state, and sets the render target once per eye. There was no redundant
state setting to remove there. The renderer change made is described below.

## What a "legacy profile" is

The code now distinguishes the console model from the capability profile the
title was started with (`include/native_hw.h`, `src/hardware.c`):

* **Full profile**: a New 3DS title (`smash64.rsf`, 804 MHz, L2 cache, 124 MB
  mode, core 2). Behaviour is unchanged: render thread on core 2, 1 MiB asset
  cache, whole asset pack resident.
* **Legacy profile**: an original 3DS, 3DS XL or 2DS, **and** the Old 3DS
  build running on a New 3DS (a `ctr`-platform title runs in legacy mode
  there: 268 MHz, 64 MB, no third core). Both are treated alike.

Conservative defaults for the legacy profile, none of them measured yet:

| Setting | Legacy default | Reason |
|---|---|---|
| Rendering | on the main thread | the only mode that is always correct; core 1 is shared with system services |
| Asset pack | stays on the card, 2 MiB decoded-asset cache | the 16.34 MiB resident pack was sized for an 84 MiB heap; the game's scene arenas use the same heap afterwards |
| 804 MHz request | not made | New 3DS only |

## Experiments on a console: `tuning.txt`

On the legacy profile the game reads `sdmc:/3ds/ssb64-old/tuning.txt`
(`/3ds/ssb64-old-unlocked/` for the unlocked title), one `key=value` per line.
It is ignored on the full profile.

```
render_core=1            # -1 main thread (default) or 1 (system core)
syscore_percent=30       # share of core 1 requested from APT, 5-80
asset_cache_kib=2048     # 256-16384
resident_reserve_mib=0   # 0 = never keep the pack resident; N = keep it only if N MiB of heap would remain
scene_arena_mib=0        # 0 = automatic (a third of the ordinary heap, 6-16 MiB); 4-16 forces that size
```

`game.log` (written by bench and development builds) starts with `HARDWARE`,
`ASSET` and `RENDER` lines that record what was actually applied. If APT
refuses the CPU limit, or the thread cannot be created, rendering silently
stays on the main thread and the log says so.

Why `render_core=1` is an experiment and not a default: on an original 3DS the
second core belongs mostly to the system (GSP, HID, DSP services). The task of
the render thread is the same as on New 3DS, and the existing synchronization
(the game does not touch display lists, vertices or assets while the frame is
translated; see the comment at the top of `render_thread.c`) is unchanged. What
is unknown is whether the time share APT grants is enough to translate a frame
inside 16.67 ms at 268 MHz, and whether taking it makes input, GPU submission
or audio worse. Try 30, then higher values, and read the `THREAD`/`PACE` lines.

## Changes that help the original 3DS specifically

* **Vertex copy loop** (`prepare_render.py`, `gfx_citro3d_draw_triangles`):
  loop-invariant values (texture scales, flags, stride) are read once per batch
  instead of per vertex, and source/destination are `__restrict`. The code was
  reloading them because the build uses `-fno-strict-aliasing` and the loop
  stores floats. The arithmetic is unchanged operation for operation, so the
  vertex data is bit-identical. Expected effect: a few percent of vertex-copy
  time; unmeasured. The two-color path (`renderTwoColorTris`) is untouched.
* **Grab on consoles without ZL/ZR**: the original 3DS and 2DS cannot press
  ZL/ZR, which made grab a two-button action. There R = grab and L = shield;
  New 3DS (and the original-3DS title on a New 3DS) keep the normal layout.
* **No 804 MHz request, a larger decoded-asset cache, main-thread rendering**:
  see the legacy-profile table above.

Not done because it cannot be justified without numbers: lowering model or
texture detail, a second render thread by default, or changing how the game
steps (the original timing is kept).

## First launch on a console: the soft-lock, and `boot.log`

The first original-3DS CIA that ran showed a black top screen and "LOADING GAME"
on the bottom, and HOME did nothing. The cause found in the source: when the
game's 16 MiB scene arena (`gPortSceneHeapSize`, `src/sys/taskman.c`) could not
be allocated, the code did `while (TRUE);`, a loop that never returns control to
the system. The same pattern guarded `syTaskmanMalloc`. Both now end the
program (`abort()`), which gives HOME back. That the allocation really is what
failed on that console is the best explanation, not yet a confirmed one.

On the original-3DS titles the arena request is no longer "16 MiB, then
whatever fits". It starts at a third of the ordinary heap (6 MiB minimum, 16 MiB
maximum; `scene_arena_mib` in `tuning.txt` overrides it) and, if that cannot be
allocated, falls back through 12, 10, 8, 6, 5 and 4 MiB. The reason is the
opposite failure from the first launch: a 16 MiB arena that *does* fit can take
most of an original 3DS's heap and leave the asset cache, the audio banks and
the loader's own containers short, which would show up in the scene that loads
the most (stage select) rather than at boot. The original game ran in 4 MiB of
N64 RAM, so a third of the heap should still be generous, but nothing had
measured the peak use; the `ARENA` lines below now do.

`/3ds/ssb64-old/boot.log` (`/3ds/ssb64-old-unlocked/` for the other title) is
written by every original-3DS build and flushed line by line, so it survives a
freeze. It holds the first 300 log lines: the hardware profile, `MEM` lines
(heap, linear heap, free linear memory and malloc state, before the arena
request and around each attempt), and `BOOT` milestones. After those 300, lines
that report a failure or a memory figure are still written (up to 500 more):
`ABORT`, `FAIL`, `DEGRADED`, `...exhausted`, `...failed`, `ARENA` and `SCENE`.
Before this, the cap could be used up long before a crash that happens in a
later scene, and the `ABORT` line itself was dropped.

* `SCENE scene=N frame=F heap_used=... heap_free=... linear_free=... textures=N (K KiB) degraded=D`
  is written each time the game changes scene: ordinary heap and linear memory
  at that moment, how many textures are resident, and how many times the
  renderer had to drop work or shrink a texture so far.
* `ARENA scene used=U KiB of S KiB` is written as the next scene starts, and
  says how much of the arena the scene that just ended used. This is the figure
  that decides how small the arena can safely be.
* `crash.log` (same folder) is written when the program aborts: the abort's
  caller address and the last 48 log lines of any kind.
* A hard fault (a bad memory access rather than `abort()`) cannot be caught, but
  the last `SCENE` line still says which scene was running.

## Optimization pass (builder 1.2.3): what the CPU is doing, and what changed

This pass started from three reports on an original 3DS (builder 1.2.2): about
10 FPS at the start of the game, about 18 FPS on character select, and a crash
on entering stage select. **Nothing below was compiled for ARM or run on a
console by whoever wrote it; the changes were checked on a PC (see "How it was
checked") and every performance statement is an expectation until the numbers
in "Measuring" say otherwise.**

### What already runs on the PICA200, and what cannot move

The renderer is the SM64-derived citro3d backend (`vendor/sm64-3ds`) adapted by
`prepare_render.py`. The GPU already does vertex projection (`shader.v.pica`),
rasterization, texturing, the combiner (TEV), blending, fog, depth and stencil,
and the final display transfer. What stays on the CPU is what the N64 did on its
RSP and in game code, and the PICA200 has no equivalent for it:

* **Game logic and the N64 scheduler/audio threads** (`game_other_ms`,
  `audio_synth_ms` in `perf/menus.csv`). Audio synthesis (ADPCM decode,
  resampling, mixing) is RSP microcode ported to C; the DSP can mix PCM, but not
  this pipeline.
* **Display-list translation** (`cpu_submit_ms`): the Fast3D interpreter in
  `gfx_pc.c` transforms each vertex by the model-view-projection matrix, lights
  it, clips, and writes the 48-byte GPU vertex. The projection multiply is the
  only part the shader could take; by estimate (not measured) it is a few dozen
  cycles per vertex, so even a busy 3000-vertex frame is under a millisecond. The rest of that stage is
  state tracking, clip decisions that need the transformed `w`, and lighting
  that depends on N64 semantics, which cannot be handed to a fixed shader
  without changing the picture. It was not moved: a large rewrite for under a
  millisecond, with clipping risk.
* **Texture decode and swizzle** when a texture is first seen. This is cached by
  content hash, so it matters in bursts (scene loads) and when the cache
  thrashes.

So "move more work to the GPU" is not where most of the frame is. The numbers
that say where it is are already recorded by the game (`perf/menus.csv`, one row
per scene); `tools/old3ds_report.py` reads them.

### Changes

* **Stage-select crash (suspected cause, not confirmed).** Two candidates,
  both about memory that only the original 3DS runs short of:
  1. The scene arena was requested at 16 MiB and kept the first size that fit,
     which can leave the ordinary heap too small for the scene that loads the
     most. It is now sized from the heap (see the arena paragraph above).
  2. A scene can need more linear memory in textures than is free. The renderer
     used to `abort()`; on the original 3DS it now evicts unused textures, then
     halves the texture it is trying to allocate (down to 8x8) and logs
     `DEGRADED`. UVs stay normalized to the full size, so the texture is a
     reduced copy of the same picture: an alpha-weighted average from builder
     1.2.4 (1.2.3 took every Nth texel, which can pick only transparent or
     black border texels), and only while memory is exhausted. The New 3DS
     keeps its hard stop.
  The vertex buffer (1 MiB) and the draw queue (4096 draws) also used to abort
  when full; on the original 3DS the frame's extra work is skipped and logged.
  If stage select still crashes, `crash.log` and the `SCENE`/`ARENA` lines in
  `boot.log` now say which of these it is, or that it is something else.
* **Texture reserve** (`gfx_citro3d_upload_texture`): the texture cache kept
  4 MiB of linear memory free and evicted the whole cache to do so. On a machine
  with far less linear memory than the New 3DS that can make every upload
  evict everything not used this frame, so the next frame decodes, swizzles and
  uploads the same textures again. The reserve is 1.5 MiB on the original 3DS
  (4 MiB unchanged on the full profile), and the `Texture reclaim` log line is
  rate-limited so a thrash cannot flood `boot.log`.
* **Texture swizzle** (`native_texture_swizzle.h`): the upload loop looked up
  the source column and row for every texel and, with `-fno-strict-aliasing`,
  reloaded its tables after each store. The block walk now builds the column/row
  maps once, loads each row pointer once, reads consecutive columns directly,
  and uses a constant Morton table. The bytes written are identical to the old
  loop for every input tested. It shortens the burst of work when a scene
  loads; it does not change steady-state frames where nothing uploads.
* **Per-scene memory and failure logging**, `crash.log`, and
  `tools/old3ds_report.py` (a table of where each scene's time goes, plus a
  before/after comparison of two `menus.csv` files).

### Looked at and left alone

* **Bottom-screen compositor:** already redraws only when its state changes and
  at most 30 times a second; the per-pixel arithmetic is cheap. Not a cause.
* **Audio mixer `getenv` calls per voice:** real, estimated at roughly 0.1 ms a
  frame, so not worth an edit to the vendored mixer.
* **Native PICA texture formats for I/IA** (LA8, L8, A8) would cut texture
  memory and upload work by two to four times, but the combiner reads texture
  alpha in ways that need checking against the picture on a console.
  Worth doing once there is a console to compare against.
* **A GPU-side vertex path, a render thread by default, compiler changes
  (LTO, -O3):** not justified without the numbers.

### How it was checked

On a PC, with the pieces that do not need a 3DS: the swizzle against a copy of
the old loop on 60,000 random textures (clamp, mirror, wrap, 16 and 32 bit,
reduced allocations); the new log ring and `crash.log` writer; the generated
upload function with stubbed citro3d types (normal, reduced and minimum
allocations); `prepare_render.py` still produces the renderer; `old3ds_config_test.py`
passes. Not run: an ARM build, `assets_cache_test.py` (needs the Windows clang
the builder installs), anything on a console.

## Builder 1.2.4: fighters, the black egg, and what could not be measured

New observations on an original 3DS after 1.2.3: the main menu is smooth; a
stage with no fighters yet runs about 30 FPS and drops to about 20 once they
load; character select with four players is about 20 FPS and worsens with
more; Yoshi's egg is black.

### Why fighters are expensive (from the code, not from a profile)

Each fighter is a tree of joints. For every joint the display list sets a
matrix, loads up to 32 vertices, draws triangles, and switches textures and
combiner state between parts (face, eyes, body). In `gfx_pc.c` that costs:

1. per `G_MTX`: a 4x4 multiply of the model-view and projection matrices;
2. per `G_VTX` vertex: the transform, lights (a fighter is lit), clip flags;
3. per triangle: culling, the combiner lookup, texture-tile checks, the
   texture-coordinate setup and a 48-byte write into the vertex buffer;
4. per texture or state change: a flush into the draw queue (`nativeQueueDraw`
   copies the tile and combiner state, merges with the previous draw if equal).

Steps 2 and 3 scale with triangle count, so four fighters cost about four times
one, and the cost of an empty stage's 30 FPS (background, scheduler, audio) is
paid on top. That matches the observed 30 to 20 drop and the character-select
numbers, but it is a reading of the code. The share of each step is what the
`STAGES` profile reports (below); until it is read, this ranking is a
hypothesis. Already done in earlier builds: vertex-loop invariants (matrix,
light colors) are hoisted, colors multiply by 1/255, texture constants are
hoisted out of the vertex loop, draws are merged, textures are cached by
content.

### What changed in 1.2.4

* **Texture-coordinate constants are cached** (`native_uv.h`). The setup for
  each textured triangle recomputed, for both texture units, 1/2^shift,
  the tile origin and 1/width, 1/height: up to eight floating-point divisions.
  The ARM11 VFP does not pipeline a divide (roughly 15 cycles), so that is on the
  order of 100 cycles a triangle, which is on the order of a millisecond a
  frame at a few thousand triangles. (An estimate from instruction counts, not a
  measurement.) The values only change when a tile's shift or origin or the
  texture size changes, so they are now computed then. The result is
  bit-for-bit what the old expressions gave (checked on four million random
  states); it only removes the repeated work.
* **Frame-850 trace output is off in release builds.** The `SHADE` and `UV`
  traces wrote about 330 lines to `boot.log` at frame 850 and used up its
  first-lines budget. They now need a bench build.
* **Reduced textures average instead of point-sampling** (see above).
* **Yoshi egg diagnostics** (below).

### The black Yoshi egg: not fixed, and how to find the cause in one run

The egg's display list is in the ROM, which was not available, so the cause
could not be read directly. Black means the colour path produced zero while the
shape (alpha) still drew. Three mechanisms in this renderer can do that:

1. **A combiner input the frontend maps to zero.** `color_comb_component` in
   `gfx_pc.c` implements the inputs SM64 uses. `PRIMITIVE_ALPHA`,
   `SHADE_ALPHA`, `ENV_ALPHA`, `TEXEL1_ALPHA`, `COMBINED_ALPHA`, `NOISE`,
   `CENTER`/`SCALE`, `K4` and `K5` fall through to zero, so a colour
   multiplied by one of them renders black. Smash can use these (SM64 never
   does), and this fits an egg that keeps its outline.
2. **A second texture that was never loaded.** Material animation
   (`gcDrawMObjForDObj`) cross-fades a current and next frame with
   `TEXEL1` and `PRIM_LOD_FRAC`. The vendored Whispy canopy bug note documents
   the same failure: the second tile is unloaded and reads as zero.
3. **A palette that was not loaded or is read from the wrong place.**

`port_diag_open` is now called when the entry egg, the egg lay effect or the
egg throw is created. For the next 240 frames boot.log gets `DIAG` lines: one
per distinct draw state (geometry mode, other-mode words, the combiner decoded
into names with a marker on inputs read as zero, primitive/environment colour,
lighting, each tile's format, size, palette and whether its TMEM was ever
loaded) and one per texture upload (size, transparent and black texel counts,
mean colour, first palette entries; the line says `BLACKTEX` when 90% of the
visible texels are black). To use it, play a match with Yoshi (or egg lay /
throw), then send `boot.log`. Lines read as:

* `<-- input the frontend reads as ZERO` on the cc line: mechanism 1. The fix
  is to support that input in the combiner (it needs a TEV change, which is
  why it was not guessed at).
* `loaded_from=(nil)` on a used tile 1, or a `BLACKTEX` texture on tile 1:
  mechanism 2.
* a `BLACKTEX` upload with an all-zero `pal=`: mechanism 3.

If none of those show, the lines still give the real state to reason from.
Outside the window the cost is one comparison per triangle.

### Things to run to get the numbers this pass could not

1. Build the bench variant with `SSB_EXTRA_CFLAGS=-DSSB_STAGE_PROFILE` set in
   the environment (this is read by `build_runtime.py`) and run the same
   scenario list as in "Measuring": character select with 1 and 4 players, an
   empty stage, a stage with 1, 2 and 4 fighters. The `STAGES` line splits
   the frame into vertex, matrix, texture and flush time. The profile adds
   overhead of its own, so use it to rank, not as the final FPS.
2. Run `azahar_profile.py` for the function ranking. It screens ideas; it is not
   a result for the console.
3. `tools/old3ds_report.py menus.csv` for the per-scene table; compare runs
   with `old3ds_report.py before.csv after.csv`.
4. System core: `tuning.txt` with `render_core=1` and `syscore_percent=10`,
   `20`, `30` (see above), one run each. Not made a default.

### Looked at and not changed

* **`gfx_sp_vertex`:** the per-vertex work is already close to the minimum
  (hoisted matrix, light colors and coefficients, integer light accumulate). The
  remaining divisions are only in the fog path. A faster formula would change
  results in the last bit, so it was not attempted without a profile saying
  vertex work dominates.
* **`nativeQueueDraw` state compares:** per draw it copies the tile and
  combiner state and compares it with the previous draw. Compact keys would
  save a fraction of a microsecond a draw; it matters only if `draw_calls_per_frame`
  is in the high hundreds.
* **`-O3`, LTO, `-Ofast`:** not tried. The original 3DS has a small cache and no L2,
  so larger code can be slower, and no ARM compiler or console was available.

## Titles, IDs and saves

The original-3DS titles are separate from the New 3DS ones so that neither can
replace the other or share a save by accident.

| Title | Title ID | Process / product code | SD folder |
|---|---|---|---|
| Smash 64 (New 3DS, unchanged) | `000400000FF64000` | `Smash64` / `CTR-P-SS64` | `/3ds/ssb64/` |
| Smash 64: All Unlocked (New 3DS, unchanged) | `000400000FF64100` | `Smash64U` / `CTR-P-SS6U` | `/3ds/ssb64-unlocked/` |
| Smash 64 (original 3DS/2DS) | `000400000FF64200` | `Smash64L` / `CTR-P-SS6L` | `/3ds/ssb64-old/` |
| Smash 64: All Unlocked (original 3DS/2DS) | `000400000FF64300` | `Smash6LU` / `CTR-P-S6LU` | `/3ds/ssb64-old-unlocked/` |

The save file format is the same, so a save can be copied between folders by
hand (`save.bin`). The original-3DS CIA does not set the SMDH `new3ds` flag
and uses `smash64-old3ds.rsf` (`Platform: ctr`, no extended memory mode,
268 MHz, no L2 flag). `AccessControlInfo` services and system calls are
identical to the New 3DS file; `old3ds_config_test.py` enforces that.

## Building

**One-click builder (Windows):** run `Build Smash 64 CIA.bat`. Builder 1.2.0 asks
`1` New 3DS, `2` original 3DS/2DS, `3` or Enter both, then produces
`Smash64-Old3DS.cia` and `Smash64-Old3DS-Unlocked.cia` (and/or the New 3DS pair)
in `Copy to SD card\cias`. Skip the question with
`start.ps1 -Target old` (or `new`, `both`). An earlier builder's work folder is
rebuilt automatically because the version changed.

**Scripts:**

Same toolchain and prerequisites as `BUILD-3DS.md`; nothing is downloaded by
the new options.

```powershell
cd 3ds
# installable CIAs for the original 3DS family (New 3DS output is unchanged without --target)
python tools/build_release.py --rom "C:/Games/SSB64/baserom.us.z64" --target old
#   build/release/Smash64-Old3DS.cia, Smash64-Old3DS-Unlocked.cia
python tools/build_release.py --rom "C:/Games/SSB64/baserom.us.z64" --target both   # all four

# development .3dsx (run from hbmenu on the console)
python tools/build_runtime.py --release --target old     # build/release-old/ssb64-release-old.3dsx
# logging build with scripted input and metrics (reads bench.txt, test-input.txt, tuning.txt)
python tools/build_runtime.py --bench --target old       # build/bench-old/ssb64-bench-old.3dsx
```

`verify_package.py --target old` checks the CIA, including that the extended
header carries the legacy bytes (no L2/804 MHz, no extended memory). Those
expected bytes (`0,0,4,48`) follow the exheader layout and have not been
compared with a real `makerom` output; if `verify_package.py` fails on that
line for a package that installs and runs, correct the table in `package.py`,
not the game.

## Measuring: what to run and what to read

Do not use emulator frame rates as an original-3DS result.

1. Install the bench `.3dsx` on the console (SD `3ds/`), plus
   `sdmc:/3ds/ssb64-old/bench.txt` (`scene stage fkind frames slider fixedAudio syncRender capture`,
   e.g. `-1 -1 -1 3600 0 0 0 0` for 2D with the default scene) and, for the same
   input every run, `test-input.txt` (`begin end buttons x y` per line).
2. Play or script: 1v1; four fighters with CPUs; busy stages; menus/results;
   a session of 30+ minutes. Repeat with `render_core=-1` and `render_core=1`.
3. Collect `/3ds/ssb64-old/game.log` and `/3ds/ssb64-old/perf/*.csv`.
4. Read, per scenario:
   * `PACE` (`fps`, `tick_ms`): the end-to-end rate and mean tick cost.
   * `THREADS` (`game`, `scheduler`, `audio`): CPU cost of each N64 thread, ms/frame.
   * `THREAD` (`submit_ms`, `render_ms`, `main_wait_ms`): translation cost and
     how long the game waited for it. `render_ms` above ~14 with
     `render_core=-1` means translation alone nearly fills the frame.
   * `STAGES` when built with `SSB_EXTRA_CFLAGS=-DSSB_STAGE_PROFILE`: vertex, matrix, texture, flush split.
   * `DETAIL`: draw calls, texture binds, command bytes.
   * In each `match-N.csv` header: `p50/p95/p99_ms_le`, `worst_ms`, and the new
     `frames_ge_16_75ms` / `frames_ge_33_5ms` (frames that missed one and two
     vblanks; 0.25 ms histogram resolution).
   * `MEMORY ... linear_free`, `ASSET ... resident=`, the `events.csv`
     scene-entry times (load stalls) and `audio_drops`/`audio_underruns`.
5. Record the result per console model, screen mode (2D vs slider up) and
   `tuning.txt` contents. Until a number is in that table it is unmeasured.

Decision points the numbers settle:

* If `game+scheduler+audio` alone is already near 16.67 ms at 268 MHz, no
  renderer change can reach 60 FPS and the question becomes game-side cost.
* If `render_ms` is the large term, a core-1 thread (limited by APT) or less
  translation work per frame (batching, caching, vertex work on the GPU) is
  the lever; those renderer changes belong in the `sm64-3ds` adaptation done by
  `prepare_render.py`, after profiling with `SSB_STAGE_PROFILE`.
* If the frame is GPU bound (`gpu_previous_ms` in the CSV), CPU-side work will
  not help and stereoscopic mode (two eyes) is the first thing to measure.

## Results

| Console | Mode | Scenario | Avg FPS | p95 / p99 ms | Frames ≥16.75 ms | Notes |
|---|---|---|---|---|---|---|
| Original 3DS / XL / 2DS | 2D | any | not measured | | | |
| Original 3DS / XL / 2DS | stereo | any | not measured | | | |
| New 3DS (regression) | any | any | not re-measured | | | |
| Azahar | any | any | not run | | | |

## Known gaps

* The changes in the 1.2.3 and 1.2.4 sections have not been compiled for
  ARM or run on a console. Whether the stage-select crash is fixed, and the
  size of any FPS change, are open until a run reports them.
* The renderer is the SM64 backend plus the adaptations in `prepare_*.py`; the
  vertex stage is still an unmodified Fast3D interpreter on the CPU.
* The one-click builder (`easy_build.py`, `make_builder_zip.py`) still builds
  the New 3DS titles only; use `build_release.py --target old`.
* No physical-console, emulator or New 3DS regression numbers exist yet.
