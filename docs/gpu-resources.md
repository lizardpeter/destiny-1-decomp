# GPU resources: how the engine creates, moves and releases them (eboot 01.33)

Established 2026-10-18 with runtime instrumentation (`D1_ND_RES_STATS`, `D1_ND_POOL_WATCH`,
`D1_ND_RES_EVENTS` in `native/src/native_gfx.cpp`) and Ghidra decompilation.

## Handles and pools

- Every engine resource is named by a 32-bit handle into the handle-pool table
  `*0x1A238A0` (0x40-byte descriptors). Pool index: `((h >> 13) >> 18 | 0x3FF) & (h >> 13)`
  (signed shifts as the engine does them); element index: `h & 0x1FFF`.
- Descriptor: `+0x10` element array, `+0x18` owner, `+0x20` end of the element array,
  `+0x30` element size (0x18 for the graphics pools), `+0x34` mask, `+0x38` ids (one short is
  0x0041 in every graphics pool seen).
- Object = `elem - (mask & *(elem + 8))`.
- A loaded package has its own pools: about 35 pools and about 1,300 vertex / index buffers in
  Earth patrol.
- `0x6FA10` relocates a whole pool by a delta (called from `0x70270`). Pool descriptors are
  written by `0x6FBC0` / `0x70C70`.

## Draws

- In-world, all but about 0.5% of draws take their vertex and index buffers from tag handles
  (`set_vertex_stream 0x7DEC20`, `set_index_buffer 0x7DEE30`).
- Every texture slot's T# pointer is a resource object of type 1-3, whose T# is at `+0`.
  The exception is the engine's own dynamic textures (e.g. 4 KiB tables around `0x562A5F000`),
  which have no resource events and are written by the CPU.

## The platform graphics callback table

The core resource manager drives the PS4 graphics layer through 15 callbacks, registered by
`0x7D3400` via `0xCFCC0` into `0x254EE30..0x254EEA0`:

| Global | Function | Role |
|---|---|---|
| `0x254EE30` | `0x7D2E60` | alignment per type (table `0x15D1010`) |
| `0x254EE38` | `0x7D2E80` | bind memory to a resource (type, handle, address) |
| `0x254EE40` | `0x7D3160` | re-point a resource at new memory (after a move) |
| `0x254EE48` | `0x7D2FE0` | release (textures: drop the top mip via `0x7D4480`; buffers: nothing) |
| `0x254EE50` / `0x254EE58` | `0x7D32B0` / `0x7D3350` | create engine-made index buffers (`0x7D3CB0` / `0x7D3BA0`) |
| `0x254EE60` | `0x7D4E20` | build resources from tag data |
| `0x254EE70` | `0x7D3560` | move: GPU copy (copy queue, `0x7DA960`) to new memory |
| `0x254EE78` | `0x7D37B0` | commit a move: swap handles and memory |
| `0x254EE80` | `0x7D4480` | rebuild a texture's T# for its resident mips |
| `0x254EE88` | `0x7D5520` | is the texture fully resident |
| `0x254EE98` | `0x7D39D0` | address / offset queries |
| `0x254EEA0` | `0x7D55C0` | flags |

Types in the bind / re-point dispatchers:

| Type | Bind | Re-point | Meaning |
|---|---|---|---|
| 1-3 | `0x7D4D40` | `0x7D4840` | textures (2D / 3D / cube): `0x7D61A0` sets the T# base (`0xF841B0`) |
| 4 | `0x7D4C30` | `0x7D4720` | vertex buffer: `+0x28` base (`0x7D64A0`), `+0` size, `+4` stride, count = size / stride |
| 6 | `0x7D4CB0` | `0x7D47B0` | index buffer: `+0x30` base, `+0x3C` count, `+0x38` 32-bit, `+8` size |
| 7, 9-0x12 | | | other kinds; 8 and 9 hold a descriptor at `+8` / `+0x18` (`0x7D4B90` / `0x7D48C0`); no draw texture is of these kinds |

The bind and re-point dispatchers start with a switch table and cannot be trampolined:
they are wrapped through the table entries instead.

## What the events guarantee (measured, Earth patrol, 5 minutes)

- Every buffer and texture a draw used had been bound by an event.
  - About 12 million buffer uses and 42 million texture uses: none unregistered.
- Contents at first use equal the contents at bind: 11,160 buffers and 3,887 textures, none
  changed.
- While a resource stays bound, nothing writes it.
  - Sampled every 10 s: 894 buffers unchanged.
  - Textures: 1,180 unchanged. The few that changed had been moved or unloaded: their
    descriptor named other memory.
