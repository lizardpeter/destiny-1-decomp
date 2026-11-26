# d1-native — Destiny 1 running natively on an x86-64 host

Transitional native PC port: the original x86-64 game machine code still runs directly
on the host. The PC executable requires its own Vulkan renderer; no external console
renderer or null-GPU interpreter is compiled in. The retail loader still needs the
original `eboot.bin`, `sce_module/libc.prx` and `sce_module/libSceFios2.prx`. Recovered
functions are being migrated into independently compiled native code. This is not yet
a source-complete standalone game. See `docs/native-platform-migration.md`.

No game files are in this repository. You need your own decrypted 01.33 eboot,
`sce_module/` and the `/app0` tree (packages etc.).

## Build and run

d1-native builds for **Windows** (x86-64, Windows 10 2004+ / 11) and **Linux** (x86-64).
Either way everything lives in the executable's folder:

```
d1-native(.exe)   the game runtime        d1-native.cfg   settings (written on first run)
d1-backend(.exe)  the replacement server  d1-native.log   log of the last run, backend.log
game/             the installed game      data/           everything the game writes
```

Put the installed 01.33 game (decrypted `eboot.bin`, `sce_module/`, `packages/`) in `game/`,
or point `game_dir` in `d1-native.cfg` at it, and start `d1-native`. It starts `d1-backend`
from the same folder by itself (`backend = local`). **The game folder is only read**: writes
under `/app0` are refused (EROFS, as on the console) and `data/` must be a separate, non-nested directory. All retail filesystem paths now pass through native asset/data containment and mount-boundary validation.

### Windows (cross-compiled from Linux with llvm-mingw)


```bash
cmake -S native -B build/win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/native/cmake/windows-llvm-mingw.cmake \
      -DLLVM_MINGW=/opt/llvm-mingw -DCMAKE_PREFIX_PATH=/opt/SDL2-2.32.10/x86_64-w64-mingw32 \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/win
```

It draws with `gfx/nrender.cpp` (see `docs/native-renderer-design.md`), presents to an
SDL2 window (`src/frontend_sdl.cpp`) and needs a Vulkan 1.4 driver.

```bash
# llvm-mingw: https://github.com/mstorsjo/llvm-mingw (ucrt build); SDL2 mingw devel package
# backend
CARGO_TARGET_X86_64_PC_WINDOWS_GNULLVM_LINKER=/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang \
RUSTFLAGS="-C target-feature=+crt-static" \
  cargo build --release --target x86_64-pc-windows-gnullvm --manifest-path backend/Cargo.toml
```

Both executables depend only on DLLs that ship with Windows (UCRT, Winsock, ...); SDL2
and the C++ runtime are linked in. Vulkan is loaded at run time from the GPU driver.

How the Windows port works (`src/os_win.cpp`, `src/os.h`):
- **Calling convention**: the game's code is System V (FreeBSD). Every host function it
  calls is registered through a System V entry point (`HLE()`/`D1_SYSV`), and every game
  function the host calls is typed `D1_SYSV`; the compiler converts.
- **Thread-local storage**: `mov r64, fs:[0]` (read the TCB) is rewritten in place to
  `mov r64, gs:[TEB TlsSlots + 8*slot]`, a `TlsAlloc` slot that holds the TCB.
- **Guest memory**: one placeholder reservation for the guest range (VirtualAlloc2 /
  MapViewOfFile3), so mappings land at exact addresses; direct memory is a pagefile-backed
  section mapped as aliasing views.
- **Faults**: a vectored exception handler serves the renderer's write tracking and crash
  reports. `scePthreadExit` returns to the thread's start without unwinding game frames.

### Linux

```bash
cmake -S native -B build/native && cmake --build build/native -j     # native Vulkan
cargo build --release --manifest-path backend/Cargo.toml
cp backend/target/release/d1-backend build/native/
build/native/d1-native            # or: --game /path/to/CUSA00219 --data ./data
```

### Settings

On first run d1-native writes `<data>/d1-native.cfg` (or the file given with `--config`)
with commented defaults. Environment variables override it:

