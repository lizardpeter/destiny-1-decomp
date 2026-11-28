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

### Status: textures and buffers done, shaders not yet

`Renderer::native_texture_*` in `native/gfx/nrender.cpp` owns every engine texture (resource types
1-3). The engine's bind and resident-mips events (`resource_bind` and `h_mips` in
`native/src/native_gfx.cpp`) copy the bytes the new descriptor describes; a loader thread detiles them and
uploads them into the texture's one image; draws bind by the engine object they captured. Removed with it:
copying every captured texture's source bytes at draw capture, and the address-keyed prefetch and
resident-mips paths.

Measured, Earth patrol on lavapipe, against main `259df30`:

| | before | after |
|---|---|---|
| Texture binds failed per report interval | about 1,380 | 0 |
| Registered textures drawn but not owned natively | | 0 |
| Pre-pass CPU per frame | 75.8 ms | 13.6 ms |
| Renderer CPU per frame (all parts) | 101.7 ms | 35.7 ms |
| Images | 7,509 created, 802 dropped | about 3,400 live, none recreated in play |

The player's arms, weapon and the snow, black on main, are textured again.

`Renderer::native_buffer_*` owns every engine vertex and index buffer (resource types 4 and 6) the
same way: the bind event copies the bytes the engine hands over, the loader uploads them into the
object's own device-local allocation, and draws bind the allocation of the object they captured.
Moves and re-points keep it, because they never change the bytes: traced in Earth patrol, about
20,000 buffer moves, every one with identical contents. A new resource bound into an object (a new
object id in its identity) gets a new allocation; a sweep frees the allocations of objects whose
resource is gone. Index buffers also keep their bytes on the CPU for the few primitive types whose
indices are read there. Removed with it: the per-epoch geometry cache for engine buffers, the
geometry source snapshots and their leases, the loader's buffer re-preparation on every move, and
the per-draw geometry pinning at capture.

Measured, against main `ccae578`, lavapipe:

| | before | after |
|---|---|---|
| Buffer re-preparations caused by moves | about 20,000 per Earth patrol run | 0 (every move kept) |
| Draws dropped for missing geometry | none on main; about 460 per report interval on an Intel Arc once lists were recorded on engine threads (test-38) | 0 in Earth patrol and orbit |
| Engine buffers in Earth patrol | | 4,729, all owned and uploaded, none uploaded at first use in play |

The first-use case still exists: a draw whose buffer the loader has not reached yet uploads it into
the submission it is in, ahead of its draws.

The address-based texture path that engine textures used before is deleted: the speculative
texture loader jobs, the split low/high-mip stream bookkeeping and its history, retained old
texture versions, owned source snapshots, and the engine-resource branches of `upload_texture` and
`texture_view` (about 1,200 lines). An engine texture the native table does not own is drawn with
a placeholder, never read from guest memory (none in Earth patrol or orbit).

Still to do in piece 1: what remains on the address path is memory that is no engine resource:
render targets (resolved by surface, see [draw-interface.md](draw-interface.md)) and textures the
CPU writes (the dynamic-texture slots). Both get native ownership next. The lease registry now only
provides the lock that keeps a draw from copying a texture descriptor the engine is rewriting; its
pinning is unused and goes with it.

## Piece 4 moved up: the frame-rate unlock causes the shaking

The reported shaking (orbit planet, trees, sky jumping sideways, on both an Intel Arc and an RTX 5090)
is not the renderer. With `fps` above 30, `game_time_hook` in `native/src/hooks.cpp` rewrites the game's
tick rate every frame to match that frame's duration (`1/elapsed`, clamped to 30..1000, written to the
time-control period and to the game-time globals at `026bc880` +4/+8). The simulation was authored for a
fixed 30 Hz tick, so anything moved over time is computed with one frame's tick and shown with another's.

Confirmed on the owner's laptop: `D1_FPS=30` with `D1_TICK_RATE_FORCE=30` (a constant 30 Hz tick, as on
the console) removed the shaking, and game time matched real time (9.97 s per 10 s, against 8.9 s with
the variable tick). It never appeared in headless runs because at about one frame per second the rate is
always clamped to 30.

The fix is piece 4, not another timing patch. Chosen design: a fixed simulation tick at the frame
rate, one tick per frame, with no interpolation. The tick rate is now configured once at its source
and the per-frame rewrite is gone; see [frame-scheduler.md](frame-scheduler.md). What remains is
fixing the systems that assume 30 ticks a second; until they are, the default stays `fps = 30`.

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
