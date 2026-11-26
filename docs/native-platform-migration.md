# Native PC platform migration — 2026-11-26

## Definition of done

A standalone source-built PC game must neither load the retail PS4 executable nor the two retail PRX modules, nor require their import/linker, TLS, event, virtual-memory, graphics, controller, audio or network ABI. Runtime assets can remain in their original on-disk formats; asset decoding is not execution of a console platform. Until that point the current product is a transitional x86-64 native execution port, not a source-complete PC game.

## Implemented in this pass

- Removed the external-renderer dispatch branches, D1_RENDERER_SHADPS4 conditionals in active graphics code, render_bridge.h, and render_null.cpp. SDL window/input/audio and explicit headless frontend now have an independent PC frontend interface. Host memory metadata and frame telemetry have their own headers. The production executable requires a successful Vulkan initialization and installation of all required device hooks; it no longer silently falls back to a null GPU.
- Deleted the legacy PM4 memory-side-effect interpreter and the frame-skipping path that invented occlusion counts. Actual GPU execution remains required. The headless frontend means no window, not a simulated GPU.
- Original submit evidence: Ghidra pseudocode 007d0000.c, 0x7DCEC0, 0x7DE950, 0x7DB810, and 0x7DD060. The original close path publishes a ready record; 0x7DB810 uses begin/end pointer differences and stores the segment addresses at record+0x30 and sizes at record+0x430. All three identified sceGnmSubmit[AndFlip]CommandBuffers calls in 0x7DD060 pass record+0x30 directly. The new NativeRecordRegistry associates the native list with that address, publishes once and transfers once. No artificial packet or list ID is written or parsed. A one-word pointer advance preserves the original nonempty record bookkeeping; the engine's command-buffer allocation and ready-ring contract remains transitional. Unknown submission records, invalid counts and premature record reuse are explicit fatal diagnostics. Abandoned unpublished lists are reclaimed separately; a ready published list is not discarded merely because 300 frames elapsed.
- Speculative native recording at retail list close has been removed from the device adapter. The original close appends final EOP labels after our association is published but before the engine's ready flag is set. The asynchronous native consumer takes the complete list in submit order. Native secondary command recording remains active. The unused low-level renderer ahead-recording implementation still exists and can be removed after a full integration build.
- 19 memory/string libc imports now resolve through explicit library-scoped host wrappers instead of original libc exports: memcpy, memmove, memset, memcmp, memchr, strlen, strcmp, strncmp, strstr, strchr, strrchr, strcpy, strncpy, strcat, strncat, strspn, strcspn, strpbrk, strlcpy. Host functions preserve standard API semantics; no mixed allocator, FILE, locale, RTTI or exception ownership. D1_PC_LIBC_LEAVES=0 permits controlled comparison only. This is not bitwise or timing equivalence with the original implementation.

## Remaining dependency inventory

Analysis/ps4-0133/imports.tsv contains 3,262 libc relocation rows for 143 distinct names (127 PLT rows), and 22 FIOS PLT imports for 22 distinct names. These are executable import counts, not total program functions or proof that every import is used in every game mode.

| Area | Remaining obligation |
|---|---|
| Graphics | Reconstruct original record ring/CBM, device descriptors, shader/resource asset conversion, flip/event consumer contracts, native compute and synchronization. Remove retail Gnm import adapters when callers have been migrated. |
| FIOS | Recover operation/handle lifecycle, attributes, callbacks, error values, directory/stat layouts and file I/O concurrency; replace 22 imported functions and any libc-to-FIOS dependencies. |
| libc | Migrate remaining memory-space allocation, C/stdio, math/locale, time, setjmp, C++ runtime and RTTI ABI. Do not redirect ownership-sensitive APIs separately. |
| Kernel/threading | Rebuild engine job graph on native threads/synchronization, native virtual-memory allocator, native time and I/O. Retire SCE TLS/Tcb and executable patching only when last retail caller is gone. |
| Input/audio/services | Native host-facing services are independent of the old renderer bridge, but retail ABI adapters and legacy action/glyph data remain. Recover callers and migrate to PC interfaces. |
| Game code | Replace the original eboot entry, fixed-address globals and original function calls with compiled native implementation. The analysis inventory identifies 54,533 eboot functions; this is not a verified count of reconstructed functions. |
| Networking | Native replacement backend remains in use; migrate remaining original networking/session/service callers without reducing gameplay coverage. |