- Moves are constant: about 27,600 vertex-buffer and 40,000 texture re-points.
  - 83,543 of 83,556 re-points carried identical contents.
  - The 13 others: 4 handle swaps (`0x7D37B0`), 9 same-size textures with new contents.
- Conclusion: a resource's contents change only with an event on it, or through a GPU copy
  command (DMA_DATA) in a command list.
  - The engine copies streamed mips and new glyphs (fmt 134 / 138 textures, e.g. 1024x512)
    into resource textures on the copy queue, just before the draws that use them.
  - Such a copy carries no resource event: the renderer marks every texture in the written
    memory stale and copies it again (about 3 per second in patrol).
  - The renderer keeps its copy per resource and epoch.
  - After an event, it compares contents once, so a move costs a hash and nothing else.

## Native implementation

`resreg` (`native_gfx.cpp`): a lock-free table from resource object to `id << 32 | epoch`,
fed by the wrapped callbacks. Captured draws carry it per T# slot, vertex declaration element
and index buffer.

The renderer (`gfx/nrender.cpp`):
- **Textures** are keyed by resource id plus the T# without its base address.
- **Buffers** are kept as whole-buffer copies keyed by resource id plus offset.
- Neither is write-protected or version-checked.
- Unregistered memory (dynamic textures, inline vertices) keeps write tracking.
- `D1_ND_RES=0` turns the event path off.

## Dynamic memory: what is no engine resource (2026-10-18, later)

After the resource events, a fault census (`D1_NG_LOG_FAULTS`: writing code address and what
the renderer kept there) showed three kinds of memory still write-tracked.

