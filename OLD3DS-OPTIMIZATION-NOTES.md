# Experimental renderer optimization branch

Branch: `old3ds-renderer-optimizations`

Base branch: `3ds`

This branch carries a small set of additional CPU-side renderer experiments for the original 3DS-class hardware. It does **not** establish that the title is supported on an Old 3DS, and it does not contain a measured FPS improvement.

## Changes

- **Byte-to-float colour lookup:** the renderer generator emits a 256-entry `float` table. The table values are generated as binary32 results of the previous `byte * (1.0f / 255.0f)` operation, rather than approximate decimal values.
- **Deferred model-view/projection multiplication:** matrix commands mark the combined matrix dirty; it is materialized before vertex transforms, before the RSP matrix-word modification path, and at the special-matrix command. The display-list reset materializes a pending matrix before resetting the stack depth.
- **Avoid unchanged combiner flushes:** `G_SETCOMBINE` only flushes queued geometry when either raw command word differs from the previous combiner command.
- **Bench-only diagnostic traces:** per-triangle combiner tracing and the frame-850 shade trace are compiled only when `SSB_BENCH` is defined.

## Not included

- No direct inline access to private relocation-table globals. The public `include/reloc_data.h` is a generated-header selector; exposing internals there without changing the actual relocation-table implementation would be unsafe.
- No texture-cache growth, LRU rewrite, PICA shader offload, or altered audio quality.
- No promise of 60 FPS, frame-time reduction, visual equivalence on hardware, or Old-3DS launch success.

## Build-system status

The repository's top-level README currently describes the supported release as New-3DS-only. Although an Old-3DS RSF and an Old-3DS technical note exist, the current `build_release.py` and `package.py` command-line paths still create the two New-3DS title profiles. This branch does not silently change that packaging behavior. A dedicated Old-3DS target needs to be wired through the runtime and package scripts and then validated.

See [EXPERIMENTAL-OLD3DS-TESTING.md](EXPERIMENTAL-OLD3DS-TESTING.md) for the source checks, available build path, and console test matrix.