FIOS imports recovered from the inventory:
sceFiosOpGetActualCount, sceFiosOpDelete, sceFiosInitialize, sceFiosTerminate, sceFiosFHOpenSync, sceFiosFHCloseSync, sceFiosDeleteSync, sceFiosFHPread, sceFiosFHPwrite, sceFiosDirectoryDeleteSync, sceFiosFHGetSize, sceFiosFHWriteSync, sceFiosFileDeleteSync, sceFiosDirectoryCreate, sceFiosStatSync, sceFiosDHOpenSync, sceFiosDHCloseSync, sceFiosDHReadSync, sceFiosFHSyncSync, sceFiosFHSeek, sceFiosFHTell, sceFiosFHReadSync.

## Validation and known risk

The exact committed pc_libc_leaf.h / pc_libc_leaf_test.cpp and native_record_registry.h / native_record_registry_test.cpp were compiled locally, with blob hashes checked against the repository. Both passed g++ C++20 -O2 -Wall -Wextra -Werror and Clang AddressSanitizer/UndefinedBehaviorSanitizer. The leaf suite ran 100,000 randomized iterations plus boundary checks; the registry ran 100,000 ownership cycles on four concurrent threads plus invalid/duplicate/reused-record checks. CTest registrations updated. The tests exercise standalone implementation, not original executable linkage or GPU playthrough.

No full PC executable build or physical GPU test was possible: direct GitHub checkout fails DNS; this environment lacks Vulkan and SDL development headers and R2 host DNS resolution also fails. The record-address association is grounded in original pseudocode but not yet live-validated; unknown callers intentionally fail rather than falling back. The original eboot and both PRX modules are still needed. A source-complete independent build, full graphics parity, and performance gains remain unverified.


## Native file services and libc migration — continued 2026-11-26

### Retail FIOS evidence

Re-read `analysis/ps4-0133/pseudocode/00080000.c`. The engine-side wrappers are:
- 0x803A0: initialization copies 0x98 bytes of parameters, installs four original memory-pool addresses/sizes, vprintf and memcpy callbacks. 0x804C0 terminates.
- 0x804E0: engine open flags translate into low FIOS bits through a recovered exact expression; all 16 low-bit combinations are preserved in `fios_contract.h` and tested over 65,536 input values. This does not establish the semantic names of every FIOS flag.
- 0x805A0: synchronous open passes a 24-byte zero-initialized attribute record with 16-bit flags at +0, a 32-bit file handle output, and null operation attributes. 0x80610 closes, 0x80640 deletes.
- 0x80670 and 0x80740: positional async read/write pass a 48-byte stack attribute record with callback at +8 and low 16-bit request token in the +0x10 user context. Zero returned operation ID maps to 0x80820001.
- 0x802E0: terminal callback event 1 stores the supplied result at request+0x94, reads actual byte count through sceFiosOpGetActualCount into request+0x18, deletes the operation, atomically clears request+0x68, then calls 0x82A70 under the original engine lock. This constrains callback ordering and lifetime; it does not prove callbacks may run on arbitrary host threads.
- 0x807F0 directory delete; 0x809F0 file-size failure -1 maps to 0x8082000B; 0x80570 recognizes 0x80820010. 0x8A1A0 tests open/write/close/delete on temporary storage.

### Implemented native service (not yet the production FIOS import provider)

