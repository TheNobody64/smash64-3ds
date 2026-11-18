# Experimental renderer build and test notes

## Important limitation

This is a **source experiment**, not a ready-to-install Old 3DS release. The current repository's release and packaging scripts still create New-3DS profiles only. The Old-3DS RSF exists, but `build_release.py` and `package.py` do not currently expose or select it. Do not install a CIA built with the New-3DS RSF on an Old 3DS expecting this branch to have converted the application target.

## Get the source

Clone the optimization branch and initialize every submodule, including BattleShip's nested dependencies:

```powershell
git clone --branch old3ds-renderer-optimizations https://github.com/TheNobody64/smash64-3ds.git
cd smash64-3ds
git submodule update --init --recursive
```

Use a Git clone (not GitHub's default source ZIP) when rebuilding from scratch because the graphics backend and BattleShip code are pinned submodules.

## Host-side generator checks

From the repository root:

```powershell
py -3.12 -m compileall -q 3ds/tools
cd 3ds
py -3.12 tools/prepare_render.py
```

The renderer generator reads the pinned graphics backend submodule and writes generated files under `3ds/renderer/`. The GitHub Actions source-package workflow performs these checks before creating the downloadable archive.

## Existing devkitARM benchmark build path

The repository's existing build guide covers the pinned Windows LLVM-MinGW/devkitARM toolchain, ROM extraction, and release pipeline: [docs/BUILD-3DS.md](docs/BUILD-3DS.md).

For an existing diagnostic 3DSX build, after completing that setup and extracting the user's own US 1.0 ROM assets, run from `3ds/`:

```powershell
py -3.12 tools/build.py --prepare
py -3.12 tools/build.py
py -3.12 tools/build_support.py
py -3.12 tools/build_runtime.py --bench
```

That existing `--bench` path is **not** an Old-3DS packaging switch. It produces the repository's current benchmark build configuration; an Old-3DS-specific RSF, runtime define selection, CIA packaging and package verification still need to be connected before this can be described as an Old-3DS installable build.

## What the source workflow verifies

- Python syntax for `3ds/tools/`.
- Successful execution of `prepare_render.py` against the pinned graphics backend submodule.
- Presence of the generated lookup table and deferred-matrix code in the generated renderer source.
- Packaging the repository source and initialized submodules into an artifact.

It does **not** compile the generated C/C++ for ARM, link a CIA, run Azahar, run an Old 3DS, or measure frame rate.

## Console test plan once an Old-3DS target exists

Record the exact hardware model and use the same ROM, display mode, stage, and CPU/player count for the baseline and experimental build. At minimum test:

1. Character select with 1, 2 and 4 fighters.
2. Dream Land or another light stage with 1, 2 and 4 fighters.
3. A visually heavier stage with 4 fighters and common effects active.
4. Pause/resume, stage transitions, and returning to the main menu.
5. Texture-heavy scenes and repeated character animations.

Capture frame-time/FPS logs for the same fixed-duration scenarios. Also watch for matrix/lighting errors, missing geometry, incorrect color/alpha, texture cache corruption, and crashes. Compare the stock build and this branch; do not assume a performance gain from source-level instruction-count estimates.
