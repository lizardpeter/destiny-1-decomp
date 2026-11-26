# Roadmap

## Renderer (native/gfx)

See [B17 evidence review](b17-renderer-evidence.md) for the imported research, conservative fixes, rejected changes and required validation. The benchmark below predates those fixes.

The draw path was rewritten to a bindless table, memoised pipelines and state diffing, then streamed
textures were given one persistent image and pipelines a saved list. Measured in Earth patrol on an
RTX 5090 at 1080p: 730 ns of CPU per draw, about 5 ms of renderer CPU per frame, 49 pipelines created during
play, 508 textures dropped of 49,763. The frame rate is still 39-65 fps: the game frame is 22.8 ms, of which
recording draws is 9 ms. Open, in priority order:

1. **Colour regression on main.** Since the streaming and pipeline change the world is tinted on the RTX
   5090 and pure blue on lavapipe (HUD correct). The change before it rendered correctly.
2. Find where the other 14 ms of the game frame goes (per-thread run and wait times); the game falls behind
   real time.
3. Recording still waits on the renderer lock (6.6 ms per frame).
4. Draws are skipped: about 2,000 per session for a null vertex buffer, some because the upload ring is full.
5. Textures are re-uploaded in place about 45,000 times per session; confirm whether sky and trees still jump.
6. Field of view has only been run at the default.
7. Renderer CPU target: under 3 ms per frame and under 400 ns per draw.
8. The census of CPU writes into GPU-read memory (docs/gpu-memory-census.md) is unfinished: Tower,
   inventory, director and strikes are not surveyed.
9. Later: take over the engine's graphics-memory pools so textures and buffers are allocated as Vulkan
   objects directly.

## Decompilation

1. Attribute functions to source files. The binary has source-path and assert strings and full RTTI
   (docs/source-tree.md, docs/executable-metadata-rtti.md); a pipeline step should write class and
   source-file tables into `analysis/` so pseudocode can be grouped by file where known.
2. Fix the roughly 2,300 functions whose Ghidra body is 16 or more bytes shorter than the unwind table
   says (`unwind_size` against `size` in `analysis/ps4-0133/functions.tsv`), mostly calls wrongly treated
   as non-returning.
3. A differential test harness: run an original function and its reconstruction on the same inputs inside
   d1-native and compare. Start with self-contained maths and container code.
4. First reconstructions in `src/`, swapped into the runtime one at a time.
5. Graph sync (`tools/graph/sync.py`) from `records/` and `analysis/`.
