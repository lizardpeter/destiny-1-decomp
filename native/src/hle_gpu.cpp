// Retail graphics/event ABI adapter. All rendering and GPU work are native Vulkan.
#include <time.h>
#include <x86intrin.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "hle.h"
#include "os.h"
#include "frame_telemetry.h"
#include "host_memory.h"
#include "runtime.h"
#include "settings.h"

namespace d1 {

// ------------------------------------------------------------------ kernel event queues
struct KEvent {
    uint64_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    uint64_t data;
    void* udata;
};
static_assert(sizeof(KEvent) == 32);

constexpr int16_t kFilterUser = -11, kFilterVideoOut = -13, kFilterGraphics = -14;

struct Equeue {
    std::mutex m;
    std::condition_variable cv;
    struct Entry { KEvent ev; bool triggered; };
    std::vector<Entry> events;
    std::string name;

    void add(const KEvent& e) {
        std::lock_guard lk(m);
        for (auto& x : events)
            if (x.ev.ident == e.ident && x.ev.filter == e.filter) { x.ev = e; return; }
        events.push_back({e, false});
    }
    bool trigger(uint64_t ident, int16_t filter, uint64_t data, bool display = false) {
        bool found = false;
        {
            std::lock_guard lk(m);
            for (auto& x : events)
                if (x.ev.ident == ident && x.ev.filter == filter) {
                    if (display) {
                        uint64_t counter = (x.ev.data >> 12) & 0xF;
                        if (counter != 0xF) ++counter;
                        x.ev.data = (__rdtsc() & 0xFFF) | (counter << 12) | (data & 0xFFFFFFFFFFFF0000ull);
                    } else {
                        x.ev.data = data;
                    }
                    x.triggered = true;
                    found = true;
                }
        }
        cv.notify_all();
        return found;
    }
};

static std::mutex g_eq_mutex;
static std::vector<Equeue*> g_equeues;

static int32_t sceKernelCreateEqueue(Equeue** out, const char* name) {
    auto* q = new Equeue;
    if (name) q->name = name;
    { std::lock_guard lk(g_eq_mutex); g_equeues.push_back(q); }
    *out = q;
    return SCE_OK;
}
HLE("sceKernelCreateEqueue", sceKernelCreateEqueue)
static int32_t sceKernelDeleteEqueue(Equeue* q) { return SCE_OK; }
HLE("sceKernelDeleteEqueue", sceKernelDeleteEqueue)

namespace render {
Perf& perf() {
    static Perf p;
    return p;
}
}  // namespace render
static thread_local bool t_flip_thread = false;  // the game's thread that submits frames
static uint64_t now_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}
static void note_guest_frame() {
    static std::atomic<uint64_t> last{0};
    t_flip_thread = true;
    const uint64_t t = now_ns(), prev = last.exchange(t);
    if (prev && t - prev < 2'000'000'000ull) {
        auto& p = render::perf();
        p.guest_ns += t - prev;
        ++p.guest_frames;
    }
}

static int32_t sceKernelWaitEqueue(Equeue* q, KEvent* ev, int num, int* out, uint32_t* timeout_us) {
    struct WaitTimer {
        uint64_t t0 = t_flip_thread ? now_ns() : 0;
        ~WaitTimer() { if (t0) render::perf().guest_wait_ns += now_ns() - t0; }
    } wait_timer;
    std::unique_lock lk(q->m);
    auto any = [&] { for (auto& x : q->events) if (x.triggered) return true; return false; };
    if (timeout_us) {
        if (!q->cv.wait_for(lk, std::chrono::microseconds(*timeout_us), any)) {
            if (out) *out = 0;
            return sce_err(60);
        }
    } else {
        q->cv.wait(lk, any);
    }
    int n = 0;
    for (auto& x : q->events)
        if (x.triggered && n < num) {
            ev[n++] = x.ev;
            x.triggered = false;
        }
    if (out) *out = n;
    return SCE_OK;
}
HLE("sceKernelWaitEqueue", sceKernelWaitEqueue)