- `pc_file.h`: owned host file descriptor, explicit access/create/truncate/exclusive options, 64-bit positional reads/writes, partial-count errors, independent sequential cursor, size/seek/tell/sync. Linux uses pread/pwrite; Windows uses wide-path CRT open and per-file serialized seek/transfer. Windows overlapped/IOCP optimization is not implemented or measured.
- `pc_file_queue.h`: native bounded-count worker queue, monotonic nonzero file/operation IDs, shared ownership of files for accepted jobs, owned write snapshots, caller-owned read storage, completed result publication and callbacks outside the lock, poll/wait/delete and close while jobs run. Independent positional jobs may execute out of order; original priority, cancellation and same-file ordering remain to recover. Queue limit counts operations, not aggregate snapshot bytes; callbacks must not throw or destroy/wait on the same worker pool.
- `pc_paths.h`: explicit immutable asset and writable data roots, containment checks, native stat and directory operations. Rejects parent components, absolute child paths and existing symlink escapes. Preserves the leaf path after canonical containment validation for unlink/rename. Not a hardened concurrent sandbox (symlink races/hardlinks remain). `pc_file_service.h` composes file operations with those roots; all its opens go through containment.
- `pc_mount_adapter.h`: exact retail mount boundary mapping. Existing kernel open, stat, truncate, mkdir, rmdir, unlink, rename and chmod now use this native resolver. `main.cpp` initializes both roots before entering game code. Both roots must be separate existing directories; unknown relative paths now map beneath data/unmapped rather than concatenating strings, invalid foreign path syntax and parent components fail, and chmod returns its actual error. These are deliberate path-policy changes. The original kernel descriptor ABI remains in use; the new native async queue is not yet called by retail FIOS.
- `pc_libc_math.h`: 18 additional native scalar math/time-difference wrappers with explicit 64-bit integer widths (including lrintf), wired to library-scoped import lookup via **D1_PC_LIBC_MATH=1**. The existing 19 memory/string leaves stay enabled by default. D1_PC_LIBC_LEAVES=0 disables both groups. Math remains opt-in: original libm accuracy, exceptional results, errno, floating-point environment and ABI crossing need game verification. Do not substitute host wchar_t: the 0x7C2750 caller copies wcslen * 2 bytes, so the retail wide-character width needs verification.

### Validation

Exact committed header/test blob hashes matched local sources. Six standalone suites passed g++ C++20 optimized -Wall -Wextra -Werror and Clang ASan/UBSan; math used -frounding-math.
- pc-file: 10,000 positional write/read pairs, 12,800 async operations, EOF, invalid ranges, close with outstanding work, nonreused IDs, sparse offset beyond 4 GiB on Linux.
- pc-paths: 128 files plus metadata/list/remove, immutable asset and existing symlink containment.
- pc-mount: mount boundary and syntax cases.
- pc-service: queue capacity, read completion callback obtains count then deletes its own operation, close during I/O, invalid range and immutable assets.
- fios-contract: 65,536 flag cases against an independent 16-entry truth table.
- pc-libc-math: 100,000 randomized scalar decomposition/rounding cases, all four rounding modes, signed zero, invalid domains and >32-bit integer values.

All six registered with CTest. This is Linux standalone execution, not a Windows full-game build. GitHub and R2 host DNS resolution failed locally, and Vulkan/SDL development headers and a Windows cross compiler are unavailable. No claim of actual FIOS replacement or reduced stutter yet. Original `libSceFios2.prx` remains required (0/22 production imports redirected); file/stat/directory output layouts, true FIOS error values/open flags, callback TLS, in-flight buffer pinning, libc-to-FIOS dependencies and original behavior still need evidence before one coherent group switch. Libc has 19 default plus 18 opt-in native imports out of 143 distinct original eboot imports. No change to graphics quality or simulation timing in this pass.

### Retail FIOS module analysis and native provider

The exact decrypted CUSA00219 01.33 PRX files were inspected through the project's R2 object API (FIOS: 400209 bytes; libc: 927802 bytes). The combined import closure contains 25 unique FIOS exports: 22 from the executable and 8 from libc, with 5 shared. The additional libc-only names are sceFiosIsValidHandle (0x9090), sceFiosFHOpenWithModeSync (0xA430), and sceFiosRenameSync (0x9A90). Offsets below are module virtual addresses, not file offsets.

