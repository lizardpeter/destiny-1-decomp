# B17 renderer and engine evidence review

Scope: the supplied B17 cumulative and main-relative diffs, compared with main at 42b96086. These are research inputs, not a validated build. Names are analyst-assigned, not original symbols. The reported 8.83 ms/frame ReBAR stall and zero-copy measurements are external patch-note claims; they have not been reproduced on main.

## Findings and implementation disposition

| Finding | Disposition |
| --- | --- |
| Mapped VRAM read in submit_recorded | Main used memcmp on ring.map. Replace with write-only late refresh, retaining the original default and reporting refresh counts, not changed counts. A CPU snapshot/hash optimization needs a benchmark and source lifetime proof. |
| Background texture detile | Main discarded prepared scratch. Pass it to upload_texture for matching full non-streamed surfaces; retain the dedicated path for split streaming. |
| Duplicate draw resolution | Resolve each run after its render targets have been bound, instead of resolving the entire list first as well. |
| Streamed mip blocks | Do not infer a contiguous mip chain from a mapped region extent. Mip 0 and mips 1..N have independent addresses. Refresh on epoch or explicit stale events; never publish a full chain if either upload failed. Conservative reuploads may increase; optimize only with independently verified content identities. |
| Resource invalidation | B17 proposes target-vs-texture generation separation and merged DMA invalidations. Need actual range/ordering tests, including both mip spans and aliasing; not wholesale imported. |
| Direct-mapped geometry/constants | Valuable allocation/lock/unlock hypotheses. Must establish buffer lifetime, coherence, GPU retirement and allocation ABI before replacing original allocation. No zero-copy claim. |
| Render-target copies | B17 adds copy hooks at 7e0040/7e05c0/7e0ae0. Existing analysis identifies shader/compute copies, including depth-to-color, not necessarily raw Vulkan copies. Need format, coordinate, slice, resolve, shader and synchronization equivalence; do not import generic blits. |
| Shader translation | Reject invocation-private LDS as a general substitute for shared GCN LDS, and ignored BUFFER_STORE_DWORD. Recover semantics and test against original shaders. |
| Timing | Reject forced one tick/frame, 240 cap for unlimited, synthetic flip-driven vblank and watchdog suppression without scheduler evidence. Preserve main behavior. |
| Resolution | Do not enlarge arbitrary pools as proof of 4K safety; census allocations and verify rendered frames. |
| Diagnostics | Retain actual reload counts. Do not suppress streamed reuploads. |
| Vulkan barriers / descriptors | Resource-specific dependencies and descriptor lifetime tests needed; do not weaken barriers based on performance guesses. |

## Candidate original functions from B17

Addresses are image-relative for ps4-0133. The pseudocode blocks below exist in main. The exact names and detailed behavior require body/disassembly review; entries not already in the canonical ledger are **candidates**, not reconstructed or verified implementations. Some B17 addresses conflict with Ghidra function boundaries; do not create records for interior addresses.

