# Native port plan

The goal is a PC game built from source: compiled C++ talking to Windows/Linux, Vulkan and native threads,
with no PS4 executable loaded. The whole executable (about 54,500 functions) cannot be rewritten at once, so
the game keeps running while its subsystems are replaced in a fixed order, each one played and measured
before the next starts. When nothing calls into the original executable any more, it stops being loaded.

The rules in [AGENTS.md](../AGENTS.md) apply: one production implementation per replaced subsystem, no
fallback switches, and nothing counts as done until the full game is built, launched and measured.

## Order

| # | Piece | Boundary to replace | Replaces |
|---|---|---|---|
| 1 | **GPU resources** | The engine's 15 graphics resource callbacks (table at `0254ee30`, installed by `007d3400`): bind, release, re-point, move copy/commit, set resident mips, resource builder | Address-based texture and buffer tracking: hashing T# descriptors, epochs, leases, write watching, detiling on the draw path |
| 2 | **Draw interface** | The engine's device-context API (`007dc180`-`007e5590`): render targets, viewport, blend/depth/stencil presets, set VS/PS, set texture/sampler, vertex streams, index buffer, constant buffers, draw, clear, copy, dispatch, command list begin/end/submit | Decoding PS4 command buffers (PM4) and GPU registers |
| 3 | **Shaders** | Shader resources (resource types 8/9/12/13) | Translating GCN shaders while the game runs; all shaders converted ahead of time and cached |
| 4 | **Frame scheduler** | Main loop `00594460`, stage graphs `00593970`, tick accumulator `00591710`/`00591d40`, job workers | Frame pacing built around a console's fixed 30 Hz flip; native threads and waits |
| 5 | **Platform** | File I/O, memory, threads, time, input, audio | The PS4 system-library layer (partly done: libc, FIOS, allocator) |
| 6 | **Game code** | Outward from the engine: simulation, UI, networking | The rest of the executable |

Pieces 1-3 together are the renderer. They come first because they are where the frame time and the
visible faults are.

## Piece 1: GPU resources

Every GPU asset the engine loads (textures, vertex and index buffers, shaders) is announced through the
callback table, by an engine handle. The native design:

- **One native object per engine handle.** `bind` creates it: a `VkImage` with the texture's full mip chain,
  or a `VkBuffer`. `release` destroys it once the GPU has finished with it. The handle is the identity; a
  memory address never is.
- **Contents are converted once**, when the engine says they are ready, on a loader thread: detile, upload,
  done. Nothing is re-hashed or re-checked per frame.
- **Streaming is explicit.** `set resident mips (handle, low, high)` is the engine telling the renderer that
  the sharp top mip arrived (`high`) or was dropped (`low` only). The native image keeps all mips; the call
  uploads the new top mip or moves the view's lowest mip. Nothing is recreated.
- **Moves are explicit.** `move copy` / `move commit` / `re-point` change where the engine keeps the source
  bytes. They do not change the native object.
- **Draws bind by handle.** The texture argument the engine passes to `set texture` is the texture tag's own
  data, so it maps to its handle and native object directly.

### Observed streaming behaviour

Traced with every call to `007d4480` logged (handle, low, high), Earth patrol, standing still:

- Sharp mips arrive as `(handle, 0, high)`: the high allocation holds the whole chain.
- They are dropped as `(handle, high + size of mip 0, 0)`: the low mips are the same allocation, past mip 0.
- 48 textures that received their sharp mip were dropped back together about 30 frames later, at the
  switch from the spawn's third-person camera to first person. Over the next 300 frames (lavapipe) none of
  them, or any other texture, flipped again: the engine's streaming decisions were stable.
- The requests come from CPU jobs that measure visible objects against the camera
  (`0052f780` "extract sobject hi mips", `0052fbb0` / `0052fc50` static and dynamic geometry), not from GPU
  feedback.

The renderer, not the engine, is failing to keep these textures stable. In the same run it rejected about
1,380 texture binds per report interval: full-resolution textures bound at load time (for example
1024x4096, 2048x2048) whose source size it could not establish from addresses, so the draw got a
placeholder instead. A texture that resolves on some frames and not others alternates on screen. Owning
the texture by handle from `bind` removes that inference.

## Acceptance for each piece

1. Builds on Windows and Linux in CI.
2. The full game launched into the same scenes as the baseline (orbit, Earth patrol, Tower), with frames
   compared against the baseline.
3. Measured: frame time and pacing, renderer CPU per frame and per draw, GPU time, memory, and the
   piece-specific counters (for piece 1: textures created, uploaded, dropped, and resident-mip changes).
4. The replaced code path is deleted, not kept behind a switch.

## Baseline

Current main (`259df30`) on Linux, lavapipe (software Vulkan), 640x360, Earth patrol:

| | |
|---|---|
| Draws per frame | 7,850 |
| Renderer CPU per frame | pre-pass 75.8 ms, pipelines 2.0, geometry 3.3, descriptors 4.1, commands 15.1, submit 1.4 |
| Recorded in parallel | 0% |
| Texture binds failed per report interval | about 1,380 (shown as black patches) |

The same scene with the build before 26 November (`4818e99`): pre-pass 10.7 ms and 100% recorded in
parallel.