| Evidence | Recovered contract |
|---|---|
| 0x2AD70 | File flag conversion, including 0x1000 -> kernel 0x10000 |
| 0x2AB90 | Encoded kernel error conversion |
| 0x9CE0 | Direct stat output: 0x50 bytes, size at 0, atime and mtime at 8/0x10 in ns, attribute bits at 0x20, mode at 0x48 |
| 0x18AC0 | Directory worker: 0x418-byte entry, size/attributes/name length/path length/name offset at 0/8/0xc/0xe/0x10, path at 0x18 |
| 0x3A70 / 0x3B70 | Positional read/write: attributes, handle, buffer, signed 64-bit count, signed 64-bit offset |
| 0xA5E0 / 0xA430 | OpenSync delegates to OpenWithModeSync, with default mode -1 |
| 0x5C90 | Operation actual count reads 64-bit field at +0x98 |
| 0x5690 | Sync operation returns count on success/EOF, signed error otherwise |
| 0xB8B0 | Directory open has five arguments; filter arguments not yet fully decoded |
| 0xB9D0 | Directory read accepts attributes, handle and entry buffer |
| 0xB620 / 0xC360 / 0x3F90 | Seek(handle, offset, origin), tell(handle), size(handle) |
| 0x5480 | Operation deletion; early-delete/wait interaction still requires recovery |

The two scalar functions were compared directly against extracted original x64 machine code on Linux: 65536 open flags and 65536 encoded kernel statuses, all matched. The original error mapper's raw -1/errno path was not exercised. No retail code bytes were added to the repository.

The native core now contains independent file/directory/operation handle namespaces, bounded staging memory, a separate serial callback dispatcher, task submission, close-safe pending operations, explicit draining and recovered metadata structures. The original runtime log confirms separate SceFiosCallback and SceFiosIO threads. Standalone staging, callback, and binary-contract tests passed optimized and address/undefined-behavior sanitizer builds. The core header itself is not yet end-to-end compiled or linked into the retail loader; none of the 25 imports have been redirected in production. Remaining gates include guest TLS/callback invocation, destination allocation ownership, directory filters, early operation deletion, flag 0x10000 semantics, and full Windows/game validation. Host status mapping and directory metadata behavior beyond the directly recovered code remain provisional. D1_FIOS_TRACE=1 enables bounded argument-register tracing of original FIOS function imports, including libc callers. R2 binary access is available through the object API, but direct local checkout/download and full SDK build remain unavailable in this environment.


### Experimental FIOS-free executable path

This pass adds an opt-in complete-cohort provider, `D1_NATIVE_FIOS=1`, not enabled by default. In that mode the loader omits `libSceFios2.prx`, preserves the known libc module virtual base, and links the 25 distinct observed FIOS NIDs from the executable and libc directly to native host functions. Uncovered FIOS symbols and unexpected closure size fail startup; none fall back to logging stubs or the old PRX. `D1_FIOS_TRACE=1` logs bounded argument-register traces for either provider. The original libc and eboot remain mandatory.

`fios_imports.cpp` supplies all 25 adapter entries, including open-with-mode, valid-handle and rename for libc. It validates the initialization structure's size field against original 0xFA0, copies the 0x30-byte operation attribute callback/context values before returning, and invokes the engine's callback contract from 0x802E0 with four arguments through shared `guest_call4_asm.h`. The callback dispatcher initializes a guest TCB and registers its thread once, on first use; static TLS layout is published at link time rather than repeatedly written by concurrent thread creation. The native core retains transfer results until an already-scheduled callback finishes even if an external caller deletes the operation early. The queue bounds pending callbacks as well as public operation records. Staged reads require a tracked destination mapping and reject delivery if its identity no longer matches. This is mapping-level validation, not proof of suballocation lifetime.

