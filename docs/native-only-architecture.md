# Native-only PC architecture and migration

This is the production architecture contract. It supersedes historical descriptions of opt-in migration cohorts and regression switches in older notes. See root AGENTS.md. The faithful reconstruction under src/ and generated original analysis remain separate; this is a PC engineering plan, not a count of decompiled functions.

## Definition of done

The game executes x86-64 gameplay code directly and uses Vulkan, native host storage, files, threading, synchronization and timing. A proper PC port does not incur inherent console adaptation costs in its steady-state hot paths. No legacy fallback for completed production subsystems. Original code is useful as a differential oracle in isolated research, not a runtime alternative. Unsupported cases must be reconstructed before their dependency is removed; the migration is not complete until none remain.

## Current evidence and remaining work

| Area | Current implementation | Remaining production debt | Acceptance evidence |
| --- | --- | --- | --- |
| CPU/game logic | Native x86-64 original executable | Faithful reconstruction and platform ABI cleanup | Correct gameplay, no instruction emulator |
| Vulkan | Native queues, pipelines, images, buffers and presentation | Broad barriers, duplicated state preparation, compatibility interfaces | GPU capture and validation, no console command stream |
| Resource lifetime | Epoch registry, texture source leases, GPU fences | Original CPU allocation identities; pending geometry/shader ownership; cube/array/volume coverage | Pool reuse/streaming stress, no stale views or lost mips |
| Asset ingestion | Background shader/texture/buffer jobs, native asset backing subset | Tiled source and original allocator still involved, foreground misses | Persistent native formats, no render-thread detile or static asset upload in warm scene |
| Materials and geometry | Bindless views, device-local geometry, native preparation | Per-draw original descriptor capture, lookup and repeated preparation | Native immutable handles and direct commands; dynamic content properly owned |
| Submission | Native lists, close/drain/ring, CBM pool | Original device fields, inherited state, engine scheduler/labels | No fabricated packets or original command storage dependency; ordered completion |
| Shader pipelines | Translated SPIR-V, persistent cache and background warmup | First-use compilation, original shader metadata | Cold preparation and warm-cache runs, no gameplay compilation for known assets |
| Platform | Native FIOS cohort and libc leaves; native graphics backing | Original libc PRX, incomplete system imports, allocator/threading/synchronization | No legacy provider; independently tested ABI and failure behavior |

## Implementation sequence

1. Establish one owner for each native resource, from asset load through command capture, submission and fence retirement. Host staging is temporary and checked; a pool address is not a resource identity.
2. Move static geometry, textures and shader ingestion to loading/streaming. Capture a native resource handle, not a console descriptor; maintain explicitly versioned dynamic resources, render attachments and constant data.
3. Replace remaining console-shaped resource and command management with native structures. Keep only the minimum ABI bridge required by still-original gameplay code; remove bridges as caller contracts are reconstructed.
4. Prepare immutable materials, pipeline state and geometry ahead of draw; generate native draw commands and use appropriate batching without changing order, predicates, query semantics or blending. Use parallel preparation/recording only where it actually reduces wall time.
5. Complete platform service migration, including memory ownership, native file APIs, threads, synchronization and libc. Do not reintroduce original providers when native implementations fail.

## Evidence and regression gate

- Validate original contracts from build 01.33 disassembly and pseudocode; synthetic tests are not machine-code differential tests.
- Windows and Linux compilation and unit/contract tests; verify required replacements install before gameplay.
- Run identical scenes/settings on the same hardware and build SHA. Record FPS, p50/p95/p99 frame time, CPU capture/preparation/recording wall time (not summed worker CPU), GPU time, allocation/upload counts and waiting.
- Verify frames visually through prolonged play and repeated texture streaming, map changes and memory reuse. No flickering, disappearing textures or lost high mips.
- No claim of massive improvement without measured live evidence. Startup success is necessary, not sufficient.
- No fallback to legacy paths for completed cohorts. Keep unresolved original leaves explicitly named as blockers, and close them with source-backed patches. No fake success or arbitrary failure solely to remove a branch.

Historical baseline from prior live testing: roughly 34 FPS and 25 ms CPU recording with little GPU wait (reconfirm against the exact baseline logs/build before comparison). The latest startup failure at 0xE01E0 was a trampoline installation failure, not Vulkan initialization. It was replaced by a native callback dispatcher; subsequent build/game validation is still pending.

## Mandatory production consolidation

The previously default-on graphics migration switches for asset backing/suballocation/container release, pool destruction, bind/rebind, command close/drain/ring and CBM backing/pool are removed from the device implementation. Complete source-defined bodies install directly rather than allocating unused original trampolines. Ordinary and instanced indexed draws share one native capture path; six draw entry points now install direct replacements. Native submission uses the native FIFO and record identities, not fabricated nonempty command-buffer extents. The complete 0x7DB810 segment ledger now executes native code for all callers, including intermediate segments. It retains actual engine record fields for unreconstructed readers, not console command generation. Capacity exhaustion terminates with a diagnostic before writing outside the 64-entry record.

