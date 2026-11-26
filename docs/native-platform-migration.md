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

### Retail libc export recovery and native wide-string leaf

Read the 927802-byte libc.prx directly through the project's R2 object API (ETag 6953eee266069be46b1434b2a96fa0ce). Parsed the SCE dynamic string/symbol tables and resolved each export by its original NID. These are libc module virtual addresses, not eboot offsets.

| Export | VA | Symbol size (bytes) |
|---|---:|---:|
| sceLibcMspaceCreate | 0x9780 | 8 |
| sceLibcMspaceDestroy | 0x9790 | 5 |
| sceLibcMspaceMalloc | 0x7EB0 | 8 |
| sceLibcMspaceFree | 0x97A0 | 364 |
| sceLibcMspaceCalloc | 0x9910 | 149 |
| sceLibcMspaceRealloc | 0x99B0 | 81 |
| sceLibcMspaceMemalign | 0x9450 | 37 |
| sceLibcMspacePosixMemalign | 0x9A20 | 167 |
| sceLibcMspaceReallocalign | 0x9A10 | 8 |
| sceLibcMspaceMallocStats | 0x9AD0 | 5 |
| sceLibcMspaceMallocStatsFast | 0x9AE0 | 5 |
| sceLibcMspaceMallocUsableSize | 0x9AF0 | 49 |

UsableSize takes a single pointer (RDI), tests null, reads the preceding 8-byte chunk header and inspects flags and a conditional tail byte. It does not accept an mspace handle. This is direct evidence that mixing host/model and retail allocations before migrating all consumers can cause invalid metadata reads. Create clears R8D before tail-jumping; Malloc duplicates RDI into RDX; Reallocalign duplicates RDI into R8 before its tail jump. These are internal argument adaptations, not evidence that the public prototypes have the extra arguments. The full allocator implementation and stats output ABI remain to be recovered.

wcslen (NID WkkeywLJcgU, VA 0x17030, 34 bytes) is a relocation-free loop that compares a 16-bit memory operand to zero, advances by two bytes, then divides the byte distance by two. Its result counts 16-bit code units, not Unicode scalar values. The native leaf uses explicit 16-bit loads via memcpy to avoid alignment and host wchar_t ABI dependencies. It is library-scoped and enabled with the existing default native leaf cohort; D1_PC_LIBC_LEAVES=0 still restores the original. Current native libc import coverage becomes 20 default leaves plus 18 opt-in math/time leaves (38/143 total). No allocator or locale-dependent import was redirected.

Local validation: 100000 randomized original-machine-code differential calls (including unaligned addresses), plus 128 guard-page boundary cases passed on Linux x86-64. The original 34-byte relocation-free routine was copied only into a private local execution harness, never into the repository. The committed portable unit test passed optimized and ASan/UBSan builds on 100000 randomized cases plus a UTF-16 surrogate-pair case. CTest registration added. Full Windows runtime and live-game compatibility remain unverified.

### Native TLS storage and lifecycle

The transitional TLS adapter now uses a common checked allocation/layout implementation (pc_guest_tls.h). Static TLS preserves the Variant II negative offsets and the published rounded extent, copies the initialized image, zeroes the remainder, and respects the ELF alignment even when it exceeds 64 bytes. Dynamic TLS uses the module's alignment, full-width module IDs, independent zero-initialized storage per guest thread/module, and rejects out-of-range offsets and missing TCBs. Invalid image extents are rejected at link time; dynamic allocation failure returns null instead of dereferencing an allocation failure. Windows checks TlsSetValue and releases an unusable high slot before failing setup.

The native storage classes have RAII ownership, but the existing retail adapter intentionally retains process-lifetime ownership for now. hle_pthread.cpp backs guest keys with host pthread keys and invokes guest destructors from host key callbacks. Freeing the TCB or dynamic blocks at thread_trampoline return could therefore cause use-after-free in those callbacks. A later coherent key/destructor migration must establish correct teardown ordering, including cancellation, repeated destructor calls and the FIOS callback thread. No assertion that the TLS leak or PS4 TLS dependency is gone.

The exact standalone header and test passed local g++ C++20 -O2 -Wall -Wextra -Werror and Clang ASan/UBSan. Eight workers exercised 100000 independent allocation/initialization/access/destruction cycles, plus static alignments 1 through 4096, invalid images and extent overflow. CTest registration added. This validates the storage implementation, not the full production adapter or guest teardown. Windows build and live game are pending.

