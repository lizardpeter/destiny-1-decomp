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


## Native PC renderer ownership and submission pass (2026-11-26)

The shipped x86-64 PS4 executable is still being run with native host hooks; this is not yet a fully reconstructed PC executable. The production direction is engine resource identity -> native Vulkan object ownership -> ordered resource updates and draw recording. A console address is not a durable asset ID, and a recorded draw should not consult mutable render-target maps on a worker.

Test 33 baseline (uploaded d1-native.log, not a post-change run): in-world graphics intervals 14.2, 16.6 and 14.5 FPS; record 36.3/33.2/29.9 ms and present+wait 32.9/25.5/38.0 ms. The 4355-frame stats interval averages 9338 draws per frame, with 97% recorded in parallel. The ring peaks at 49.1 MiB of 256 MiB. Copied geometry averages round to 0.0 MiB/frame over the run, so allocating a larger ring or replacing every allocation with a mapped buffer is not justified as the primary fix. The CPU per-frame phase timings are sums across threads, not elapsed wall time. Separate timing trace intervals show 5.2 and 6.8 game seconds per 10 wall seconds, another unresolved correctness issue.

Changes:
- a873e29f: a split mip image has no mutable global view offset. Each captured low/high descriptor selects its own mip origin and must match a loaded source. Registered texture identity is authoritative, consistent with the existing feedback-scan assumption that registered asset textures are not attachments. Verify that assumption across all scenes; unknown/unregistered descriptors still use attachment alias lookup. Asset and attachment view generations are separate; cache hits account for texture use under the renderer lock. Pass-local feedback views resolve by target identity (including stencil/read aliases) before worker recording. Workers no longer create snapshot views.
- a2b4e02e: coalesce successful DMA destination ranges before scanning native textures for invalidation, skip unused texture slots in preparation, and do not hash full unchanged assets in the loader. Target creation changes attachment-view generation, independently of ordinary assets.
- 9713d155 / 8943b8a2: reserve a recording slot before acquiring a swapchain image, track the consuming submission's completion for each acquire semaphore, reset acquisition slots on swapchain rebuild, and fail explicitly on queue-submit failure instead of waiting on a fence that cannot signal. Opt-in D1_NG_GPU_TIMING=1 adds two timestamps per submission and collects results only after its fence completes; reports sums/mean/max submission spans, not GPU utilization (spans may overlap).
- 0c8c5ac4: clear obsolete split-source metadata on contiguous uploads and invalidate cached views when a verified non-streamed relocation changes identity.
- 9aceba9cd: on the submission-time recording path, prepare native draw segments in command order relative to CPU-visible DMA/WRITE_DATA updates. Deduplication is scoped to each preparation segment, not the entire submission. Earlier draws' uploads are recorded before subsequent writes are prepared. This is not a proof of GPU-produced DMA source semantics; the ahead-of-submit engine-list path still needs ordered upload and mixed-list lifetime reconstruction.

Native-port acceptance criteria: explicit engine allocation lifetimes (including release, rename and fence retirement); authoritative typed render targets and per-subresource hazards; native resource update ordering for all command-list modes; original frame scheduler with wall-clock-correct simulation under both fast and slow rendering; no dependency on fake vblank or fabricated shader outputs; validated original visual output before enhanced graphics. Next priorities are replacing blanket ALL_COMMANDS barriers with evidence-based per-resource transitions, measuring GPU pass costs, and taking over transient allocations using GPU-safe lifetimes rather than simply returning arbitrary mapped pointers. Do not silently disable occlusion or query consumers.

Validation: repository source and call sites inspected, commits verified on main. The execution container cannot resolve github.com and has no local full checkout or Vulkan headers. No full native compilation, Vulkan validation or live-GPU performance comparison was performed in this pass.


## Native PC submission, synchronization and shader ownership (continued 2026-11-26)

This pass continues the native Vulkan design rather than changing visual quality, simulating a faster console, or disabling queries.