static uint64_t sceKernelGetEventData(const KEvent* e) { return e->data; }
HLE("sceKernelGetEventData", sceKernelGetEventData)
static uint64_t sceKernelGetEventId(const KEvent* e) { return e->ident; }
HLE("sceKernelGetEventId", sceKernelGetEventId)
static int sceKernelGetEventFilter(const KEvent* e) { return e->filter; }
HLE("sceKernelGetEventFilter", sceKernelGetEventFilter)
static void* sceKernelGetEventUserData(const KEvent* e) { return e->udata; }
HLE("sceKernelGetEventUserData", sceKernelGetEventUserData)

static int32_t sceKernelAddUserEvent(Equeue* q, int id) {
    q->add({uint64_t(id), kFilterUser, 1, 0, 0, nullptr});
    return SCE_OK;
}
HLE("sceKernelAddUserEvent", sceKernelAddUserEvent)
HLE("sceKernelAddUserEventEdge", sceKernelAddUserEvent)
static int32_t sceKernelTriggerUserEvent(Equeue* q, int id, void* udata) {
    {
        std::lock_guard lk(q->m);
        for (auto& x : q->events)
            if (x.ev.ident == uint64_t(id) && x.ev.filter == kFilterUser) { x.ev.udata = udata; x.triggered = true; }
    }
    q->cv.notify_all();
    return SCE_OK;
}
HLE("sceKernelTriggerUserEvent", sceKernelTriggerUserEvent)
static int32_t sceKernelAddGpuExceptionEvent(Equeue* q, void* udata) {
    q->add({0x8000, kFilterGraphics, 1, 0, 0, udata});
    return SCE_OK;
}
HLE("sceKernelAddGpuExceptionEvent", sceKernelAddGpuExceptionEvent)

bool native_gfx_active();
bool native_gfx_on_fault(void* uc, void* addr);
void native_gfx_on_unmap(uint64_t addr, uint64_t size);
bool gpu_active() { return native_gfx_active(); }

void native_gfx_flip(void* addr, uint32_t width, uint32_t height);

// ------------------------------------------------------------------ video out
struct FlipStatus {
    uint64_t count, process_time, tsc;
    int64_t flip_arg;
    uint64_t submit_tsc, reserved0;
    int32_t gc_queue_num, flip_pending_num, current_buffer;
    uint32_t reserved1;
};
// Retail ABI layout; translated here, never exposed to the native frontend.
struct BufferAttribute {
    uint32_t pixel_format, tiling_mode;
    int32_t aspect_ratio;
    uint32_t width, height, pitch_in_pixel;
    uint32_t option, reserved0;
    uint64_t reserved1;
};
static_assert(sizeof(BufferAttribute) == 40);
static struct VideoOutPort {
    std::mutex m;
    FlipStatus status{0, 0, 0, -1, 0, 0, 0, 0, -1, 0};
    void* buffers[16] = {};
    BufferAttribute attr{};
    int registered = 0;
    alignas(64) uint64_t labels[16] = {};
    std::vector<Equeue*> flip_eqs, vblank_eqs;
    std::atomic<uint64_t> vblank_count{0};
    int prev_index = -1;
} g_vo;
static constexpr int32_t kVoHandle = 1;