### Native pthread-key cohort

The import inventory lists scePthreadKeyCreate/Delete/Getspecific/Setspecific. The old adapter assigns one host pthread key per guest key, exposes a slot as used before host key initialization completes, and can read an inactive slot in Getspecific. Added a separate native key registry with generation-tagged per-thread values, coherent create/delete/get/set locking, bounded four-pass destructor dispatch, clear-before-callback semantics and callbacks outside registry locks. Reused guest key numbers cannot resurrect earlier per-thread values. The existing default Getspecific also now rejects inactive slots; other legacy concurrency limitations remain.

D1_PC_PTHREAD_KEYS=1 selects all four native key imports together (default remains the original host-per-key implementation). A single host pthread key provides thread exit notification and per-thread state; the callback re-publishes that state during native destructor dispatch and invokes guest destructors using the existing System V root-frame bridge. This does not remove host pthreads, establish retail destructor pass count/order, or prove cancellation and destructor-initiated exit semantics. Static and dynamic guest TLS storage intentionally remains alive throughout process lifetime. Main exits via _exit, and the FIOS provider is process-lifetime. TLS reclamation and host thread registry teardown remain pending.

The exact standalone registry and test passed local g++ C++20 -O2 -Wall -Wextra -Werror and Clang ASan/UBSan. Eight workers completed 100000 create/set/get/drain/delete cycles; dedicated cases cover stale generations, per-thread isolation, capacity, clear-before-destructor, reentrant sets and the four-pass limit. This is not an original machine-code differential test, nor a test of the production host callback integration. New CTest registration. No live-game or Windows runtime claim.

### Native Vulkan resource allocation policy

All mapped Vulkan buffer creation (upload ring, geometry pools, staging, readback) now uses one native allocation path. HOST_VISIBLE and HOST_COHERENT are mandatory for mapped allocations; only preferred flags such as DEVICE_LOCAL and HOST_CACHED may be relaxed. Compatible memory types are tried in preference order, never an incompatible type. Buffer creation now checks bind and map results and tears down failed objects, and explicitly unmaps before release. Geometry pool creation uses the same path rather than its own divergent fallback.

Image and buffer allocation query VkMemoryDedicatedRequirements through core Vulkan requirements2 functions. Images requiring or preferring dedicated allocations bypass the shared image heap; dedicated allocations carry the actual VkImage in their allocation info. All image binds are checked before layout commands can reference the image; initialization checks mandatory dummy image creation. These are renderer-side native allocation changes, not yet a replacement of the original engine's memory-pool callers. Existing resource event identity, original descriptor conversion and ordered submission remain intact. No claim of improved FPS or resolved visual parity without physical-GPU testing.

Added a driver-independent CTest suite for required/preferred memory flags, compatibility masks, candidate uniqueness, ordering and empty results (100000 randomized cases). Full build and execution pending at commit time.

### Source-defined native command-list close (experimental)

Reviewed Ghidra pseudocode and original disassembly for 0x7DCEC0, 0x7DE950, 0x7DB810, 0x7DD060, 0x7EF3A0 and 0x7DEA90. The latter enqueues a worker record before its recording starts; worker close must not enqueue it again. Main close emits the dependency label, optionally rotates progress labels, records the segment metadata, marks ready, appends to the 64-entry ring, advances tail and toggles context index. The worker sign test is a 32-bit wrapping subtraction of completion and previous progress; the submit enable byte is at 0x18DCC30. Original close uses MFENCE and a locked exchange loop before publishing readiness, and MFENCE before publishing the main ring tail.

D1_PC_NATIVE_CLOSE=1 replaces the body of both close functions with a source-defined native close. It calls the original engine's dependency-label routine (0x7EF3A0), which reaches the native EOP callback, and the original segment bookkeeping (0x7DB810) for any genuine legacy extents. The list is published only after final completion events are captured, then the current context is detached, then the ready flag is published. The main record is enqueued based on native list existence rather than synthetic command-buffer bytes. The worker uses its reserved ring slot. No word is advanced or written to make a command-buffer segment nonempty. The import boundary accepts zero retail segments for a registered native record in this mode. Existing default mode remains unchanged. The original begin, ring reservation, submit drain, progress wait and descriptor structures are not removed; 0x7DC140 also calls the segment bookkeeping so it is not safe to delete that routine wholesale. The worker path intentionally follows the previous forced-nonempty native behavior even for empty native lists, rather than skipping the dependency label as the retail empty-buffer path did.

