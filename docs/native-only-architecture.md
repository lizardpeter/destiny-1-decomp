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