- **Dynamic geometry: about 170 vertex and 107 index buffers per frame.** These sit in pool
  1028 (the engine's global pool) with memory around `0x5_6269_xxxx`.
  - They come in pairs and triples (handles `…18/19`, `…1A/1B`, `…1C/1D/1E`): double- and
    triple-buffered buffers the CPU fills every frame.
  - Native: copied for each draw (the vertex range it uses), never cached or tracked, as a PC
    engine treats dynamic buffers. About 0.1 MiB per frame in patrol.
- **Dynamic textures: about 11, about 600 KiB.**
  - The engine writes them only through a lock / unlock API over a table of 0xE0-byte slots at
    `0x26F23B0`: lock `0x818F80 -> 0x81A600`, unlock `0x818FA0 -> 0x81A640`, descriptor
    `0x818DD0`.
    - Slot memory: `+0x40` (or `+0x48` when flag bit 3 of `+0` is set).
    - Row pitch: `+0xD8`. Locked bit: bit 2 of byte `+2`.
  - Only five functions lock slots: `0x33E600`, `0x8439E0`, `0x88FE50`, `0x8A3620`,
    `0x8CC630`.
    - `0x88FE50` refreshes two double-buffered tables every frame: slots 6/7 (pitch 512) and
      8/9 (32x32, pitch 64) at `0x578B00000..`.
  - Native: the unlock is replaced (it only clears the locked bit) and announces "the CPU wrote
    this memory", the equivalent of Unmap.
- **Small render targets the CPU may rewrite** (light tables): announced the same way. Copies
  are kept per address generation, bumped by those announcements and by the copy-queue /
  WRITE_DATA writes the renderer executes itself.
- **Our own readbacks** of small targets used to fault on our own protection. With nothing
  protected, they no longer do.

Result: no page is write-protected any more, so there are 0 write faults in patrol and orbit,
on both Linux and Windows. `D1_NG_VERIFY_DYN=1` hashes every dynamic texture at every use:
525 checks, 0 changed without an event.

## Stale registry entries (2026-10-18)

Registrations are keyed by object address and were never removed. Handle pools are freed (a
package unloads) and relocated (`0x6FA10`), so an old object address can later hold anything.
In an orbit run, about 130 lookups hit addresses whose registered handle no longer resolved
there (`handle ... now names 0`). Several were around `0x4045_xxxx`, where the UI builds T#s in
scratch memory. The registry now stores the handle with each entry and rejects the entry when
`resolve_handle_safe(handle)` names another address.


## Resource Bind/Repoint Dispatch, Streaming Mips, Submesh VBs & Camera FOV (2026-11-23)

- **Resource manager bind (`0x7D2E80`, `*0x254EE38`) and repoint (`0x7D3160`, `*0x254EE40`) 18-type switch tables (`0x7D2F84` / `0x7D3258`)**:
  - Types **1, 2, 3** -> `0x7D4D40` / `0x7D4840`: Texture (`Gnm::Texture`, 32-byte `T#` at tag `+0x00`; authored full dimensions at `+0x28` u16 `full_width`, `+0x2A` u16 `full_height`, `+0x2C` u16 `full_depth`, `+0x31` u8 `full_mips`, `+0x34` u8 `has_hi_mip`).
  - Type **4** -> `0x7D4C30` / `0x7D4720`: Vertex Buffer (`+0x0` u32 size, `+0x4` u32 stride, `+0x28` u64 base, `+0x30` u32 vertex count).
  - Type **6** -> `0x7D4CB0` / `0x7D47B0`: Index Buffer (`+0x8` u64 size, `+0x30` u64 base, `+0x38` u32 is_32bit, `+0x3C` u32 index count).
  - Type **7** -> `0x7D4DA0`: Generic `Gnm::Buffer` (`V#` at tag address).
  - Type **8** -> `0x7D4B90`: Texture with 8-byte header (`Gnm::Texture` 32-byte `T#` at `tag + 0x08`, NOT a pixel shader).
  - Type **9** -> `0x7D48C0`: Vertex Shader (`VsWrapper` at tag address, `Gnm::VsShader` at `+0x14`, fetch shader at `+0x08`).
  - Type **10** -> `0x7D4AB0`: Geometry Shader; Types **11 & 14** -> `0x7D4970`: LS/ES Shader.
  - Types **12 & 13** -> `0x7D49F0`: Pixel Shader (`Gnm::PsShader` at tag `+0x00`, `spiShaderPgmLoPs/HiPs` at `+0x08/+0x0C`).
  - Type **18** -> `0x73D50`: Generic data block pointer.
- **Streamed texture mip lifecycle (`0x0CB940` -> `0x0E01E0` -> `0x7D4480` `texture_mips`)**:
  - When a streamed texture (`*(uint8_t*)(tag + 0x34) != 0`) binds with an active hi-mip block, `0x0CB940` first calls `h_bind` (`0x7D4D40`), which writes `T#.base = hi_mip_addr` while `T#.width/height/last_level` are still at the half-resolution low-mip count (`full_mips - 1`), and then calls `0x7D4480(handle, low_mip_addr, hi_mip_addr)` (`h_mips`) to set `T#.width/height` to `full_w x full_h` and `last_level` to `full_mips - 1`.
  - When `hi_mip_addr != 0`, only mip `0` resides at `hi_mip_addr`; mips `1..full_mips-1` remain at `low_mip_addr`. The renderer allocates one permanent full-mip `VkImage` (`full_w x full_h`, `full_mips` levels) keyed solely by resource ID (`res >> 32`), uploads mips `1..full_mips-1` from `low_mip_addr` and mip `0` from `hi_mip_addr` in place, and switches the active mip range via `VkImageView` `baseMipLevel` (`0` vs `1`).
- **Multi-submesh vertex declaration switch (`0x845DE0` -> `0x7DEB70`)**:
  - In `0x845DE0`, when consecutive submeshes of the same mesh (`uVar24 == local_874`) use different vertex declarations (`uVar8 != local_894`), the engine calls `0x7DEB70` (`set_vertex_decl`), which resets `dev + 0x1D78[0..31] = 0xFFFFFFFF` (the device redundant-bind cache) without unbinding CUE stream 0/1 (`0xF8B600`), and skips re-calling `0x7DEC20` (`set_vertex_buffer`) for stream 0/1. `DevState::vb_handle[32]` preserves the bound stream handles across `0x7DEB70` so submeshes 1+ are not dropped as `"null vertex buffer"`.
- **TFX technique state application (`0x82B3F0..0x82B830`)**:
  - TFX techniques store 4 render-state indices at `technique + 0x20`, a VS handle at `technique + 0x28` (`0x82D070`), and a PS handle at `technique + 0x2A8` (`0x82D020`). Hooking `0x82B490`, `0x82B5C0`, and `0x82B730` alongside `resreg` types `9`, `12`, and `13` pre-translates shaders and warm-builds persisted `DiskPipeSpec` pipelines (`native_pipelines.bin.specs`) off the draw path.
- **First-person world camera vs. weapon viewmodel FOV (`0x526FE0`, `0x5270E0`, `0xBD3BF0`, `0x6BF320`, `0x8EFAB0`)**:
  - `0x526F80` resolves the camera globals tag (`*(float*)(g + 0x18) = 1.25663706 rad = 72.0 deg`). `0x526FE0` returns the default world camera horizontal FOV in radians, and `0x5270E0` returns the first-person base world horizontal FOV in radians to `0xBD3BF0(unit, zoom_slot)`, which divides `0x5270E0()` by the weapon ADS zoom magnification `0xBD3740()`.
  - `0x6BF320` stores weapon viewmodel FOV (`param_3[4] = 0x737C80`) separately from world camera FOV (`param_3[5] = 0xBD3BF0`). `0x8EFAB0` writes world FOV to `DAT_0273c3e0 + 0x4ac` (building the visibility culling frustum before culling and `"first-person environment view"` in `0x52CE20`) and weapon viewmodel FOV to `DAT_0273c3e0 + 0x554` (`"first-person player view"` / `"first-person iron sight view"` in `0x80F730`).

## Native identity and exact geometry keys

The renderer resource registry now lives in a reusable native ResourceRegistry, independent of the retail handle resolver. The adapter resolves the original object and validates its handle; the registry owns the identity and version. Changing the handle at an existing object address allocates a new resource identity instead of continuing the old epoch. Non-inserting events cannot adopt a recycled address. A coherent atomic snapshot prevents observing a new handle paired with an old identity. A reader that cannot obtain a stable snapshot treats the resource as unregistered. True same-handle/same-address destruction and recycling still require the upstream pool lifetime contract; no slot removal is guessed from the original release callback (which may only discard a top mip).

Geometry cache keys now retain the complete resource ID, offset, length, index width and count; for unregistered geometry they retain the full address, rather than packing it into 40 bits. Hash collisions no longer imply identical resources. Index interpretation is part of the key because cached index range metadata must not be shared between 16-bit, 32-bit and untyped data. Replacement invalidates the thread-local native buffer memo generation before deferring the old allocation for GPU retirement. This is a native ownership and cache identity correction, not a full removal of the original resource manager. Unit test added for 100000 resource generation changes with concurrent readers and deliberately colliding geometry hashes; CI pending.

## Package-pool destruction as a native lifetime boundary

The original 0x6FBC0 body masks the input to 14 bits, tests the pool occupancy bitmap, calls two destruction callbacks, frees the datum allocation, unlinks the descriptor, clears the occupancy bit and zeroes the complete 0x40-byte descriptor. Its disassembly confirms the relocatable prologue and the pool-index mask. This is the actual pool destruction event, unlike 0x7D2FE0 (mip release). The native registry now brackets the original body with per-pool retirement epochs: odd epochs reject publication and reads during teardown, and even epochs after teardown invalidate every previous resource identity, including same-handle/same-address reuse. Expired entries can be reclaimed during probing without breaking the collision chain; no full-table scan is needed at unload. A failed hook leaves the original registry behavior and logs the issue. This is lifetime tracking, not yet replacement of the package allocator or protection of all captured CPU bytes against concurrent frees. Added tests for 10000 pool retire/reuse cycles (320000 resource insertions), stale snapshot rejection, negative handle partition and same-address reuse; CI pending.

## Source-defined resource bind/repoint cohort

D1_PC_NATIVE_BIND=1 replaces the ordinary texture (1–3), vertex buffer (4), index buffer (6), header-prefixed texture (8) and data-pointer (18) cases at the registered callback boundary. Recovered the exact field writes of 0x7D4D40/0x7D4840, 0x7D4C30/0x7D4720, 0x7D4CB0/0x7D47B0, 0x7D4B90, 0x73D50 and helpers 0x7D61A0, 0xF841B0, 0xF839A0, 0x7D64A0, 0x162B80, 0x7D27D0, 0x781E0. Disassembly confirms 0x7D59F0 is RET-only and both dispatchers return AL, not all of RAX. Corrected the default wrapper signature and stopped publishing a successful resource event when the retail callback returns false. Initial VB/IB binding sets count, while relocation preserves it. VB divide-by-zero and invalid handles are rejected rather than reproducing an invalid access. The original tag fields are still mirrored; the native registry and loader receive the resulting buffer source range. Other kinds and split-mip rebuilding use the original implementation. The type-8 descriptor is not fed to the type-1 texture loader. Added 100000 byte-preservation/return contract cases; this is source-derived validation, not a full original-machine-code comparison. Integrated CI and gameplay pending.

The complete experimental switch now covers generic buffer (7, 0xF7D050), VS (9, 0x7D48C0), GS (10, 0x7D4AB0 / 0x7D4AA0), LS/ES (11/14, 0x7D4970), PS (12/13, 0x7D49F0) and constant-success kind 16 (0x1F70). Repoint type 14 is deliberately unsupported, matching the different original dispatch table. GS's second program pointer uses the recovered variable metadata and 256-byte alignment; the native path requires its metadata range to fit a tracked mapping instead of guessing its length. Unsupported types return false without invoking the retail switch. This replaces descriptor binding logic, not shader translation or arbitrary original engine callers. A further 100000 randomized field-preservation cases exercise these kinds; full CI and actual rendering remain pending.

The experimental cohort also reconstructs release dispatcher 0x7D2FE0 and texture release 0x7D43E0. The latter checks the authored full-mip count, the high-mip flag and the descriptor's last mip (0xF84640, special types 14/15 return zero). Only an actual high-mip transition invokes the existing hooked 0x7D4480; the native registry then publishes the resulting epoch once. Other release types are RET-only or unsupported; they do not falsely invalidate GPU resources. Original pool teardown remains the true destruction boundary. Added exhaustive 131072 type/mip/full-count/high-flag combinations, and corrected the VS test to cover the no-fetch-shader branch. Full mip reconstruction remains retail. This is still opt-in and not live validated.

## Source-defined mip transition and queries

Under D1_PC_NATIVE_BIND the 0x7D4480 transition body now constructs the exact recovered argument plan: authored full or half dimensions (with special minimum and BC rounding), volume depth, mip count, packed format/swizzle from 0xF846B0/0xF7D320, tile index and MSAA fragment count. The original 0xF83530 and 0xF83AB0 descriptor constructors remain explicitly called for their pitch/layout calculations; the original mip transition body and final base setter are not called. This preserves the original constructor's failure/write behavior rather than guessing pitch or replacing a full texture descriptor with a partial one. Callback slots for 0x7D5520 full residency and 0x7D55C0 flags are also replaced when their original addresses match. Added 100000 randomized mip argument/base byte tests; no physical-GPU or whole-constructor differential validation yet. The original constructor internals, streaming copy and CPU pool allocator remain.

## Source-defined resource copy reservation and enqueue

The 0x7DA8B0 reservation, 0x7DA910 capacity query, 0x7DA960 enqueue and 0x7DB260 record initialization were recovered from original disassembly. Under D1_PC_NATIVE_BIND the first three entry points use source-defined implementations, preserving the 0xA00-slot shared cursor, one-empty-slot reservation, exact capacity-query comparison, 0x40-byte record layout, optional pre-reserved record and release of a ready record only after both callback arguments have been copied. The enqueue ABI uses destination, source, 32-bit byte length, callback, two pointers to 64-bit callback arguments and optional record pointer. CAS and exchange preserve the ordering required by concurrent producers. The original queue allocation, consumer and completion callbacks still run; this is not yet a replacement of the retail queue storage. Added exhaustive 6553600 cursor pairs and 100000 record-field cases. The full consumer and resource movement metadata still require verification before replacement.

The experimental queue now also replaces 0x7DACF0 (issue ready prefix) and 0x7DAB10 (retire completed prefix), including wraparound, one-time completion sample and unsigned sign-bit sequence comparison from 0x7DB2C0. The issue path still calls 0x7DB2D0/0x7DB340/0x7DB4B0 to preserve the original copy batch and completion label operations; the retirement path calls original callbacks with copied stack arguments and releases each slot only afterwards. These replacements assume the same single-consumer contract as the original; they do not remove queue allocation or replace GPU completion semantics. The consumer may observe newly appended ready work before returning rather than capturing a fixed tail snapshot. Test coverage extends all 6553600 cursor pairs to membership checks and 100000 completion sequence checks. Integrated gameplay validation pending.

## Native copy batch and checked mapping lifetimes

The opt-in blitter cohort now replaces 0x7DB2D0/0x7DB340/0x7DB4B0. It retains engine TLS device selection, begin/reset/close calls, 32-bit batch sequence, 4000-command split condition and EOP completion label, but uses one ordered native DMA capture per resource move rather than breaking overlapping copies into multiple console DMA fragments. The native renderer executes an actual host memmove (not a GPU packet or shader). This changes command-count batching intentionally; it does not move an already resident VkBuffer/VkImage and cannot yet eliminate the original CPU resource pool. Captured source/destination mapping identities are checked together under the host mapping-table lock before performing the move. A stale/untracked mapping terminates the opt-in run with a diagnostic rather than publishing a false success completion. This protects tracked mapping lifetimes, not suballocations of a still-live pool. The original blit consumer's batch helper calls now enter these source-defined replacements. The non-experimental path remains as before; physical-GPU and actual copy-queue execution are not validated yet.
