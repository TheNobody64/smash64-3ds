# PERF-NOTES: Old 3DS perf bundle for builder 1.2.4

Read `FINDINGS.md` first. It lists every finding, what is implemented here, and what is not.

**Status: nothing in this bundle has been compiled for ARM or run on a 3DS or Azahar.** The x86 tests below prove equivalence of the replaced code, not speed or ARM build success. Your own `docs/OLD-3DS.md` already records that 1.2.3 and 1.2.4 are uncompiled, so apply this on top of that baseline and expect to fix compile errors.

## What changes

| File | Change |
|---|---|
| `3ds/src/mixer_3ds.c` (new) | Optimized copy of `vendor/BattleShip/port/audio/mixer.c`: env-mixer settled-ramp fast path, unrolled ADPCM, cached `getenv`. Bit-exact with the original. |
| `3ds/tools/build_support.py` | Compiles `src/mixer_3ds.c` instead of the vendor mixer; adds `-fno-math-errno`; adds `SSB_GAME_CFLAGS` hook; adds `import os`. |
| `3ds/tools/build.py` | `game_flags()` gets `-fno-math-errno` and the `SSB_GAME_CFLAGS` hook. |
| `3ds/tools/build_runtime.py` | Runtime flags get `-fno-math-errno`; renderer files get the `SSB_RENDER_CFLAGS` hook. |
| `3ds/src/platform_3ds.c` | `native_time_ticks()` without software 64-bit division. |

The vendor submodule is not modified. Line endings of each file are preserved (CRLF for `build.py`, `build_support.py`; LF for the rest).

## Apply

Copy the files under `3ds/` over your `builder/3ds/` tree, or review and apply `patches/perf.diff` (the four modified files) and add `3ds/src/mixer_3ds.c`. `patches/mixer_vs_vendor.diff` shows how the new mixer differs from the vendor one.

## Experiments (off by default)

Each changes the flags stamp, so the next build is a clean rebuild.

```bat
rem Unaligned loads/stores (verify first that clang defaults to strict-align; see FINDINGS C1)
set SSB_GAME_CFLAGS=-munaligned-access
set SSB_EXTRA_CFLAGS=-munaligned-access

rem Type-based alias analysis for the Fast3D interpreter only.
rem Do not enable until the float->int32 pun at gfx_pc.c:899-902 is removed (FINDINGS C2/R8).
set SSB_RENDER_CFLAGS=-fstrict-aliasing
```

Test one at a time, and keep emulator and hardware results separate.

## Re-run the equivalence tests

Needs an x86 Linux/WSL box with `gcc`:

```sh
BATTLESHIP=/path/to/builder/3ds/vendor/BattleShip sh verify/run_tests.sh
```

| Test | Result when I ran it |
|---|---|
| Mixer, reference vs `mixer_3ds.c` | 1.2 M env-mixer + 300 k ADPCM calls, **bit-exact**. Mutation check: two deliberately broken variants were both caught. |
| Tick conversion vs original formula | 27 M values (including every step boundary): **identical**. Verbatim function from the patched `platform_3ds.c`: 18 M values, identical. |
| Colour LUT | 256/256 identical. |
| Divide-free cull (not applied; evidence for FINDINGS R8) | 0.117 % disagreement on 40 M synthetic triangles, all sub-pixel slivers. **Not bit-exact.** |

**Not tested:** any ARM build, any run on hardware or Azahar, a 32-bit x86 build (the multilib headers were not installed, though the tested code uses fixed-width types throughout).

## What to measure after applying

Compare the `audio=` field of your `THREADS` log line before and after. That is where the mixer change should show, and it is the cheapest check that the bundle did anything.
