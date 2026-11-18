# Old 3DS performance pass — implementation report

Date: 2026-11-18  
Repository: https://github.com/TheNobody64/smash64-3ds  
Base branch: `3ds`  
Implementation branch: `perf/old3ds-safe-pass-2026-11`

## Scope

This pass concentrates on changes that are source-supported, exact or opt-in, and useful to the Original/Old 3DS. It does not claim an FPS gain: the repository's own notes state that no current Old 3DS ARM benchmark has been collected for this source state.

## Implemented

### CPU and audio

- **Wire in the existing Old-3DS mixer optimization.** `3ds/src/mixer_3ds.c` already contains the settled-envelope fast path, unrolled ADPCM predictor, and cached dump-environment lookups. The support build previously compiled `vendor/BattleShip/port/audio/mixer.c`, so the optimized implementation was not used. `build_support.py` now compiles the local optimized mixer while continuing to use the vendored audio DMA implementation.
- **Disable math errno bookkeeping in optimized target builds.** Added `-fno-math-errno` to the game, support-library, and runtime compilation flag sets. This lets Clang optimize math operations such as `sqrtf` without preserving the C library `errno` side effect. It does not alter the requested mathematical result.
- **Add experiment-only compiler flag overrides.** `SSB_GAME_CFLAGS` applies to game and support code; `SSB_RENDER_CFLAGS` applies to renderer translation/submission sources. They are empty by default, so they do not change normal flags. They allow controlled A/B tests without editing build scripts.

### CPU and rendering

- **Use a bit-exact colour conversion lookup.** The renderer generator now emits table accesses instead of repeating `uint8 -> float` conversion and multiplication by `1/255` for every colour component of every emitted vertex. The 256-entry table occupies 1 KiB of read-only data and is generated from the exact float32 expression previously used.
- **Extend the existing host-side renderer math test.** The test now compares all 256 entries in the shipped lookup table against the old expression bit-for-bit; the runner adds the include path for the new header.

## Modified files

- `3ds/tools/build.py`
- `3ds/tools/build_support.py`
- `3ds/tools/build_runtime.py`
- `3ds/tools/prepare_render.py`
- `3ds/include/native_color_lut.h` (new)
- `verify/test_render_math.c`
- `verify/run_tests.sh`
- `OLD3DS-OPTIMIZATION-REPORT.md` (this report)

## Verification status

| Check | Result |
|---|---|
| Repository source review | Completed via GitHub API against the `3ds` branch |
| Renderer LUT equivalence | Test added for all 256 byte values; local execution is not claimed |
| Mixer bit-equivalence | Existing project test and notes cover the optimized mixer; this pass did not rerun the test because the pinned BattleShip submodule is not available in this execution environment |
| Python build-script syntax | Not run against a full checkout |
| ARM compile / link | Not run: this environment has no full submodule checkout, pinned Windows LLVM-MinGW/devkitARM toolchain, or supplied ROM/assets |
| Azahar runtime / physical Old 3DS | Not run |
| FPS, CPU, GPU, RAM before/after | Not measured; no fabricated results |

## Known limitations and next steps

- The changes are committed to an isolated branch and do not rewrite the `3ds` base branch.
- The exact colour LUT is a safe instruction-count reduction, but its performance impact must be measured on an Old 3DS; cache behavior can affect the result.
- The mixer fast path is present in this repository but still needs a full ARM build and an on-device audio regression check.
- The largest unverified candidate remains per-triangle Fast3D state validation. It needs the existing stage profile to determine whether it is truly hot before introducing epoch-based cached render state.
- Matrix-result laziness, DL-bound-check inlining, texture-state change suppression, and texture-cache eviction policy are intentionally not included here. Their correctness depends on all command/state mutation paths and should be implemented with targeted tests rather than speculative broad edits.
- The repository's README currently describes New 3DS as the main supported target; Old 3DS build/tuning information is in `docs/OLD-3DS.md`. The existence of an Old 3DS build profile does not establish a verified performance target.

## Build and use

Fetch this branch, initialize the pinned submodules as described in `docs/BUILD-3DS.md`, configure the pinned toolchain, then run the host tests and `build_release.py --target old`. Run the `--bench` build on physical hardware and capture the `perf` CSVs before comparing FPS or changing any non-exact rendering or audio behavior.