static int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void*) { return kVoHandle; }
HLE("sceVideoOutOpen", sceVideoOutOpen)
static int32_t sceVideoOutClose(int32_t) { return SCE_OK; }
HLE("sceVideoOutClose", sceVideoOutClose)
static void sceVideoOutSetBufferAttribute(BufferAttribute* a, uint32_t fmt, uint32_t tiling, uint32_t aspect,
                                          uint32_t w, uint32_t h, uint32_t pitch) {
    *a = {fmt, tiling, int32_t(aspect), w, h, pitch, 0, 0, 0};
}
HLE("sceVideoOutSetBufferAttribute", sceVideoOutSetBufferAttribute)
static int32_t sceVideoOutRegisterBuffers(int32_t, int32_t start, void* const* addrs, int32_t num,
                                          const BufferAttribute* attr) {
    std::lock_guard lk(g_vo.m);
    for (int i = 0; i < num && start + i < 16; ++i) g_vo.buffers[start + i] = addrs[i];
    if (attr) g_vo.attr = *attr;
    g_vo.registered = std::max(g_vo.registered, start + num);
    log("video out: %d buffers registered, %ux%u pitch %u format %#x tiling %u", num, g_vo.attr.width,
        g_vo.attr.height, g_vo.attr.pitch_in_pixel, g_vo.attr.pixel_format, g_vo.attr.tiling_mode);
    return 0;
}
HLE("sceVideoOutRegisterBuffers", sceVideoOutRegisterBuffers)
static int32_t sceVideoOutSetFlipRate(int32_t, int32_t rate) {
    static int last = -1;
    if (rate != last) { log("video out: flip rate %d (%s)", rate, rate == 0 ? "60 Hz" : rate == 1 ? "30 Hz" : "20 Hz"); last = rate; }
    return SCE_OK;
}
HLE("sceVideoOutSetFlipRate", sceVideoOutSetFlipRate)
static int32_t sceVideoOutGetFlipStatus(int32_t, FlipStatus* s) {
    std::lock_guard lk(g_vo.m);
    *s = g_vo.status;
    return SCE_OK;
}
HLE("sceVideoOutGetFlipStatus", sceVideoOutGetFlipStatus)
static int32_t sceVideoOutIsFlipPending(int32_t) {
    std::lock_guard lk(g_vo.m);
    return g_vo.status.flip_pending_num;
}
HLE("sceVideoOutIsFlipPending", sceVideoOutIsFlipPending)
static int32_t sceVideoOutGetBufferLabelAddress(int32_t, uintptr_t* out) {
    *out = reinterpret_cast<uintptr_t>(g_vo.labels);
    return 16;
}
HLE("sceVideoOutGetBufferLabelAddress", sceVideoOutGetBufferLabelAddress)
static int32_t sceVideoOutAddFlipEvent(Equeue* q, int32_t, void* udata) {
    q->add({6, kFilterVideoOut, 1, 0, 0, udata});
    std::lock_guard lk(g_vo.m);
    g_vo.flip_eqs.push_back(q);
    return SCE_OK;
}
HLE("sceVideoOutAddFlipEvent", sceVideoOutAddFlipEvent)

static void vblank_thread() {
    using namespace std::chrono;
    auto next = steady_clock::now();
    static const int hz = [] { const char* v = getenv("D1_VBLANK_HZ"); return v ? std::max(1, atoi(v)) : 60; }();
    while (true) {
        next += microseconds(1000000 / hz);
        std::this_thread::sleep_until(next);
        uint64_t c = ++g_vo.vblank_count;
        std::vector<Equeue*> qs;
        { std::lock_guard lk(g_vo.m); qs = g_vo.vblank_eqs; }
        for (auto* q : qs) q->trigger(7, kFilterVideoOut, 7 | (c << 16), true);
    }
}
static int32_t sceVideoOutAddVblankEvent(Equeue* q, int32_t, void* udata) {
    q->add({7, kFilterVideoOut, 1, 0, 0, udata});
    static std::once_flag once;
    std::call_once(once, [] { std::thread(vblank_thread).detach(); });
    std::lock_guard lk(g_vo.m);
    g_vo.vblank_eqs.push_back(q);
    return SCE_OK;
}
HLE("sceVideoOutAddVblankEvent", sceVideoOutAddVblankEvent)

struct ResolutionStatus { int32_t full_w, full_h, pane_w, pane_h; uint64_t refresh; float screen_size; uint16_t flags; uint16_t reserved0; uint32_t reserved1[3]; };
static int32_t sceVideoOutGetResolutionStatus(int32_t, ResolutionStatus* r) {
    std::memset(r, 0, sizeof *r);
    r->full_w = r->pane_w = settings().render_width;
    r->full_h = r->pane_h = settings().render_height;
    r->refresh = 3;  // 59.94 Hz
    r->screen_size = 50.0f;
    return SCE_OK;
}
HLE("sceVideoOutGetResolutionStatus", sceVideoOutGetResolutionStatus)
static int32_t vo_noop() { return SCE_OK; }
HLE("sceVideoOutColorSettingsSetGamma_", vo_noop)
HLE("sceVideoOutAdjustColor_", vo_noop)