Added unit coverage of 65536 flag/kind combinations, 100000 unsigned wrap/slot cases and registry take-by-record ownership. These are source-derived contract tests, not original machine-code differential or live render tests. Build and gameplay pending. The legacy marker remains only in the default path until this cohort is validated. No performance improvement asserted.

### Device-local geometry pool (experimental)

`D1_NG_LOCAL_GEOMETRY=1` opts into unmapped Vulkan device-local geometry blocks. Registered whole-resource vertex/index buffers are prepared on the ordered primary recording path, with exact resource identity/epoch/range deduplication, staging snapshots, zero-padded 4-byte copies, transfer-to-vertex/index dependencies, and submission-fence retirement of staging allocations. Replaced GPU ranges use the existing submission-aware deferred-free queue. The background buffer preloader is disabled for this mode; ordinary mapped and dynamic geometry paths remain available if a resource cannot be prepared. Device-local blocks are capped at half the configured geometry block budget. The original game still supplies the resource identity and CPU source bytes: this does not yet replace the game's CPU-side resource allocator. Independent staging allocations are currently per uploaded resource, so batching them is a follow-up before making this the default. Physical-GPU verification and performance measurements are pending; no performance improvement is asserted.

### Submission-owned geometry staging pages

The experimental local geometry path now suballocates 4 MiB mapped staging pages (large resources get appropriately sized pages), bounded to 128 MiB per submission. Failed snapshots roll back the latest span; both fence retirement and abandoned recorded lists free their staging pages. It no longer creates one VkDeviceMemory per mesh. The experimental local cache keys include the entire resource epoch, preventing pre-pass replacement of one draw's resident buffer with a different epoch before earlier draws execute. The mapped default retains its existing relocation optimization. This does not solve an upstream source address that has already been overwritten before capture; original pool lifetime recovery remains necessary. Added a million-span driver-independent test, pending CI execution.

### Native record drain (experimental)

The original 0x7DD060 body was checked against disassembly including the actual Gnm jump thunks 0xF84740/0xF84750, locked sequence counters, half-range unsigned head/tail comparison and JNS progress test. With both D1_PC_NATIVE_CLOSE=1 and D1_PC_NATIVE_DRAIN=1 it is replaced by a source-defined drain that consumes the ready FIFO prefix, preserves the original cooperative scheduler lock and progress check, records flip timing samples, and transfers ownership directly from the native record registry to the GPU queue. No Gnm submit import or DCB/CCB pointer array is needed on this path. The current host queue has no recoverable submit error (failures terminate the runtime), so the original console flip-resubmission error branch is intentionally not implemented. The flip-rate hint is retained as existing host telemetry, not an imposed 30 Hz cap. Source-defined drain does not yet replace original ring producers/reservation or engine synchronization. Default close/drain remains unchanged. Added 100000 wrap/ready checks to the existing contract suite; live validation remains required.

The native registry now stores record identities directly; the +0x30 console array-address conversion exists only at the legacy import boundary. Both legacy submit variants and the source-defined drain use the same native queue ownership function, eliminating the duplicate sync/async submit wrappers. No changes to the queue's two-flip backpressure or fence completion semantics are intended. Build pending.

### Native FIFO, begin and worker reservation

Disassembly of 0x7DCCE0 returns the new record pointer in RAX; the old void hook discarded this ABI result. Corrected default hook return type. Recovered 0x7DEA90 worker reservation, 0x6AC60 record-header initialization, 0x7DCCE0 main begin and 0x7EF890 label initialization. D1_PC_NATIVE_RING=1 (requires native close and drain) uses a native mutex-protected 64-record FIFO with explicit reservation/ready/consume transitions. Main lists reserve ready positions at close; worker lists reserve before recording and mark ready only after native publication. A blocked head cannot be overtaken. Original slot pointers, counters and ready flag are mirrored for compatibility, not consulted by the native FIFO consumer. Main begin initializes the original record layout and cache sentinels in source; it preserves original reset, label allocation and context selection routines. Capacity is conservatively bounded to 32 main records based on the gap from +0x2E0 to +0x132E0; overflow and duplicate reservations fail loudly rather than overwriting active records. Inherited begin, original record storage, scheduler lock and label handling remain. No live GPU validation. Added CTest for wraparound, full queue, out-of-order worker completion and 100000 concurrent reservations.

