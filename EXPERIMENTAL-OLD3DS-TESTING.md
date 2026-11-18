# Experimental renderer build and test notes

## Important limitation

This is a **source experiment**, not a ready-to-install Old 3DS release. The repository README still declares New 3DS as the supported target, and the current `build_release.py` / `package.py` scripts only expose New-3DS title profiles. The Old-3DS RSF and technical documentation do not, by themselves, make the runtime and packaging path Old-3DS-compatible.

## Get the source

Clone the optimization branch and initialize every submodule, including BattleShip's nested dependencies:

```powershell
git clone --branch old3ds-renderer-optimizations https://github.com/TheNobody64/smash64-3ds.git
cd smash64-3ds
git submodule update --init --recursive
```

Use a Git clone rather than GitHub's default source ZIP when rebuilding from scratch, because the graphics backend and BattleShip code are pinned submodules.

## Host-side generator checks

From the repository root:

```powershell
py -3.12 -m compileall -q 3ds/tools
cd 3ds
py -3.12 tools/prepare_render.py
```

The renderer generator reads the pinned graphics backend submodule and writes generated files under `3ds/renderer/`. The GitHub Actions source-package workflow performs these checks and verifies the generated lookup/cache code before creating the downloadable archive.

## Existing devkitARM benchmark build path

The existing build guide covers the pinned Windows LLVM-MinGW/devkitARM toolchain, ROM extraction and release pipeline: [docs/BUILD-3DS.md](docs/BUILD-3DS.md).

For the repository's current diagnostic 3DSX benchmark path, after completing that setup and extracting the user's own US 1.0 ROM assets, run from `3ds/`:

```powershell
py -3.12 tools/build.py --prepare
py -3.12 tools/build.py
py -3.12 tools/build_support.py
py -3.12 tools/build_runtime.py --bench
```

**This is not an Old-3DS packaging switch.** The dedicated Old-3DS RSF selection, runtime defines, CIA packaging and package verification still need to be wired through before calling the output an Old-3DS installable build.

## What the source workflow verifies

- Python syntax for `3ds/tools/`.
- Successful execution of `prepare_render.py` against the pinned graphics backend submodule.
- Generated renderer anchors for the color lookup, deferred matrix, combiner hash cache, cached shader metadata, per-frame texture lifetime checks, redundant tile-state guards, and lighting-state guards.
- Packaging of the repository source and initialized submodules into an artifact.

It does **not** compile generated C/C++ for ARM, link a CIA, execute Azahar, launch on an Old 3DS, or measure FPS/frame time. It does not prove that the optimizations are visually equivalent on the console.

## Hardware test plan once an Old-3DS target exists

Use the same ROM, console, stage, display mode, player count and fixed-duration run for baseline and optimized builds. At minimum test:

1. Character select with 1, 2 and 4 fighters.
2. A light stage with 1, 2 and 4 fighters.
3. A visually heavier stage with four fighters and common effects active.
4. Pause/resume, stage transitions, and returning to the main menu.
5. Texture-heavy scenes and repeated character animations.

Capture the existing `THREADS`, `STAGES`, `TEXTURE_CACHE`, `RENDER`, texture reclaim, and degraded-event logs. Check for matrix/lighting errors, missed geometry, incorrect color/alpha, stale or corrupted textures, audio issues, and crashes. Compare frame times as well as FPS. Do not infer performance gains from the fact that a source optimization is present.