static std::atomic<uint64_t> g_frames{0};
uint64_t gpu_flip_count() { return g_frames.load(); }
static void gpu_stats(uint64_t frame);
static void do_flip(int32_t index, int64_t arg) {
    std::vector<Equeue*> qs;
    {
        std::lock_guard lk(g_vo.m);
        auto& s = g_vo.status;
        ++s.count;
        timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        s.process_time = uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
        s.tsc = __rdtsc();
        s.flip_arg = arg;
        s.current_buffer = index;
        if (s.flip_pending_num > 0) --s.flip_pending_num;
        if (s.gc_queue_num > 0) --s.gc_queue_num;
        if (g_vo.prev_index >= 0) g_vo.labels[g_vo.prev_index] = 0;
        g_vo.prev_index = index;
        qs = g_vo.flip_eqs;
    }
    for (auto* q : qs) q->trigger(6, kFilterVideoOut, 6 | (uint64_t(arg) << 16), true);
    uint64_t n = ++g_frames;
    if (n == 1 || n == 10 || n == 100 || n % 1000 == 0) log("flip #%llu (buffer %d, arg %lld)", n, index, arg);
    static const bool rate = getenv("D1_GPU_STATS") != nullptr;
    if (rate) {
        static auto t0 = std::chrono::steady_clock::now();
        static uint64_t n0 = 0;
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (s >= 5.0) {
            log("gpu stats: %.1f flips per second", double(n - n0) / s);
            t0 = std::chrono::steady_clock::now(), n0 = n;
        }
    }
    gpu_stats(n);
}
namespace gpuq {
bool enabled();
void push_flip(int32_t buf, int64_t arg);
}
static int32_t sceVideoOutSubmitFlip(int32_t, int32_t index, int32_t, int64_t arg) {
    note_guest_frame();
    if (gpuq::enabled()) {
        { std::lock_guard lk(g_vo.m); ++g_vo.status.flip_pending_num; }
        gpuq::push_flip(index, arg);
        return SCE_OK;
    }
    if (index >= 0 && index < 16) native_gfx_flip(g_vo.buffers[index], g_vo.attr.width, g_vo.attr.height);
    do_flip(index, arg);
    return SCE_OK;
}
HLE("sceVideoOutSubmitFlip", sceVideoOutSubmitFlip)

// ------------------------------------------------------------------ null GPU command processor
static std::mutex g_gnm_mutex;
static std::vector<Equeue*> g_eop_eqs;
void print_call_rates(uint64_t frames);
static void gpu_stats(uint64_t frame) {
    if (getenv("D1_GPU_STATS") && frame % 300 == 0) print_call_rates(300);
}

static void signal_eop() {
    std::vector<Equeue*> qs;
    { std::lock_guard lk(g_gnm_mutex); qs = g_eop_eqs; }
    for (auto* q : qs) q->trigger(0x40, kFilterGraphics, 0x40);
}

bool native_device_active();
struct NativeSubmission;
NativeSubmission* native_device_capture_submit(uint32_t count, uint32_t* const* dcbs, const uint32_t* sizes);
void native_device_execute_submit(NativeSubmission* batch);
void native_device_submit(uint32_t count, uint32_t* const* dcbs, const uint32_t* sizes);
void gnm_signal_eop() { signal_eop(); }
static void submit_now(uint32_t count, uint32_t* const* dcbs, const uint32_t* dcb_sizes) {
    if (!native_device_active()) {
        log("native GPU: submit attempted without an installed native device");
        std::abort();
    }
    native_device_submit(count, dcbs, dcb_sizes);
}