### Bind-time geometry ownership and mapping-safe background preparation

Buffer load jobs now carry the source mapping identity captured at the resource bind event. The background loader copies through the tracked host mapping table under its lock, checking the mapping identity, readable extent and original resource epoch before using a CPU-owned snapshot. Both mapped and experimental device-local paths consume the snapshot rather than rereading an arbitrary recycled source address. With D1_NG_LOCAL_GEOMETRY enabled and ordered recording selected, bind-time jobs stage persistent native Vulkan buffers in the shared upload submission; execute() submits that slot before recording dependent draws. The on-demand local path also copies through the current mapping guard. Original resource epoch checks and fence-owned staging remain in place. The lock guards tracked mapping destruction, not arbitrary writes to a live mapping or untracked host allocations; legacy raw pointer use in texture/shader and draw capture remains. No physical-GPU validation. The native FIFO standalone header/test blob hashes were verified against commit 9640cd3 after local optimized C++20 and ASan/UBSan runs passed 131072 wrap/capacity cases and 100000 concurrent reservation cases.

### Owned contiguous texture preparation

Background texture jobs now capture source mapping identity at enqueue. For contiguous registered surfaces, the loader takes an owned tiled snapshot, detiles and generates missing mips from that snapshot, and compares it against the same tracked mapping before publishing under the renderer lock. Prepared uploads carry the source extent and content hash; they cannot fall back to raw source reads or be applied to a split streamed texture. Speculative source and linear buffers are each bounded at 128 MiB; oversized textures remain on-demand. Cached geometry hits avoid mapping-table locks or redundant background snapshots. This does not remove the original texture format, the split-mip streaming path, or legacy untracked-memory access; live visual validation remains pending.

The mapping copy, comparison and delayed GPU-write helpers now share overflow-safe lifetime, range and permission checks. Background geometry verifies the owned bytes again at publication (in addition to the resource epoch). Added a 200000-case randomized mapping contract test. The validation concerns the tracked mapping table; arbitrary memory protection changes, suballocations inside a still-live pool and untracked mappings remain separate lifetime problems.

### Host-owned command-buffer backing

0x7EF7D0/0x7EF560 originally allocate the CBM's 512-slot table and chunk metadata via retail new, then request two GPU-memory allocations via 0xB49B0 (label and backing). D1_PC_NATIVE_CBM=1 replaces initialization at 0x7EF7D0 for the full backing and label with a registered, ordinary host mapping. It retains retail metadata allocation and chunk initialization (0x6B100/0x6B150/0x6B190), exact manager fields and 512 KiB chunk size, so the existing reclaim and destructor still understand the layout. Slots start free with explicit invalid-chain sentinels. The host backing is intentionally process-lifetime; the retail destructor only destroys metadata and does not free it. Repeated initialization with active metadata fails loudly. This removes two original GPU pool allocations, not the original chunk allocation/reclaim logic. No gameplay validation. Added 200000 sequence and 1536 backing-layout test cases.

### Native CBM chunk/slot ownership

The retail init call at 0x7E9F98 requests 0x6300000 bytes (99 MiB, 198 full chunks). All recovered 0x7EF8B0 callers request 0x80000 bytes aligned to 0x80. D1_PC_NATIVE_CBM_POOL=1 (requires D1_PC_NATIVE_CBM) bypasses retail metadata new, 0x6B100/150/190, and replaces label reservation, full-chunk allocation, close, single/bulk reclaim, wait, frame stats/reset, block-size query and teardown (0x7EF460/8B0/3A0/2A0/180/0x7EEF90/0x7EF630/0x7EFC00/0x7EF5C0). Native slots distinguish open/closed/free independently of the original zero sentinel, own chunk IDs through completion, reserve at most 511 simultaneous slots and retire only the completed FIFO prefix. The old slot table and record header fields are mirrored for remaining consumers; native frame statistics do not emulate the original allocator counters. EOP emission still goes through the existing native device hook; record memory and context pointer layout remain unchanged. Unexpected partial-chunk allocations abort rather than guessing a new contract. Teardown waits for all closed slots and leaves backing mappings process-owned. Added tests for 396000 chunk allocations over 198000 wrapped slot lifetimes, capacity, out-of-order close and 100000 concurrent retirements; live validation remains required.