Validation: the exact committed adapter, ABI assembly, core and dependencies were compiled locally on Linux with g++ C++20 -O2 -Wall -Wextra -Werror and Clang ASan/UBSan. Both the full core test and import harness passed. Core checks include real file and directory I/O, metadata, seek, append, immutable assets, disjoint handle classes, 1,000 deterministic early-delete/callback interleavings and rejection of positional writes on append handles. The import harness checks all 25 NIDs against the exact original symbol names/values, uses the production SysV import wrappers and actual four-argument assembly entry, verifies short-read, write and rejected-destination callback results, validates operation deletion, and checks that an untracked read destination is rejected. Runtime functions for guest TLS, thread registration and mapping delivery are test doubles; no original executable is run by this harness. Existing staging, callback and binary-contract regressions also passed again in optimized builds. Both new tests registered with CTest (import harness currently Linux only).

Remaining integration gates: full Windows/Linux native executable build and live asset-streaming test; original filter and nonzero operation-attribute semantics; actual FIOS initialization pools, thread/TLS behavior, termination and reinitialization; exact EOF/error parity; untracked or suballocated read destinations; virtual-memory protection/unmap metadata edge cases; priority/deadline and cancellation semantics. The adapter explicitly rejects unsupported nonzero attributes, directory filters and open flag 0x10000 instead of silently pretending to implement them. Termination drains the process-lifetime provider rather than reproducing complete original pool destruction; callback-time termination is rejected. The current experimental build flag is intended for controlled local verification only; production remains on the original FIOS PRX. No FPS or game boot claim.

### Native allocator ownership — bounded mspace model

Original executable disassembly and pseudocode at 0x2450–0x2670 establish a dedicated User Malloc arena. 0x2450 allocates and maps 0x4000000 bytes of direct memory with 0x4000000 alignment, then calls sceLibcMspaceCreate("User Malloc", base, 0x4000000, 0), storing the returned handle at 0x1A21618. The direct-memory offset is at 0x1A21610. 0x2510 passes that handle to Destroy and releases the direct-memory range only on success (or no handle). The decompiler omits the Destroy argument, but the machine code at 0x2514 loads RDI explicitly.

| Eboot address | Evidence from register moves and tail jump |
|---|---|
| 0x2550 | Malloc: prepends arena handle to caller's one argument |
| 0x2570 | Free: prepends handle |
| 0x2590 | Calloc: prepends handle to two arguments |
| 0x25B0 | Realloc: prepends handle to two arguments |
| 0x25D0 | Memalign: prepends handle to two arguments |
| 0x25F0 | PosixMemalign: prepends handle to three arguments |
| 0x2610 | Reallocalign: (handle, original arg0, original arg2, original arg1) |
| 0x2630 / 0x2650 | Stats / StatsFast: prepend handle |
| 0x2670 | UsableSize: direct tail jump, no argument remapping |

The decompiler labels nine of these forwarding wrappers void, but the tail jumps preserve the imported function's return register. Do not infer that Malloc or Realloc return void from that output. Neither the exact imported stats layout nor all imported argument/return types have yet been verified against libc.prx machine code.

pc_mspace.h introduces an independent bounded arena model over caller-owned backing storage: aligned allocation, zeroed allocation, coalescing free, in-place shrink/grow, move-on-reallocation, ownership checks and size/aggregate statistics. It uses host-owned map metadata and a mutex, not an emulation of retail heap metadata. No global malloc/free or C++ operators are redirected, and it is not registered as an import provider. Its first-fit behavior, zero-size semantics, 16-byte minimum and stats representation are model choices, not recovered retail contracts. A freed address can be reused and cannot by itself identify an allocation generation. The model is not yet benchmarked for hot-path use.

The exact model and test were compiled on Linux with g++ C++20 -O2 -Wall -Wextra -Werror and Clang ASan/UBSan. Four workers performed 100000 randomized allocation/reallocation/free iterations with content checks, alignment checks, capacity accounting and final full coalescence. Boundary tests exercised overflow, invalid/interior free, zeroing, exhaustion and failed-realloc preservation. Not a retail binary differential test. The test is registered in CTest; Linux CI now executes the registered native suites instead of only the tiling executable. Windows cross-compilation is not Windows runtime testing.

Next integration gate: recover all 12 mspace imports from the original libc, exact stats and error semantics, handle lifetime, allocator cross-calls and every external consumer of the arena. Keep allocator ownership coherent and preserve guest mapping identity before redirecting any member of the cohort.