namespace gpuq {
struct Job {
    bool flip = false;
    NativeSubmission* native = nullptr; // consumed once by the queue thread
    int32_t buf = -1;
    int64_t arg = 0;
};
constexpr int kAheadFlips = 2;
std::mutex m;
std::condition_variable cv, cv_space;
std::vector<Job> queue;
size_t head = 0;
int queued_flips = 0;
bool busy = false;
std::atomic<uint64_t> wait_ns{0};

bool enabled() {
    static const bool on = [] { const char* v = getenv("D1_NG_ASYNC"); return !v || std::atoi(v) != 0; }();
    return on && native_gfx_active();
}
void run(Job& j) {
    if (j.native) {
        native_device_execute_submit(j.native);
        j.native = nullptr;
    }
    if (!j.flip) return;
    void* addr = nullptr;
    uint32_t width = 0, height = 0;
    {
        std::lock_guard lk(g_vo.m);
        if (j.buf >= 0 && j.buf < 16) addr = g_vo.buffers[j.buf];
        width = g_vo.attr.width;
        height = g_vo.attr.height;
    }
    if (addr) native_gfx_flip(addr, width, height);
    do_flip(j.buf, j.arg);
}
void worker() {
    os::set_thread_name("gpu queue");
    for (;;) {
        Job j;
        {
            std::unique_lock lk(m);
            cv.wait(lk, [] { return head < queue.size(); });
            j = std::move(queue[head++]);
            if (head == queue.size()) queue.clear(), head = 0;
            busy = true;
        }
        run(j);
        {
            std::lock_guard lk(m);
            busy = false;
            if (j.flip) --queued_flips;
        }
        cv_space.notify_all();
    }
}
void push(Job&& j) {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread(worker).detach();
        log("gpu queue: submissions and flips run on their own thread (D1_NG_ASYNC=0 turns it off)");
    });
    std::unique_lock lk(m);
    if (j.flip) {
        const auto t0 = std::chrono::steady_clock::now();
        cv_space.wait(lk, [] { return queued_flips < kAheadFlips; });
        wait_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
        ++queued_flips;
    }
    queue.push_back(std::move(j));
    lk.unlock();
    cv.notify_one();
}
void capture_submit(Job& j, uint32_t count, uint32_t* const* dcbs, const uint32_t* sizes) {
    if (!native_device_active()) {
        log("native GPU: queued submit attempted without an installed native device");
        std::abort();
    }
    j.native = native_device_capture_submit(count, dcbs, sizes);
}
void push_submit(uint32_t count, uint32_t* const* dcbs, const uint32_t* sizes) {
    Job j;
    capture_submit(j, count, dcbs, sizes);
    push(std::move(j));
}
void push_submit_and_flip(uint32_t count, uint32_t* const* dcbs, const uint32_t* sizes, int32_t buf, int64_t arg) {
    Job j;
    capture_submit(j, count, dcbs, sizes);
    j.flip = true, j.buf = buf, j.arg = arg;
    push(std::move(j));
}
void push_flip(int32_t buf, int64_t arg) {
    Job j;
    j.flip = true, j.buf = buf, j.arg = arg;
    push(std::move(j));
}
}  // namespace gpuq

uint64_t gpu_queue_wait_ns() { return gpuq::wait_ns.load(); }

static int32_t submit(uint32_t count, uint32_t* const* dcbs, const uint32_t* dcb_sizes) {
    if (gpuq::enabled()) gpuq::push_submit(count, dcbs, dcb_sizes);
    else submit_now(count, dcbs, dcb_sizes);
    return SCE_OK;
}

static int32_t sceGnmSubmitCommandBuffers(uint32_t count, uint32_t* const* dcbs, const uint32_t* dcb_sizes,
                                          uint32_t* const* ccbs, const uint32_t* ccb_sizes) {
    return submit(count, dcbs, dcb_sizes);
}
HLE("sceGnmSubmitCommandBuffers", sceGnmSubmitCommandBuffers)
static int32_t sceGnmSubmitAndFlipCommandBuffers(uint32_t count, uint32_t* const* dcbs, const uint32_t* dcb_sizes,
                                                 uint32_t* const* ccbs, const uint32_t* ccb_sizes, uint32_t,
                                                 uint32_t buf, uint32_t, int64_t arg) {
    note_guest_frame();
    if (gpuq::enabled()) {
        { std::lock_guard lk(g_vo.m); ++g_vo.status.flip_pending_num; ++g_vo.status.gc_queue_num; }
        gpuq::push_submit_and_flip(count, dcbs, dcb_sizes, int32_t(buf), arg);
        return SCE_OK;
    }
    submit(count, dcbs, dcb_sizes);
    if (buf < 16) native_gfx_flip(g_vo.buffers[buf], g_vo.attr.width, g_vo.attr.height);
    do_flip(int32_t(buf), arg);
    return SCE_OK;
}
HLE("sceGnmSubmitAndFlipCommandBuffers", sceGnmSubmitAndFlipCommandBuffers)
static int32_t sceGnmSubmitDone() {
    return SCE_OK;
}
HLE("sceGnmSubmitDone", sceGnmSubmitDone)
static int32_t sceGnmAddEqEvent(Equeue* q, uint64_t id, void* udata) {
    q->add({id, kFilterGraphics, 1, 0, 0, udata});
    if (id == 0x40) { std::lock_guard lk(g_gnm_mutex); g_eop_eqs.push_back(q); }
    return SCE_OK;
}
HLE("sceGnmAddEqEvent", sceGnmAddEqEvent)
static int32_t sceGnmIsUserPaEnabled() { return 0; }
HLE("sceGnmIsUserPaEnabled", sceGnmIsUserPaEnabled)