- d7d47989 / 7788c1db: mixed recorded/deferred lists preserve actual order, completion labels flush at their boundary, CPU-visible writes are not pre-executed or replayed by speculative list recording, original constant captures are preserved when early recording fails, and explicit unbinds no longer inherit stale slots. The label interrupt flag is still a list-level contract; further evidence is needed for every interrupt producer.
- 5030e257: submission-order recording is the default; D1_NG_EARLY_RECORD=1 retains the experimental ahead-of-submit path for controlled A/B. The uploaded test-33 log was already on the submission-time path, so changing this default alone is not an explanation for that run's low FPS. Parallel secondary recording remains enabled.
- 07da60a2 / e40d015c / 2e928b30 / f8fc255a / 1badbf28: exact-boundary per-thread mapping extent cache avoids the global mapping mutex on stable hits. Adjacent or overlapping mappings are not merged. Standalone C++20 regression compiled locally with g++ -O2 -Wall -Wextra -Werror -pthread and passed 1,088,900 comparisons with the original ordered-map predecessor lookup, including remapping, unmapping, adjacent and overlapping ranges. This tests metadata correctness, not concurrent unmap lifetime.
- e718c7ce: native image uploads carry image-specific transfer-write dependencies, repeated writes to one image are ordered, and preparation with no uploads emits no redundant barrier. Broad submission and render-pass barriers remain until exact hazards are recovered.
- 84340092 / 1eec30cd / 77f7ad11 / 8115306d / c29f14c0: native shader module keys include bytecode, fetch code, resource usage, semantic linkage and stage; pointer/epoch identity is used for fast lookup. The old pipeline-spec file format is deliberately superseded by .specs.v2. The technique prefetch path now uses a real resource epoch instead of the value 1 that the loader always rejected. Type 8 (0x7D4B90) is not a pixel shader; its header-bearing texture format is excluded from speculative shader work until separately recovered. Released resources no longer start new prefetch jobs. Pending identical jobs are deduplicated, persisted warmup candidates indexed by shader, foreground misses build only the requested pipeline, and the loader releases the pipeline lock between warmup builds. Unregistered shader objects still have pointer-based lookup limitations; complete engine shader lifetime ownership is not proven.
- 524af8a8 / e0f1495a / be512f79: register the permanent native command allocation arena as a valid host mapping, retain chunks still referenced by inherited constant bindings, remove manual unlock/relock of the global allocation mutex, refresh constant stage tables after resource writes and zero alignment padding. The grace period for other captured pointers still needs explicit ownership. No claim of direct Vulkan mapped dynamic geometry or constants yet.

The test-33 log was independently reread from the uploaded file during this pass: the reported 14.2/16.6/14.5 FPS in-world intervals, record and present+wait values, 9,338 draws per frame interval and 5.2/6.8 game seconds per 10 wall seconds are present. These remain pre-change observations.

Validation: the standalone allocation extent test compiled and passed. Source integration and commits inspected on main. A direct git ls-remote failed because github.com cannot resolve in the execution container, and Vulkan headers were not installed. No full Windows/Linux executable build or live GPU validation was performed. The new cache and synchronization paths require actual Vulkan validation and comparable cold/warm gameplay capture; clear/rebuild only the obsolete .specs file if desired (new .specs.v2 is built automatically). Do not infer measured FPS gains from these source changes.

Next reconstruction priorities: recover original two-channel tick accumulator and frame-stage dependencies; replace console address aliasing with typed subresource ownership; eliminate full render-pass barriers using measured dependencies; replace the remaining transient allocator with a fence-retired native allocation model; translate remaining shader operations rather than suppressing them; recover query consumers so only genuinely CPU-read outputs cross back to CPU memory. This is still native execution of the retail x86-64 code via a transitional platform layer, not yet an independently compiled reconstruction of the complete game.


## Native submission ownership and original frame timing (continued 2026-11-26)

### Scheduler evidence, not a new FPS-unlock heuristic

Re-read the original Ghidra pseudocode for 0x591710, 0x591D40, 0x593970, 0x593F00, 0x594460, 0x74BB00 and 0x929EF0 (analysis/ps4-0133/pseudocode/00590000.c, 00740000.c and 00920000.c). This corrects an important earlier assumption:

