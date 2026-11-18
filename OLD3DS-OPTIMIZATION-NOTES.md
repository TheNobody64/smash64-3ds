# Experimental renderer optimization branch

Branch: `old3ds-renderer-optimizations`

Base branch: `3ds`

This branch is a source-level optimization experiment for the CPU-bound rendering path on original-3DS-class hardware. It adds changes to the generator and related texture-state generator; the base branch already contained audio mixer, timing, texture upload, TMEM, vertex-loop, and build-flag optimizations. **No FPS improvement is claimed:** no ARM build or original-3DS run has been completed for this branch.

## Additional optimizations in this branch

### Renderer hot paths

- **Cache shader metadata in each combiner.** `shader_get_info` now runs when a combiner is generated, not once per triangle. The triangle path reads the cached input count and texture-use flags.
- **256-entry direct-mapped combiner lookup cache.** Keeps the last-combiner fast path and adds a small hash lookup before the existing pool scan. Hash collisions fall back to the scan, so the key remains the complete combiner ID.
- **256-entry byte-to-float color table.** Values are generated as binary32 results of the original `byte * (1.0f / 255.0f)` expression.
- **Deferred MP matrix multiplication.** RSP matrix stack operations mark the combined matrix dirty; it is materialized before vertex transformation and other consumers.
- **Bench-only diagnostic traces.** Per-triangle combiner and frame-850 shade traces are gated by `SSB_BENCH`.

### Avoid redundant state work

- **Texture tile state:** unchanged `G_SETTILE` and `G_SETTILESIZE` commands return early. A changed tile invalidates only the active texture unit(s) it can affect.
- **Texture selection/scaling:** repeated `G_TEXTURE` selections and identical scale factors do not re-mark texture units as changed.
- **Texture lifetime:** texture-ID liveness and frame pinning are checked once per texture unit/ID/frame instead of on every triangle. A changed texture ID or newly encountered frame is checked again; an expired texture still triggers re-import.
- **Fog:** identical fog color and fog multiplier/offset updates are skipped.
- **Geometry and viewport state:** identical geometry mode, viewport, and scissor values do not rewrite RSP/RDP state.
- **Lighting:** unchanged GBI2 light-count values and unchanged uploaded light records do not invalidate the cached transformed light data. The legacy light-count branch retains its explicit recalculate behavior.

## Inherited optimizations already present on the base branch

The source also contains prior work including the optimized 3DS audio mixer, division-free system-tick conversion, hoisted vertex-transform state, UV arithmetic hoisting, TMEM hashing, bounded texture storage/eviction, render-queue state caching and the earlier color/MP-matrix experiments. Those existing changes are not all newly introduced by this pass.

## Deliberately not included

- Direct inline access to private relocation-table globals. `include/reloc_data.h` is a generated-header selector, and the actual table members are private to the BattleShip implementation.
- Divide-free triangle culling: the prior synthetic test found a small set of sub-pixel disagreements, so it is not an exact replacement.
- Lower audio rate, linear-only resampling, fewer simultaneous sound voices, approximate trigonometry, reduced model detail, or a rewritten PICA vertex pipeline. These either change output/gameplay or need a much larger renderer redesign and hardware measurements.
- A new Old-3DS release/CIA packaging target.

## What validation means

The branch workflow checks Python syntax, runs the renderer generator against the pinned graphics backend, verifies the generated C contains the optimization anchors (including the cached shader metadata and texture lifetime checks), packages the full source tree with initialized submodules, and uploads a ZIP.

It **does not** compile generated C/C++ for ARM, link a CIA, run Azahar, run a physical 3DS, or measure frame time. Source-level estimates are not benchmark results.

## Build-system limitation

The repository README still describes the supported release as New-3DS-only. Although an Old-3DS RSF and technical note exist, the current `build_release.py` and `package.py` command-line paths still create the New-3DS title profiles. This branch does not silently relabel that output as an Old-3DS package. A dedicated Old-3DS runtime/RSF/package/verification path is still required.

See [EXPERIMENTAL-OLD3DS-TESTING.md](EXPERIMENTAL-OLD3DS-TESTING.md) for source checks, the existing diagnostic build path and the console test matrix.