Native FIOS is mandatory: the old PRX is not loaded, and every one of the 25 observed import NIDs must resolve to the native provider. The 37 reconstructed libc memory/string/math leaves are likewise mandatory; original libc still supplies the unreconstructed ABI. Static geometry loader uses device-local Vulkan preparation and exact epoch keys, not the selectable mapped-buffer loader. Dynamic and unified-memory native buffers remain valid PC mechanisms.

Validation: the exact committed native_close_contract.h and native_close_contract_test.cpp blobs (2b06c70f63f98d3df4161137e75332b90038496d and 5bdef88a25cf20be9e1adfc2e5dea1b804ffa385) passed optimized C++20 -Wall -Wextra -Werror and ASan/UBSan runs: 65,536 close plans, 200,000 wrap/progress cases and 100,000 segment ledger cases. This is a standalone synthetic contract test, not a retail differential or integrated game test. Integrated Windows/Linux build and live results remain unverified.

Explicit remaining blockers: original non-GPU region and unhandled GPU suballocation/release cases (B4780/B4630/B2BD0/B2F40), full tile layout/footprint leaves (F85CB0 and unsupported F89BD0 seed paths), inherited command-list begin (7DD6A0) and reset/context helpers, remaining device setters and compute dispatch, pending static geometry/shader lifetime ownership and texture coverage, first-use native upload/translation, original libc ABI and system imports. Do not classify these as completed or remove their branches without reconstructing the missing behavior. The objective remains removing all of them, not preserving legacy alternatives.

## Native buffer binding and geometry ownership

The complete 01.33 buffer descriptor leaves F7CF90 (vertex), F7D180 (constant) and F7D010 (memory type) are now native code. The vertex/index binding entries 7DEC20/7DEE30 and constant allocation entry 7DF630 no longer call retail bodies. They publish native binding/arena state and retain only the engine ABI fields still read by original callers. Inline draws no longer capture static geometry that they will replace; native attachment clears no longer capture unused material/geometry resources. Original clear-side state restoration, inherited begin and other device setters remain migration debt.

Static geometry is owned by exact resource epoch, base and size. The first native list or background loader that needs unavailable geometry reserves a bounded immutable source (256 MiB total including pending reservations). Unrelated source copies execute outside the table lock; same-key consumers share the result. Rejected reads release the reservation and never publish partial data. Native command lists pin exact ranges; GPU geometry eviction cannot reclaim pinned epochs. CPU index inspection can separately request a retained source even when its Vulkan buffer is already resident. The ordered preparation pass selects device-local allocations before command recording. The former registered-geometry address-based upload/cache path and recording-thread static resource lookups are removed. Native ring uploads under memory pressure and CPU index inspection use retained exact bytes or checked current mappings with matching resource epochs, never an unchecked old pool address. Genuinely dynamic unregistered buffers remain native upload-ring resources.

The native background asset loader is mandatory; early speculative record mode and the global no-cache switch no longer select alternative production paths. Existing original layout calculation and allocator metadata remain explicit migration debt. Full asset-load-time ownership, shader byte ownership, original pool race coverage and elimination of first-use CPU snapshots/ring uploads are not yet complete. New runtime counters expose source memory, requests, rejected captures, retained/current CPU slice reads and rejected epoch accesses. Source copies can still block a first-use draw for the same resource, and live wall-time effects are not yet measured.

Validation: exact committed blobs for native_buffer_binding.h (19ba2a1063afe5091bc5e7da047133db9cb0a3bc), native_buffer_binding_test.cpp (b919501f879faa7c7c2d63e12ec3c118b1059f47), native_geometry_sources.h (ddca9605e62320a85b4ec1a84920dce97e0f6a4e), native_geometry_sources_test.cpp (a0f036c23b694a2584b1d5a5681c836278fb4a99), and native_resource_identity.h (27dff8153ced320693f12f8fcbc08847bfb31ba5) passed local Clang C++20 optimized -Wall -Wextra -Werror and ASan/UBSan runs. Buffer suite: 65,536 vertex layouts, 8,388,608 memory-type combinations, 100,000 constant/binding cases. Geometry suite: 8,000 concurrent acquisitions, independent pending claims, pressure, epoch reuse, pinned and externally held source lifetimes, exception/failure recovery. New geometry range lease cases are registered in the existing texture identity suite but have not been executed locally. These are standalone contract tests, not integrated game or original-machine-code differential tests. Windows/Linux CI and live game rendering remain unverified.
