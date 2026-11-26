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