static void write_nop(uint32_t* p, uint32_t n) {
    if (!n) return;
    if (n == 1) { p[0] = 0x80000000u; return; }
    p[0] = 0xC0001000u | ((n - 2) << 16);
    std::memset(p + 1, 0, (n - 1) * 4);
}
static uint32_t sceGnmDrawInitDefaultHardwareState(uint32_t* cmd, uint32_t size) {
    if (size < 0x100) return 0;
    write_nop(cmd, 0x100);
    return 0x100;
}
HLE("sceGnmDrawInitDefaultHardwareState", sceGnmDrawInitDefaultHardwareState)
static uint32_t sceGnmDispatchInitDefaultHardwareState(uint32_t* cmd, uint32_t size) {
    if (size < 0x100) return 0;
    write_nop(cmd, 0x100);
    return 0x100;
}
HLE("sceGnmDispatchInitDefaultHardwareState", sceGnmDispatchInitDefaultHardwareState)

#define D1_SHADER_SETTER(NAME, ...)                                   \
    static int32_t NAME(uint32_t* cmd, uint32_t size, const uint32_t* regs __VA_OPT__(, ) __VA_ARGS__) { \
        if (!cmd) return -1;                                                \
        write_nop(cmd, size);                                               \
        return SCE_OK;                                                      \
    }
D1_SHADER_SETTER(sceGnmSetPsShader)
D1_SHADER_SETTER(sceGnmSetCsShader)
D1_SHADER_SETTER(sceGnmSetGsShader)
D1_SHADER_SETTER(sceGnmSetVsShader, uint32_t mod)
D1_SHADER_SETTER(sceGnmSetEsShader, uint32_t mod)
D1_SHADER_SETTER(sceGnmSetLsShader, uint32_t mod)
D1_SHADER_SETTER(sceGnmSetHsShader, uint32_t param)
#undef D1_SHADER_SETTER
HLE("sceGnmSetPsShader", sceGnmSetPsShader)
HLE("sceGnmSetVsShader", sceGnmSetVsShader)
HLE("sceGnmSetCsShader", sceGnmSetCsShader)
HLE("sceGnmSetEsShader", sceGnmSetEsShader)
HLE("sceGnmSetGsShader", sceGnmSetGsShader)
HLE("sceGnmSetHsShader", sceGnmSetHsShader)
HLE("sceGnmSetLsShader", sceGnmSetLsShader)
static int32_t sceGnmInsertWaitFlipDone(uint32_t* cmd, uint32_t size, int32_t, uint32_t buf) {
    write_nop(cmd, size);
    return SCE_OK;
}
HLE("sceGnmInsertWaitFlipDone", sceGnmInsertWaitFlipDone)

void gpu_on_map(uint64_t addr, uint64_t size) {
    native_gfx_on_unmap(addr, size);
}
void gpu_on_unmap(uint64_t addr, uint64_t size) {
    native_gfx_on_unmap(addr, size);
}
bool native_gfx_research_fault(void* uc, void* addr);
bool gpu_on_fault(void* uc, void* addr) {
    if (native_gfx_research_fault(uc, addr)) return true;
    if (native_gfx_on_fault(uc, addr)) return true;
    return false;
}

void print_gpu_summary() {
    log("native GPU: %llu presented frames", (unsigned long long)g_frames.load());
}

}  // namespace d1