| Block | Candidates (address: proposed role) |
| --- | --- |
| 00050000 | 00057e60: T# low base dword getter (body returns first u32 only); 00057f80: identity texture tag accessor |
| 000c0000 | 000cb940: resource-memory bind |
| 000d0000 | 000de2a0: link low mip; 000de3b0: memory link lookup; 000de470: low mip handle; 000de510: high mip handle; 000de860: linked-resource iteration; 000de960: unlink |
| 000e0000 | 000e00d0: notify memory relocation; 000e01c0: mip callback thunk; 000e01e0: apply mip handles |
| 000f0000 | 000f4e40: job semaphore signal; 000f4f80: timed wait |
| 00230000 | 0023c730 / 0023c740: proposed stage acquisition/submission (exact starts not confirmed); 0023ce80: idle test; 0023cec0: ready test; 0023cf00: phase job lookup; 0023d160: cross-stage dependency |
| 00590000 | 00593970: kick frame stages; 00593f00: existing time_control_config_build; 00594330: wakeup job (named by original registration string); 00594460: existing main_loop; 00594cb0: vblank timestamp candidate |
| 007d0000 | 007d2730 / 007d2800 / 007d2870: index tag init/lock/unlock; 007d3fc0 / 007d4150: VB/IB creation; 007d6410 / 007d64b0 / 007d6510: vertex tag init/lock/unlock; 007da960: copy queue enqueue; 007db340: existing gpu_copy_queue_flush_item; 007db4b0: copy queue submit; 007de100: stage flush candidate (exact start not confirmed); 007de8c0: existing set_texture_gnm_stage; 007df200: B17 PS texture setter label contradicted by body (writes dummy T# pointer to output); 007df310: existing set_ps_texture |
| 007e0000 | 007e0040 / 007e05c0 / 007e0ae0: existing copy_texture / compute_copy / copy_depth; 007e5750 / 007e57a0: VB/IB allocation candidates (VB decompiler prototype incomplete) |
| 00830000 / 00840000 / 008a0000 | 00833940: UI dynamic VB upload; 00848fc0: cloth dynamic VB lock/unlock candidate (exact start not confirmed); 008a98c0 / 008a99b0: particle dynamic VB lock/unlock |

Frame scheduler hypothesis: simulation, extraction, rendering have separate pipelined job slots and explicit dependency edges. Instrument stage acquisition, job completion, semaphore waits, GPU completion and frame pacing separately; do not infer that renderer CPU sums equal wall-clock critical path.

## Required validation

Build Linux and Windows; capture identical Earth Patrol, orbit, inventory and Tower scenes against last correct ac988d31; capture G-buffer, lighting, LUT, depth and final output; verify texture mip address/epoch transitions, same-address changes, dirty low chain while hi is resident, descriptor exhaustion and skipped draws. Compare cold/warm frame time distributions, lock waits, bytes uploaded, GPU timestamps, queue waits, and actual simulation time. No visual parity or FPS improvement claimed until run on hardware.

## Follow-up: verified function bodies and mip range safety

The original 007d4480 body calls the descriptor initializer with half dimensions when high memory is absent, rounds dimensions to multiples of four when tag+35 is set, and selects the low or high address explicitly. The Vulkan upload now validates these descriptor states, treats mip callback addresses as authoritative, checks the two independent memory spans on DMA/CPU invalidation, allows failed uploads to retry, and crops padded block rows into the authored destination footprint. Split resources use demand upload until the background loader can prepare both allocation ranges safely; this is a correctness-first tradeoff, not a claim of higher streaming FPS.

The full function index was checked before recording 17 additional evidence-backed names. Existing ledger entries and unconfirmed function starts were not overwritten. In particular, 007df200 writes a dummy T# pointer, rather than binding a PS texture. See records/names.tsv for individual evidence. The status table was regenerated programmatically using the existing status algorithm and full function index.


## Test 33 follow-up (2026-11-26)

The supplied client log identifies renderer build v12, Intel Arc, 1920x1080 internal rendering, 2880x1575 resized swapchain, immediate presentation, 120 FPS target. It does not identify an exact source commit; do not assume this executable includes the preceding B17 fixes.

- Warm intro 120.1 FPS; orbit 98.7 FPS; in-world intervals 14.2, 16.6 and 14.5 FPS.
- World: 6,983–9,338 recorded draws/frame, 96–97% parallel; 29.9–36.3 ms recording, 25.5–38.0 ms reported presentation/wait per frame. Worst recorded in-world hitch 502.0 ms (465.9 ms presentation, 34.7 ms record). Many hitches have zero newly compiled pipelines.
- At the 4,455-frame checkpoint: 742 cumulative texture evictions, 52 cumulative null vertex-buffer skips, 2,911 cumulative target readbacks, 49.1 MiB reported ring peak, 1,230 MiB image memory after a previous 1,727 MiB peak. Feedback snapshots 431 copied, zero reused in the latest interval. These counters do not prove the cause of the visible flicker; cumulative byte averages are not steady-state GPU bandwidth.
- Simulation: two 10 s world intervals advanced only 5.2 and 6.8 s of game time. The original timing routine returns a single capped primary tick in the trace even on long hitches. Do not patch the nominal rate or force a larger delta without recovering both tick consumers, the accumulator and scheduler dependencies.
- Backend log has warnings and unhandled messages; no evidence that these account for the recorded renderer stalls.

Source changes on main:
- 05ea9a42: distinguish guest primitives that share Vulkan topology in the final pipeline key; depth and stencil copy regions separately; serialize loader and draw pipeline creation; throttle cache persistence by wall time; separate acquire, queue present, frame-end and cache-save timings.
- 4a37e97f: synchronized texture-use masks with epoch-validated thread-local fast path; unknown masks remain conservative.
- 7e349fb2: bind run targets before resolving texture views in submission-time recording (previously only the engine-list path did this); separate secondary-worker view lock from the parent renderer mutex; do not skip background uploads solely on identity if contents are stale.
- 4d40e66d: bounded split-mip binding trace includes epoch, source spans and resident range, no frame-1200 cutoff.

Validation status: source review and guarded edits only. No Windows build, Vulkan validation-layer run or real-GPU comparison was available in this session. Neither FPS uplift nor elimination of flickering is certified.

Next test: same scene, camera path, GPU and settings; first compare current main on the normal submission-time path. Record the new presentation avg/max line, split-mip bind transitions, skipped draws, simulation/wall-time ratio and cold/warm pipeline creation. For an isolated presentation A/B keep the internal resolution constant while comparing window extent and present mode; do not ship a visual-quality reduction as an optimization. Next source investigations: exact target alias ownership rather than same-base fallback, sampled attachment content generation, pipeline cache identity/lifetime, per-pass GPU timestamps, GPU query/readback consumer contracts, and the original frame scheduler. Keep original shaders and game timing until those contracts are verified.
