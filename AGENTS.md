# Working in destiny-1-decomp

Decompilation and native PC port of Destiny (PS4, CUSA00219, patch 01.33). Read this before changing anything.

The structure and method come from `lizardpeter/ghosts-decomp` and `lizardpeter/black-ops-1-decomp`. This repo
owns its own copy of the tools and adapts them; there is no shared toolkit. It replaces the earlier research
repo `lizardpeter/destiny-1`, which stays as it is; a path in a note that does not exist here refers to that
repo.

## Mandatory native-only PC architecture

The project owner requires a fully native, properly optimized PC game, not a faster PS4 compatibility layer. The production target is Windows/Linux x86-64, Vulkan and host-native platform services. Original gameplay logic and the faithful decomp are preserved; original console graphics/platform implementations are migration evidence, not the desired production backend.

**NO LEGACY FALLBACK.** New or completed native subsystems must have exactly one production implementation. Do not add environment switches that restore retail Gnm/Gnmx, console command buffers, PS4 graphics pools, old file/platform providers, or other replaced infrastructure. Remove obsolete fallback branches as their replacements become complete. If a required contract remains unreconstructed, identify it explicitly as a blocker, reconstruct it from source evidence, and do not silently claim native completion or hide the failure. Do not turn an unsupported production case into a crash merely to inflate coverage: recover the missing contract before removing its dependency. Original code may be called by an explicitly isolated differential research harness, never as a fallback for a completed native production subsystem. Existing incomplete dependencies are migration debt, not an approved permanent exception.

Priority order:
1. Independent, fence-owned Vulkan resource lifetimes and host-native asset storage; no dependency on recycled console pool addresses.
2. Asynchronous asset ingestion and native texture/mesh preparation before first use; no synchronous detiling/upload in the steady-state draw path.
3. Persistent native material, shader and geometry bindings; direct native render commands from engine state, not repeated console descriptor decoding, re-resolution or synthetic command streams.
4. Efficient multithreaded recording, native batching and GPU-driven work where equivalent; no unnecessary global locks, copies, barriers, queue waits, or per-draw allocations.
5. Ahead-of-time shader translation and pipeline preparation, persistent validated caches; no gameplay compilation in the steady state.
6. Native file I/O, libc, allocation, threads, synchronization, timing and other platform services; remove remaining original PRX and console platform dependencies as complete contracts are recovered.

Keep original gameplay semantics and the faithful `src/` reconstruction separate from PC enhancements. Native code is not evidence of original binary equivalence by itself. Large coherent patches are preferred over tiny speculative optimizations or repetitive progress reports. Diagnose root causes from original machine code, runtime logs and measured CPU/GPU data. Never sacrifice correctness to make a benchmark look better.

Acceptance: compile and run tests on Windows/Linux, launch the game, compare rendered output and streaming stability (including long runs and pool reuse), measure CPU capture/preparation/recording, GPU time, FPS, frame pacing and memory. Record build SHA, configuration and baseline. A successful compile, native API name, function count or theoretical speedup is NOT proof of PC parity. No fabricated progress or completion percentages. See [native-only architecture](docs/native-only-architecture.md) for the migration checklist.

## The one rule: generated vs authored

Every file is exactly one of these. They never share a folder.

| Kind | Where | Who writes it |
|---|---|---|
| **Generated** | `analysis/<build>/`, `STATUS.md` | Only the scripts in `tools/`. Never hand-edit. |
| **Authored** | `src/`, `native/`, `backend/`, `tests/`, `docs/`, `records/` | People and agents. |

If a generated file is wrong, fix the script that wrote it and re-run. Don't patch the output.

## Layout