- 0x591710 produces two aggregate-duration/count pairs. In its normal path, the timescale-adjusted elapsed interval is multiplied by 0x5938B0 (rate from time-control config +08), accumulated with the fractional carry at *0x26BC888 +08, truncated to a count, and capped according to the game-mode and timescale branches. When the cap is exceeded, it returns count * 0x593890 and retains the previous fractional carry. The excess duration is not automatically carried forward.
- When 0x18DCAA4 is nonzero, timescale is approximately 1, the pause gates permit it and elapsed is positive, the primary pair is overwritten with the nominal duration from *0x26BC880 +08 and a count of 1. The secondary pair remains independently computed unless 0x26BC890 forces its own single step.
- 0x591D40's first primary step is aggregate_us - (count - 1) * extra_step_us, not aggregate_us * count. Subsequent scheduler calls consume extra_step_us and decrement the remaining count without re-running 0x591710. The extra step period comes from time-control config +40, copied to the 0x48-byte Fiber Frame Time Info +38 by 0x593970.
- 0x591D40 publishes a double-buffered 0x38-byte state at 0x26C6FD8, selected by 0x26C7048. The first-step flag at +29 controls the first-step work in 0x74BB00; that function forwards elapsed/current step/count to 0x929EF0. The latter has additional mode-dependent handling and a 0.2-second cap on one input. Do not assume one return value controls all simulation.
- 0x593970 constructs the three stage graphs, connecting the stage handles at 0x26C6FD0, 0x26EC3A0 and 0x26EC43C using 0x23D160. 0x594460 checks readiness of downstream stages before kicking another graph. Preserving these dependencies remains mandatory.

The uploaded test-33 log has e.g. elapsed 0.26549 s -> primary 0.03333 / 1, secondary 0.06667 / 2; elapsed 0.42273 s -> the same output pair. The forced single-primary branch and original cap therefore explain lost simulation duration under severe stalls. The earlier 5.2/6.8 game-seconds per 10 wall-seconds observations still describe that trace (primary count was 1). They are not evidence that arbitrary large variable deltas would be safe.

e7ce43b7 corrects aggregate timing accounting, bounds the integer-rate conversion, and introduces D1_NATIVE_FIXED_TICK=1 as a research-only path preserving the original accumulator period and temporarily suppressing the forced-primary branch while invoking the original routine. The limiter period is restored. This is NOT certified as a fix for slow motion: the engine caps, timescale, stage dependencies, and actual game consumers still need live validation; it also temporarily changes global fields, so concurrent readers must be checked. Existing variable-tick production behavior remains the default.

6bbee8a6 / ef7bf1f2 / 33d5e1e1 / d33eba16: recovered frame record layouts and integer split reference; D1_NATIVE_FRAME_TRACE=1 observes the original 0x591D40 publication including follow-up steps, rather than treating calculation calls as rendered frames. It reports planned vs published time separately. The standalone reference test checks layouts and 100,005 arithmetic cases; it does not replace or machine-code-validate the original scheduler.

### Native GPU queue

402fa24c / 1c4a1794 / 6e2c0f7d / 7928c2a5 / dce6a3b3 / 73e49b43: the previous queue stored pointers into the engine's reusable command buffers and looked up lists later on the GPU thread. Now the submit API decodes the transitional list tokens synchronously and detaches the native lists from the registry. The asynchronous consumer receives native list ownership, not reusable console-shaped packet memory. The fallback/null path owns a copy of its packet words. Combined submit-and-flip is a single queue operation, and orphan retirement no longer calls renderer routines under the registry lock. The retail token ABI still exists at the capture boundary; this is not removal of all PS4 interfaces.

7d48b783: an attachment snapshot from a previous rendering instance can no longer be reused just because the attachment set matches. Reuse across rendering instances must pass the existing per-command-buffer freshness check. Failed Vulkan sampled-view creation or descriptor allocation no longer installs an invalid image view; the logged placeholder is an error path, not a visual-parity solution.

