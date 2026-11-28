# Draw interface (native port, piece 2)

The engine draws through its own device-context API: a context `D` per thread (one main, six workers,
`007e3740` / `007e3a20`), setters that change state on it, draw calls, and command lists that are closed
and submitted. On the console those functions write PS4 GPU commands. Piece 2 makes them record Vulkan
directly, so nothing in the draw path is PS4 GPU state any more. See [native-port-plan.md](native-port-plan.md).

## Where things stand

Already intercepted (`native/src/native_gfx.cpp`):

| Area | Functions | What happens now |
|---|---|---|
| Draws | `007e15d0` `007e16a0` `007e1710` `007e17c0` `007e1840` `007e1960` | Replaced: each call captures a `Draw` record from `D`'s fields |
| Clear, dispatch | `007e2a00`, `007e0780` | Hooked: captured, original body still runs |
| Vertex / index streams, constants | `007dec20`, `007dee30`, `007df630` | Replaced |
| Command lists | `007dcce0` `007dcec0` `007dd060` `007dd6a0` `007de950` `007dea90` `007db810` | Replaced: lists become native lists submitted on a queue thread |
| Command buffer memory | `007ef7d0`-`007efc00` | Replaced |
| GPU copies (blitter) | `007da8b0`-`007db4b0` | Replaced |
| Gnm library calls made by engine code | `00f7feb0` alloc, `00f8b500` set constant buffers, `00f8f3a0` DMA, `00f809f0`/`00f80a80`/`00f80ae0` predication and occlusion, `00f80c90`/`00f80d70` end-of-pipe labels | Intercepted at the library entry |
| GPU resources | resource callbacks, `007d4480` | Native ownership (piece 1) |

The setters (render targets, viewport, blend/depth/stencil presets, primitive setup, shaders,
textures, samplers, vertex layout) still run their original bodies, but every Gnm library call that would
write the console command buffer is already a no-op (`kNoops` in `nd::install`), and the library calls whose
effects the renderer needs are intercepted. So no console GPU command is produced; the setters only
update `D`, and the capture reads `D`. What remains PS4-shaped is the *form* of that state: GCN register
words, T#/V# descriptors and GCN shader binaries, which the renderer decodes per draw.

Two engine helpers draw through the library directly instead of through the captured draw calls, so
their draws are dropped with the no-op'd library calls: `007e2cb0` (a full-screen pass, called about ten
times per frame from `007eaf00`, which works on the dynamic-texture slots, and from `007e6420`, a
frame-level pass that also calls `007e5590` present-prepare) and `007e0040` (texture copy, from the
dynamic-texture code at `00818ae0`/`00818ca0`). The renderer already reads CPU-written dynamic textures
directly (`h_unlock` at `00818fa0`), which may make these copies redundant; whether anything visible is
missing has not been established. To check.

The renderer then turns each captured `Draw` (GCN register encodings, T# and V# descriptors, GCN
shader binaries) into Vulkan when the list is recorded: a serial pre-pass on the queue thread, then
per-draw pipeline, geometry, descriptor and command work. Measured on an Intel Arc at 30 fps in Earth
patrol, about 9,300 draws: pre-pass 6-7 ms, commands 7-8 ms, submit 2.4 ms per frame; the queue thread
spends about 20 ms per frame, which is what limits the frame rate.

### Submissions

The engine marks completion with end-of-pipe labels (`00f80c90`/`00f80d70`), about 166 per frame in the
world. The renderer used to end a Vulkan submission at every label, so each frame cost about 166
`vkQueueSubmit` calls, fences and command-buffer begin/end pairs. Now one engine submission call becomes one
Vulkan submission: the labels it contains are collected and written, with their end-of-pipe signals, once
the GPU has finished that whole submission. A label still never becomes visible before the work in front
of it, and the engine cannot wait on a label in the middle of its own submission call, so nothing waits
longer than before.