```
target.toml          builds (sha256 + R2 key of each original file), project id, Ghidra version
STATUS.md            GENERATED progress table
analysis/<build>/    GENERATED per build
  functions.tsv        every function: address, size, unwind length, name, bytes hash, decompile status
  imports.tsv          every imported symbol slot: NID, library, name
  unwind.tsv           function starts and lengths from the unwind table
  pseudocode/<block>.c Ghidra decompiler output, grouped by 64 KB address block
  disasm/<block>.asm   disassembly, same grouping
  ghidra/              Ghidra exports (functions, callgraph, symbols, strings, types.h) + saved project (split tar)
  logs/                pipeline logs
records/             AUTHORED small facts
  names.tsv            identified functions: name, class, subsystem, method, role, evidence
  functions.tsv        status of each reconstructed function
  bugs.tsv             everything worth fixing, for the enhanced build
  import_names.tsv     NID -> library and name, read by the runtime build and the Ghidra pipeline
src/                 AUTHORED faithful reconstruction, mirroring the original source tree (see docs/source-tree.md)
native/              AUTHORED d1-native: runs the original executable on a PC (loader, PS4 system layer,
                     native Vulkan renderer). Reconstructed functions from src/ replace original ones here.
backend/             AUTHORED d1-backend: stands in for Bungie's servers
tests/               differential tests (once the first function is reconstructed)
docs/<topic>.md      method notes and findings. Topic names, never dates.
tools/               pipeline scripts (ghidra/, model.py, status.py, fetch_originals.py)
```

Folder and file names describe *what*, never *when*. Dates belong in commit messages.

Originals are never committed. `target.toml` lists each build with its R2 key and sha256, and
`tools/fetch_originals.py` verifies the hash. Helpers for an agent's own access (bucket readers, credentials)
stay out of the repo, and so do one-off probe scripts: a script lives in `tools/` only if the pipeline or CI
runs it.

## How this differs from the other decomp repos

- **No build with symbols exists.** There is no reference to match against. Names come from RTTI, source-path
  and assert strings, log channels, imports and behaviour, and every one is recorded in `records/names.tsv`
  with its evidence. Pseudocode is grouped by address block, not by source file.
- **The port runs the original code.** `native/` loads the PS4 executable and replaces the console's system
  software and GPU with PC code. It is the harness the decompilation is checked in: a reconstructed function
  is swapped in for the original one at its address and the game must behave the same.
- **`native/` is where PC changes live.** The renderer, input, settings and anything else a PC needs are
  there, not in `src/`.

## Faithful and enhanced

`src/` is the **faithful** decomp: original behaviour, bugs included, verified function by function. Never
fix a bug in `src/`. **Everything worth fixing goes in `records/bugs.tsv`**: columns id (`B` + 4 digits),
build, va, function, source, kind, priority, description, evidence, enhanced (`planned`, `fixed <patch id>`
or `keep`). Kinds: `security`, `crash`, `hang`, `logic`, `ub`, `robustness`, `limit`, `precision`,
`performance`, `quality`. `enhanced/` and `records/patches.tsv` are created with the first change to
reconstructed code.

## Addresses

Image-relative, 8 lowercase hex digits without `0x` (`003527d0`). The executable's first segment has virtual
address 0 and Ghidra loads it at image base 0, so the same number is `FUN_003527d0` in Ghidra and
`eboot.bin+0x3527d0` in the runtime, which maps the image at `0x800000000`. Every tool and table uses the
8-digit form; prose and C++ may write `0x3527D0`.

## Function status ladder

Every function has exactly one status:

`located -> named -> typed -> decompiled -> compiles -> matches -> integrated`

- `located`: entry and size known (analysis)
- `named`: identified. An import's name, or a row in `records/names.tsv` saying what the function is, how
  that was established and on what evidence. These are our names, not Bungie's, unless the evidence says
  the name is in the binary.
- `typed`: prototype known
- `decompiled`: authored source exists in `src/`
- `compiles`: that source builds in CI
- `matches`: passes an automated test against the original
- `integrated`: linked into the runtime in place of the original

`located` and `named` are computed; the rest come only from `records/functions.tsv`. `matches` needs a
passing automated test; CI fails if a record claims more than the tests show, names an address that is not a
function, or points at a source file that does not exist. `STATUS.md` is computed by `tools/status.py`.
Don't track progress anywhere else.

## Working rules

- Root-cause fixes only; no workarounds that hide a symptom.
- Verify with the game running and rendered frames, and say what was not tested. Report measured numbers.
- Keep `docs/` and the READMEs current with the code in the same change.
- The graph database (`uregraph`) is derived from `records/` and `analysis/`; no code text in the graph.
