# Roadmap

## Renderer (native/gfx)

The draw path was rewritten to a bindless table, memoised pipelines and state diffing. Measured in Earth
patrol on an Intel Arc laptop: 1,740 ns of CPU per draw, about 16 ms of renderer CPU per frame at 7,000
draws, 36-52 fps. Open, in priority order:

1. Sky, trees and textures visibly jump. Streamed textures are dropped and recreated (22,702 dropped of
   31,330 created in one session). A streamed texture must keep one image for its lifetime.
2. Pipelines are created during play (4,926 in one session), each a hitch. Build them when the shader is
   loaded and persist the list.
3. Recording still waits on a shared lock (6 ms per frame) and does not run in parallel.
4. Renderer CPU target: under 3 ms per frame and under 400 ns per draw.
5. Field of view: the hook is in and `fov` is read from the settings; it has only been run at the default.
6. The census of CPU writes into GPU-read memory (docs/gpu-memory-census.md) is unfinished: Tower,
   inventory, director and strikes are not surveyed.
7. Later: take over the engine's graphics-memory pools so textures and buffers are allocated as Vulkan
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