Measured, Earth patrol on lavapipe: 158 submissions per frame before, 28 after; frames identical. The
remaining 28 are separate engine submission calls and splits at CPU-side memory writes; they go to one
per frame when lists are recorded on the engine threads (step 3).

### The console library calls in the device layer

The device layer (`007dc180`-`007e5600`) calls about 80 functions of the statically linked Gnm library
(`00f7d010`-`00f8f0e0`). Two kinds:

- **Register builders** that only compute a register word stored in `D` (for example `00f819f0`,
  `00f81a10`, `00f81a30` build the blend control at `D+0x25d8`; `00f81b30`-`00f81ec0` the depth/stencil
  control at `D+0x25dc`/`D+0x25e0`). Pure computation on engine state.
- **Context calls** that take the GPU context `*(D+0x50)` and write the console command buffer or its
  shadow state: blend control `00f7e7c0`, target mask `00f7e740`, depth control `00f80790`, viewport
  `00f80340`, primitive setup `00f7fe20`, VS / PS `00f8b790` / `00f8b8c0`, textures `00f8afd0`, samplers
  `00f8b410`, constant buffers `00f8b500`, render targets `00f8f0e0` / `00f7eec0`, draw packets
  `00f7f1e0` / `00f7f260` / `00f7f320`, pre-draw commit `00f8df60` / `00f8e880`, and so on.

The context calls are already no-ops. Step 2 replaces the register words themselves with
native state built once from the engine's preset tables (`015d16c4` blend, `015d2394` depth,
`015d2424` polygon offset, `015d1cd0`/`015d1d70` stencil).

### Measured: where the serial pre-pass goes

Earth patrol, lavapipe, about 7,900 draws per frame (`CPU detail` line in the renderer statistics):

| Part | ms per frame |
|---|---|
| Render-target resolution | 0.9-2.7 |
| Material preparation | 9-39 |
| of which shader identity lookups (2 per draw) | 1.6-3.8 |
| of which shader texture-slot masks (2 per draw) | 1.5-2.6 |
| of which per-slot texture descriptor hashing, segment cache probes and view resolution | the rest |
| Geometry preparation | 4.6-8.7 |

All of it is the renderer re-deriving, once per draw and on one thread, what the engine already decided
when it called a setter. Tuning these loops would be another patch on the translator; step 3 removes
them, because the draw is recorded with the state the setters already resolved.

### Render targets: identified by surface, not by draw order

Measured in Earth patrol: about 37% of all texture reads in a frame (about 26,000) sample the game's own
render targets, through about 110 engine objects over about 60 addresses. None of them are engine
resources. The engine itself names a target by its surface: the render-target table at `026f2398`
(entries of `0xe0` bytes: a Gnm render target at `+0xa0`, its T# at `+0xcc`; `0081b130` builds a
target set from entry ids, `0081c5a0` binds it through `007dfd10`) hands out T# copies, and the sampling
code keeps those copies in its own structures. Most target sets are built on the stack per call.

The renderer used to give a T# the image drawn into its address most recently. That answer depends on
the order lists reach the renderer, which rules out recording lists on the engine threads. Now a T#
gets the image whose surface it describes: same address, size and format. 99.7% of sampled targets
match exactly one image that way; depth (sampled through a colour format) matches by size. The rare
rest, a T# whose size matches no target at its address (34x34 over an 80x45 target in the
dynamic-texture slots, likely drawn by one of the dropped helper draws above), still gets the
address's current target.

Still order-dependent, to be replaced before lists are recorded on engine threads: targets the CPU
rewrites (`target_current`), feedback copies of attachments a pass samples, occlusion queries and
predication, and GPU results read back into guest memory.

### Lists recorded on the engine threads (step 3, stage 1): done

Each command list without GPU memory copies is recorded into its own Vulkan command buffer by the
engine thread that closes it (`record` in `native/src/native_gfx.cpp`, `Renderer::record_list`); the
queue thread fills the constants a list inherits from earlier lists, and submits the recorded
lists in the engine's order. Lists from the GPU copy queue (memory moves) still run at
submission.

