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