| setting | env | values |
|---|---|---|
| `window_width`, `window_height` | `D1_WINDOW_SIZE=1920x1080` | window size in pixels (default 1280x720); the picture is scaled to fit, letterboxed |
| `fullscreen` | `D1_FULLSCREEN=1` | borderless fullscreen; F11 or Alt+Enter toggles while running |
| `overlay` | `D1_OVERLAY=1` | stats overlay (FPS, frame time, where a frame's time goes, render and window resolution, v-sync, GPU, game state); F3 toggles it while running. The same breakdown is logged every 10 s as a `perf:` line |
| `render_resolution` | `D1_RENDER_RESOLUTION=2560x1440` | the game's own internal resolution (1920x1080 on the console): display buffers and UI at this size; the 3D scene follows it up to 1920x1080 (above that the game crashes in a job at the title screen; `D1_SCENE_RESOLUTION=0` keeps the scene at 1920x1080) |
| `vsync` | `D1_VSYNC` | `off-tearing` (default: Immediate, lowest latency, may tear), `off` (Mailbox: newest frame at the next refresh, no tearing), `on` (Fifo) |
| `fps` | `D1_FPS` | frame-rate limit: 0 / `unlimited` runs with no limiter; a number caps it (30 = the console's rate). Either way every rendered frame runs one game tick of that frame's real length, so game time stays real time when the cap is not reached |
| `aim` | `D1_AIM=toggle` | aim down sights: `hold` (default) or `toggle` (press to aim, press again to stop) |
| `discord`, `discord_app_id` | `D1_DISCORD=0`, `D1_DISCORD_APP_ID` | Discord Rich Presence over Discord's local IPC socket; needs the Discord desktop app and an application id |

The game draws its render targets at 1080p whatever display resolution it is told about, so
the window size is the output resolution; changing the internal resolution needs an
engine-side patch (not done yet).

### Environment

| env | effect |
|---|---|
| `D1_TRACE=1` | log every firmware call with its first three arguments |
| `D1_TIMING_TRACE=1` | (research) in the world, log every frame's game time outputs (elapsed, tick dt, tick count) and the player update's dt / nominal / facing and camera kicks |
| `D1_TICK_NOMINAL30=1` | (research) keep the console's fixed 1/30 nominal tick instead of the frame's real tick |
| `D1_SEMA_BROADCAST=1` | (research) semaphore signals wake every waiter (the old behaviour) instead of only as many as the count allows |
| `D1_CALL_STATS=1` | route every implemented import through the logging entry (call counts at exit, the last calls in crash reports); off by default, implemented imports jump straight to their code |
| `D1_TRACE_CALLS=off[:n][/a[+bytes]],...` | (research) log the first n calls (default 20) of up to 16 eboot functions (hex offsets): six integer arguments, return value and caller; `/a` also dumps the memory argument a (0-5) points at before the call (default 0x80 bytes). Only for functions with at most six integer / pointer arguments |
| `D1_GAME_LOG=1` or `=<substring>` | re-enable the engine's own logging (stripped thresholds in release) and print it |
| `D1_FOV=<degrees>` (`fov` in `d1-native.cfg`) | field of view; 72 is the console's. Applied at the game camera before culling, so the wider view is filled in; aiming down sights zooms by the same ratio as before |
| `D1_AUTOPRESS=<s>` | tap Cross for 100 ms every `<s>` seconds (no host input yet) |
| `D1_WATCHDOG=<s>` | after `<s>` seconds dump every guest thread's call stack, call counts and GPU stats, then exit |
| `D1_ABORT_ON_UNIMPL=1` | stop at the first call to an unimplemented firmware function |
| `D1_BYPASS=prereq,transport` | research hooks: report failed bootflow prerequisites as satisfied; report the DemonWare transport as started |
| `D1_NET=link` | report a connected interface, give the game real host sockets and a signed-in PSN user; DNS is refused |
| `D1_BACKEND=<host>` | replacement backend server (implies `D1_NET=link`): Bungie, DemonWare and PSN hostnames resolve to it, all others are refused; HTTP(S) requests to those services go to it as plain HTTP on `D1_BACKEND_HTTP_PORT` (default 8080) with the original `Host` header |
| `D1_NET_ID=<n>` | run as console 10.0.2.`<n>` (4..254): sockets bind to 127.0.0.`<n>`, so several instances on one PC reach each other (co-op testing) |
| `D1_PEEK=off,*off` / `D1_POKE=secs:off+delta=value;…` | research aids: dump / write eboot-relative memory (`*` dereferences a pointer) |

Native renderer (`native/gfx`):

| env | effect |
|---|---|
| `D1_GPU=<n>` | use the n-th Vulkan device |
| Legacy frame skipping | Removed: all submitted frames execute on the native renderer; no fabricated occlusion results |
| `D1_NG_NOCACHE=1` | no geometry cache and no texture change tracking |
| Native device | Required; startup fails if the native renderer or required device hooks cannot be installed. The retail submit boundary associates engine record addresses with native lists, not packet tokens |
| `D1_PC_NATIVE_BIND=1` | Experimental source-defined bind/repoint for textures, vertex/index buffers, type 8 and data pointers; remaining types use original callbacks |
| `D1_PC_NATIVE_RING=1` | Experimental native ready-FIFO and source-defined main begin/worker reservation, requires native close and drain; original record layout mirrored for compatibility |
| `D1_PC_NATIVE_DRAIN=1` | Experimental source-defined record drain, requires `D1_PC_NATIVE_CLOSE=1`; direct native queue submission, retains original producer ring and progress labels |
| `D1_PC_NATIVE_CLOSE=1` | Experimental source-defined main/worker command-list close, without fabricated command-buffer extent. Keeps original ring, segment accounting and completion labels; not yet live-validated |
| Early recording | Retail device adapter now records only in actual submit order, including the original close-time completion labels. Parallel native secondary command recording remains enabled |
| `D1_ND_RES=0` | the engine's GPU resources (textures, vertex and index buffers) are tracked by write-protecting their memory again, instead of by the engine's own resource events (default: kept per resource and epoch, see docs/gpu-resources.md). The log's "engine resources" line counts uses, moves with the same contents and real changes |
| `D1_NG_PREFETCH=0` | engine resources are uploaded at their first draw instead of on the renderer's loader thread when the engine binds or moves them (default). The log's "loader" line counts uploads ahead of use, moves kept and work dropped because the resource changed again |
| `D1_NG_DYNAMIC_COPY=0` | memory that is no engine resource (dynamic vertex / index buffers, dynamic textures, small render targets the CPU rewrites) is write-tracked again instead of following the engine's own write announcements (texture unlocks, copy-queue writes); default: nothing is write-protected, 0 write faults |
| `D1_NG_VERIFY_DYN=1` / `D1_NG_LOG_FAULTS=1` / `D1_ND_LOG_UNLOCK=1` / `D1_ND_LOG_UNREG=1` | (testing) hash dynamic textures at every use and report changes without an event / write faults by writing code address / dynamic texture unlocks / vertex buffers that are no engine resource |
| `D1_NG_RECOPY=0` | (testing) dynamic vertices, indices and constants a recorded list copied are not copied again when the list is submitted |
| `D1_NG_CENSUS=<file>` | (research, with `D1_ND_RES=0 D1_NG_DYNAMIC_COPY=0`) every code address that writes GPU-read memory, with two callers and the range written (docs/gpu-memory-census.md) |
| `D1_NG_FEEDBACK_COPY=0` | (testing) draws that sample a render target they also render into read it in place, as before test-29 (undefined in Vulkan: garbage on most real GPUs) |
| `D1_NG_LOG_FEEDBACK=1` / `D1_NG_LOG_SNAPS=1` / `D1_NG_LOG_DMA_TEX=1` | (diagnostics) draws that sample their own render targets / the target copies made for them / copy-queue writes into texture memory |
| `D1_ND_RES_EVENTS=1` / `D1_ND_RES_STATS=1` / `D1_ND_POOL_WATCH=1` | (research) resource event counts with content checks / which handle pools draws use / who writes the handle-pool table |
| `D1_ND_LIST_STATS=1` | (diagnostics) draws and lists per device context per frame |
| `D1_ND_CB_CHECK=1` | (diagnostics) how many draws take constant buffers from bindings made in earlier lists |
| `D1_NG_RECORD_THREADS=<n>` | threads recording runs of draws in one render pass in parallel (secondary command buffers); default up to 6 (hardware threads - 4), 0 = one thread. `D1_NG_RECORD_RUN=<n>`: fewest draws split (default 64) |
| `D1_NG_GNM_DRAWS=1` | also run the engine's Gnm draw code (constant update engine, draw packets) for captured draws (default: capture only) |
| `D1_NG_ASYNC=0` | record and submit on the game's submitting thread (default: a separate GPU queue thread, at most 2 flips ahead) |
| `D1_NG_RING_SYSMEM=1` | keep the upload ring in system memory (default: device-local host-visible memory when the GPU offers it) |
| `D1_NG_PENDING_STATS=1` | log the pending draw / constant event lookups per submission |
| `D1_NG_FRAMES_IN_FLIGHT=<n>` | frames the CPU may record ahead of the GPU (default 2) |
| `D1_NG_GPU_TIMING=1` | optional GPU submission timestamp spans, collected after fences complete; logs summed/mean/max spans (not utilization, spans can overlap). |
| `D1_PC_LIBC_LEAVES=0` | diagnostic A/B only: disable native libc memory/string leaves and optional math wrappers; allocator/stdio/exception ABI still uses the original module |
| `D1_PC_LIBC_MATH=1` | experimental: 18 native scalar math/time-difference import replacements, in addition to 19 default memory/string leaves. Original floating-point behavior and Windows integration still need validation |
| `D1_PC_PTHREAD_KEYS=1` | experimental native guest key registry with a single host exit-notification key; guest destructor/cancellation behavior still needs live validation |
| `D1_NATIVE_FIOS=1` | Experimental: omit original libSceFios2.prx and require all 25 observed FIOS NIDs to resolve to native implementations. Original libc.prx and eboot.bin remain mandatory. Live game and Windows validation pending |
| `D1_FIOS_TRACE=1` | Bounded first 32 calls per FIOS import argument trace for either native or original provider |
| `D1_NATIVE_FRAME_TRACE=1` | optional scheduler instrumentation: observe published 0x591D40 frame records, including follow-up steps and aggregate timing channels; reports every 10 seconds. |
| `D1_NATIVE_FIXED_TICK=1` | experimental fixed-step path: preserve the original accumulator period and temporarily suppress the forced one-primary-step branch during timing calculation. Not the default; animation, physics, networking, and stage scheduling need live validation. |
| `D1_NG_READBACK=<pixels>` | largest never-sampled render target written back to guest memory for the CPU (default 4096, 0 off); `D1_NG_LOG_READBACK=1` lists the decisions |
| `D1_NG_STATS=1` | renderer statistics every 5 s instead of 15 s |
| `D1_NG_LOG_FAULTS=1` | the 64 KiB regions the game keeps rewriting (write-protection faults) |
| `D1_NG_FRAMES=<dir>` + `D1_NG_DUMP_EVERY=<n>` / `D1_NG_TARGETS=1` / `D1_NG_TARGETS_MAXPIX=<n>` | write presented frames (and the render targets a frame used) as PPM |
| `D1_NG_SNAP=<state prefix>:<s>` | `<s>` seconds after the game enters that state: draw every frame, log the next frame's passes and dump it; `D1_NG_SNAP_FRAMES=<n>` dumps n more frames, `D1_NG_SNAP_DEBUG=1` logs every draw (vertex buffers, constant buffers, textures with zero-block counts), `D1_NG_SNAP_RELOAD=1` copies every texture again first |
| `D1_NG_PASSLOG=<frame>\|world` / `D1_NG_PASSLOG_N=<n>` / `D1_NG_DEBUG=<frame>` | pass list / draw log of an executed frame |
| `D1_NG_TRACE=<base>,<x>,<y>,<w>,<h>` | in the snapshot frame, that rectangle of the render target at `<base>` after every draw into it, to `<frames dir>/trace.bin` |
| `D1_NG_ID_MAP=1` | G-buffer shaders that output black write their shader object's address bytes instead (who draws the black pixels) |
| `D1_NG_PROBE=<ps>:<v>,<v>,<v>,<v>` | that pixel shader's MRT0 gets those VGPRs as they are at the final export |
| `D1_NG_SKIP_PS=<ps>,..` / `D1_NG_GETLOD0=1` / `D1_NG_LANE_BRANCH=1` | experiments: leave pixel shaders out, IMAGE_GET_LOD returns 0, per-lane SCC/VCC branches |
| `D1_NG_DUMP_GCN=<dir>` / `D1_NG_DUMP_SPV=<dir>` / `D1_NG_DUMP_TEX=<dir>` | shader code for `d1-shadertool dis`, translated SPIR-V, texture memory (with its T#) |
| `D1_NG_LOG_DISPATCH=1` / `D1_NG_LOG_RELOADS=1` / `D1_NG_LOG_WAITS=1` | compute dispatches with their user data, small texture reloads, memory waits unsatisfied after a submit |
| `D1_VK_VALIDATION=1` | Khronos validation layer |

## How it works

| piece | file | notes |
|---|---|---|
| ELF-SCE loader | `src/loader.cpp` | maps modules at fixed bases (eboot `0x800000000`, prx `0x900000000+`) |
| linker | `src/runtime.cpp` | resolves imports to game-module exports, host functions (by NID), or logging stubs that return 0; applies RELATIVE / 64 / GLOB_DAT / JUMP_SLOT / DTPMOD64 relocations |
| TLS | `src/runtime.cpp` | PS4 code reads its TCB with `mov r64, fs:[0]` (2,542 sites); the loader rewrites the prefix to GS and each guest thread gets its own TCB with the static TLS block below it |
| guest frames | `src/runtime.cpp` | guest code walks frame-pointer chains (allocation call-stack capture); calls into the guest start from a terminal frame |
| kernel | `src/hle_kernel.cpp`, `src/hle_pthread.cpp` | memory (direct memory is a sparse memfd so aliases share pages), time, files (`/app0` → `--app0`), modules, threads, mutex/cond/rwlock, semaphores, event flags |
| GPU | `src/hle_gpu.cpp` | Retail event/flip ABI adapter; native Vulkan only. No PM4 interpreter or external renderer bridge. |
| audio | `src/hle_audio.cpp` | paced silent audio out; AJM codec jobs complete with silence |
| input / services / net | `src/hle_input.cpp`, `src/hle_services.cpp`, `src/hle_net.cpp` | one connected pad, user/system services, libSceLibcInternal subset, network reported as unavailable |
| game hooks | `src/hooks.cpp` | eboot-specific patches (01.33 offsets only) |

## Input

Keyboard and mouse play natively with PC key bindings (Destiny 2's PC defaults), and the
game's button prompts show the bound keys ("Hold [E]", "[Space] Continue") while you play
on keyboard and mouse; they switch back to controller buttons when a controller is used.
Clicking in the window captures the mouse (F2 or switching away releases it), and mouse
motion goes straight into the game's aim as raw counts, without stick emulation (see
"Mouse look" below).

| key | action (what the game sees) |
|---|---|
| W A S D | move (left stick) |
| mouse | look (raw, see below) |
| left / right mouse | fire (R2) / aim (L2) |
| Space | jump (Cross) |
| Left Ctrl | crouch (Circle) |
| Left Shift | sprint (L3) |
| R | reload (Square, tapped) |
| E | interact (Square, held) |
| C / Q / F | melee (R1) / grenade (L1) / super (L1+R1) |
| 1 / 2 / 3 | primary / special / heavy weapon (direct: d1-native taps or holds Triangle until the weapon is out) |
| mouse wheel, ` | switch weapon (Triangle tapped) |
| Tab / M | Ghost (touch pad held) / Director (touch pad tapped) |
| I | Character screen (Options) |
| Esc | menu (Options) |
| Backspace / Enter | back (Circle) / confirm (Cross) |
| arrows | emotes (D-pad) |

Every binding can be changed in `d1-native.cfg` (`bind_<action> = <key>, <key>`; the
commented list in the file has all actions, their defaults, and raw `pad_*` buttons;
`none` unbinds). `prompts = auto | keyboard | controller` picks the prompt style.

How it works: the game's text is built from localized string parts, and button glyphs are
private-use characters (U+E000..U+E021) in parts of their own, drawn by the UI font as
DualShock 4 buttons. d1-native reimplements the part decoder (eboot 0x8D5530) and swaps a
glyph part for the key bound to the matching action, growing the text run it decodes into
(docs/findings-log.md, 2026-10-17).

### Mouse look

The game has no mouse support; its look runs pad -> input abstraction (dead zones,
0x926910) -> player control (response curves, turn-speed acceleration, aim assist, zoom
scaling, 0x91B500), which ends with this frame's yaw and pitch deltas in radians.
d1-native hooks the end of that function and adds the mouse counts there, so the mouse
skips every stick stage (no quantization, curve, acceleration, dead zone or aim assist).
Counts come from SDL's relative mode on raw input with no OS pointer acceleration, and
reach the game at most about 1 ms after the window sees them. A controller keeps working
alongside. Settings in `d1-native.cfg`:

| setting | default | meaning |
|---|---|---|
| `sensitivity` | 1.5 | Destiny 2 PC scale (same as Modern Warfare 2019): 1.0 = 0.0066 degrees per count; 1.5 at 3200 DPI = 28.9 cm/360. Older configs' `mouse_sensitivity` (Source scale, 0.022) is converted |
| `mouse_invert` | false | inverted vertical look |
| `mouse_zoom_scale` | 1.0 | 1 = the game's own aim-down-sights slowdown, 0 = none |

Game controllers work through SDL's controller mapping (hot-plug supported). Any Vulkan 1.3 device works, including lavapipe.

The x86-64 host CPU must support AVX, BMI1, MOVBE and LZCNT (Haswell / Zen or
newer).

## Status

With `d1-backend` standing in for Bungie's servers, the game boots, signs in, creates or loads a
character, flies to orbit and loads activities (Tower, patrols, story, strikes), drawn by the native
Vulkan renderer. Current renderer work and measured numbers are in `docs/native-renderer-design.md`;
the history of how each stage was reached is in `docs/findings-log.md`.

Licence note: `native/` and `backend/` contain only new code.