It needed native buffers first: on the old per-epoch geometry cache, every engine buffer move
made a list recorded early miss its geometry, and the dropped draws showed as surfaces flickering
to black (test-38/39 on an Intel Arc). With native buffers no draw is dropped.

Measured on the owner's Intel Arc, Earth patrol, `fps = 120`, 2560x1440 (test-42):

| | test-37 (queue thread records) | test-42 (engine threads record) |
|---|---|---|
| Frame rate | 30 (capped) | 103-111 |
| Queue thread per frame | 13-18 ms | 0.35 ms |
| Engine threads' recording per frame (all threads) | | 7-8 ms, of it 1.6-2.3 ms waiting for the renderer lock |
| GPU timestamp spans per frame | | about 2.3 ms |
| Draws dropped | 0 | 0 |
| Slow frames in the world | 83 | 15 |

Lists with GPU memory copies are recorded too. They are not separate copy lists: the engine's copy
queue writes into ordinary draw lists (measured: 390 of 400 lists that still ran at submission had
both, about 190 copies and 73 draws each). Their copies are applied on the CPU when the queue thread
reaches the list in submission order (`apply_memory_writes`), before the GPU runs it, as the
console's GPU does them; no draw reads guest memory a copy writes (native textures and buffers own
their data, and no constant or index range overlapped a copy destination). With that the queue
thread translates nothing: 0.00 ms per frame in Earth patrol on lavapipe.

Recorded lists already go to the GPU in one `vkQueueSubmit2` per engine submission call (grouped by
`submit_recorded_group`); merging across calls would let a label the engine waits on between calls
wait for later work, so it is not done.

Still to do in this stage: less work under the renderer lock (pre-pass, feedback copies and
resource resolution, about 2 ms per frame on the Arc); two recording stalls of 130-145 ms without
new shaders or pipelines, not yet explained; and deleting the submission-time translation path
once the GPU copy lists no longer need it.

## Target

- **Setters keep only the engine's own state.** Each one updates exactly the `D` fields its original
  body writes (other engine code reads them) and nothing else: no Gnm calls, no console command buffer.
- **State is native when it is set.** A setter that has a Vulkan meaning stores it in Vulkan form on the
  thread's native context: blend/depth/stencil/raster presets become pipeline-state indices once,
  shaders become pipeline stages, textures become bindless views (piece 1), render targets become
  attachments.
- **Draws are recorded when the engine draws.** Each engine thread records into its own Vulkan secondary
  command buffer at the draw call. No `Draw` record, no pre-pass, no second translation.
- **The engine's own barriers drive synchronisation.** The console engine already marks where the GPU
  must wait (`007e03a0`, `007e2000`, render-target changes); those become Vulkan barriers. Nothing is
  inferred from memory addresses.
- **Lists map to command buffers.** Begin and end of an engine list are begin and end of its command
  buffers; submission executes them in the engine's order.

## Order

1. ~~Setters stop producing console commands~~: already the case (see above).
2. Native per-thread state: blend, depth, stencil and polygon-offset presets converted once from the
   engine's tables into Vulkan pipeline state; shaders and render targets resolved at set time.
3. Draws recorded on the engine threads, in two stages:
   1. Each command list is recorded into its own secondary command buffers by the engine thread that
      built it, when the list is closed. The DMA and data writes inside lists are applied in issue order
      so a list recorded early reads the right bytes. The queue thread only gathers the recorded lists
      and submits them, once per frame.
   2. Recording moves to the draw call itself; the `Draw` record, the pre-pass and list-time
      translation are deleted. This is the step that removes the remaining per-draw translation cost.
4. Barriers from the engine's own flush, decompress and render-target calls.
5. The helpers above that draw through the library (`007e2cb0`, `007e0040`) recorded natively, if
   anything they draw turns out to be missing.

Each step: built in CI, the game run into orbit and Earth patrol with frames compared, CPU per frame
measured, and the replaced path deleted.