Validation: locally compiled the exact committed native_list_tokens and frame_time_contract headers and tests (git blob hashes compared with GitHub), C++20 -O2 -Wall -Wextra -Werror and Clang AddressSanitizer/UndefinedBehaviorSanitizer. Both configurations passed 40,000 token cases and 100,005 frame arithmetic cases. No full native executable build or real GPU test: direct GitHub clone failed DNS in this environment and Vulkan headers were not present. No claimed FPS uplift, solved stutter, or validated fixed-tick gameplay. The next evidence-driven work is stage callback recovery, resource allocation pinning and precise per-pass hazards. 


## Native GPU writeback ownership and true occlusion reduction (continued 2026-11-26)

- 223e1017 / e9b84642: registered host mappings now have monotonically assigned identities. A delayed target readback or occlusion counter captures that identity and completes under the mapping mutex only if the range is still valid, writable according to the recorded protection, and belongs to the same mapping. An expired destination is counted and logged rather than writing into a replacement mapping. This is mapping-level ownership, not suballocation pinning; reuse inside a permanent arena, arbitrary CPU writes during an in-flight result, partial mappings and protection transitions still require engine allocation contracts.
- 1bdff77c / c26c6c71: end-of-pipe labels capture destination ownership at the producer-side submit boundary, not at delayed GPU completion. Native lists are detached before taking the memory-map lock. Expired labels are diagnosed; the actual completion interrupt still fires.
- 819d5e24 / 1d910ff4 / 3680eecd / 5abe0282: half-open merged write intervals are shared by DMA invalidation and CPU-write notifications. The earlier watched-memory predecessor walk could miss an older, wider overlapping range. Visit every watched range once against the merged intervals, retain zero-length unlock notifications as point writes, and bump the texture-view epoch once per invalidation batch.
- f742f43d / 9fe97784 / a379cfd9 / 7ca3e1db / 13fa16e5: multi-pass occlusion used to manufacture a nonzero conditional-rendering value. The new native compute shader ORs both 32-bit words of every actual 64-bit Vulkan sample count and emits a normalized 0/1 result without requiring shaderInt64. Empty ranges produce zero; nonempty ranges copy real query results on the GPU. End-of-submission processing copies the query pool once and writes disjoint predicate slots with batched dependencies when the slot table cannot wrap. Inline consumers remain ordered. Predicate slot identity includes the host mapping; failed initialization logs and leaves the predicate unavailable, rather than supplying a fabricated result. The conservative unpredicated fallback is still present for unavailable queries. Cross-submission begin/end pairing, query-pool exhaustion and original query consumer coverage remain unresolved.
- 1bf868ad: fence/timeline errors and unavailable completed occlusion results are now explicit fatal diagnostics, not synthetic zero counts or silent completion. This deliberately prioritizes correctness and debuggability; recovery from device loss is not implemented.

Validation performed in the local container (source blob hashes checked against GitHub):
- Native write-range test: 1,000,004 reference comparisons; passed g++ C++20 -O2 -Wall -Wextra -Werror and Clang ASan/UBSan builds.
- c4187d5e / bbe7b76d: compiled the actual committed query_reduce.h and spirv.cpp/spirv.h. The generated module is 301 words. A narrow test-only evaluator executes its emitted integer/control-flow instructions against a reference predicate, checking untouched destination slots, empty ranges, high-word-only counts, and nonzero counts outside the requested range. All 10,005 cases passed optimized and ASan/UBSan builds. This is a CPU-side emitted-shader regression, not spirv-val or GPU-driver validation.
- Both tests registered with CTest. Full renderer build not run: git ls-remote again failed DNS resolution in the container; Vulkan headers and SPIR-V validation tools are unavailable. Existing test-33 logs show hundreds of predicated draws and up to 1,920 occlusion dumps per stats interval, but no post-change performance or visual result is available. The added GPU work must be profiled with real hardware.

Next: reconstruct the original query consumers and cross-submission spans, pin engine suballocations rather than only virtual mappings, recover authoritative render-target subresource identities, and replace the remaining full GPU dependencies only where hazards are proven. No change in simulation timing policy in this pass.
