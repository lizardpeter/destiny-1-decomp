// Native renderer device layer for Destiny 1.
//
// The engine draws through its "GPU device" object (eboot 0x7D8000-0x7E4000) over the
// statically linked Gnm library; see docs/native-device-layer.md.
// Every Gnm / Gnmx function that would build PS4 command buffers is replaced so draws and
// resource events go straight into per-context command lists recorded on the engine's own
// threads and executed by our Vulkan renderer (gfx/nrender.cpp). No PM4, no emulated GPU.
#include <deque>
#include <map>
#include <memory>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "formats.h"
#include "nrender.h"
#include "settings.h"
#ifdef D1_FRONTEND_SDL
#include "frontend.h"
#endif
#include "hle.h"
#include "os.h"
#include "frame_telemetry.h"
#include "host_memory.h"
#include "native_asset_region.h"
#include "native_asset_suballocation.h"
#include "../gfx/native_resource_move.h"
#include "../gfx/native_buffer_binding.h"
#include "../gfx/native_pool_contract.h"
#include "../gfx/native_texture_constructor.h"
#include "../gfx/native_small_surface_layout.h"
#include "native_record_registry.h"
#include "../gfx/native_record_fifo.h"
#include "../gfx/native_cbm_contract.h"
#include "../gfx/native_cbm_pool.h"
#include "native_submission.h"
#include "../gfx/native_close_contract.h"
#include "../gfx/native_resource_identity.h"
#include "../gfx/native_texture_leases.h"
#include "../gfx/native_resource_binding.h"
#include "../gfx/native_blit_queue.h"
#include "runtime.h"
#include "tiling.h"

#ifndef _WIN32
#include <sys/syscall.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace d1 {

void* hook_eboot_function(uint64_t off, const void* host);  // hooks.cpp
bool replace_eboot_function(uint64_t off, const void* host);  // hooks.cpp
uint64_t gpu_flip_count();                                  // hle_gpu.cpp
uint64_t gpu_queue_wait_ns();                               // hle_gpu.cpp

namespace {

// ---- device object fields (eboot 01.33)
constexpr uint64_t kVsShader = 0x1D28, kPsShader = 0x1D30, kVertexDecl = 0x1D60;
constexpr uint64_t kBlendIndex = 0x25B4, kRasterIndex = 0x25BC;
constexpr uint64_t kColorTarget0 = 0x26E4;

template <class T>
T field(uint64_t dev, uint64_t off) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(dev + off), sizeof v);
    return v;
}

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

// ---- hooked device API (System V signatures as decompiled)
namespace nd {
D1_SYSV void* s_alloc(uint64_t cb,uint32_t bytes,uint32_t align_log2);
D1_SYSV void s_set_cbs(uint64_t cue,uint32_t stage,uint32_t first,uint32_t count,const uint32_t* vs);
}

bool nd_push(uint64_t dev, ng::Draw* d);
ng::TextureLeaseSet* nd_texture_leases(uint64_t dev);

namespace nr {

uint64_t g_base = 0;  // eboot base
template <class T>
T eb(uint64_t off) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(g_base + off), sizeof v);
    return v;
}

D1_SYSV void build_vsharp(uint32_t* out,uint64_t address,uint32_t format,uint32_t stride,uint32_t count){
    ng::vertex_words(out,address,format,stride,count);
}
D1_SYSV void build_csharp(uint32_t* out,uint64_t address,uint32_t bytes){
    ng::constant_words(out,address,bytes);
}
D1_SYSV void set_buffer_memory_type(uint32_t* out,uint32_t type){
    ng::buffer_memory_type(out,type);
}

struct DevState {
    ng::Draw::Cb cb[ng::kStages][16] = {};
    uint32_t vb_count[32] = {};
    uint32_t vb_handle[32] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    uint32_t vb_offset[32] = {};
    int32_t vb_raw_count[32] = {};
};

std::mutex g_m;
std::unordered_map<uint64_t, DevState*> g_devs;
int g_dump_every = 0;       // write every Nth executed frame (0: none)
uint64_t g_executed_frames = 0;
bool g_frame_executed = false;
uint64_t g_exec_ns = 0, g_flip_ns = 0;

struct CbEvent {
    uint64_t order;
    uint32_t seq;
    uint8_t stage, slot;
    ng::Draw::Cb cb;
};
ng::Draw::Cb g_ce[ng::kStages][16] = {};  // bindings as of the end of the last submission
bool g_ce_known[ng::kStages][16] = {}; // distinguish explicit unbind from no inherited state
struct CueState {
    ng::Draw::Cb cb[ng::kStages][16] = {};
};
std::unordered_map<uint64_t, CueState*> g_cues;
CueState* cue_state(uint64_t cue, bool create) {
    thread_local uint64_t last_cue = 0;
    thread_local CueState* last = nullptr;
    if (cue == last_cue && last) return last;
    std::lock_guard lk(g_m);
    auto it = g_cues.find(cue);
    CueState* c = it != g_cues.end() ? it->second : nullptr;
    if (!c && create) g_cues[cue] = c = new CueState;
    if (c) last_cue = cue, last = c;
    return c;
}

DevState* dev_state(uint64_t dev) {
    thread_local uint64_t last_dev = 0;
    thread_local DevState* last = nullptr;
    if (dev == last_dev) return last;
    std::lock_guard lk(g_m);
    auto& d = g_devs[dev];
    if (!d) d = new DevState;
    last_dev = dev, last = d;
    return d;
}

// engine resource handle -> buffer object (pool table at *0x1A238A0)
uint64_t resolve_handle(uint32_t h) {
    const uint64_t tbl = *reinterpret_cast<const uint64_t*>(eb<uint64_t>(0x1A238A0));
    const int32_t hs = int32_t(h) >> 13;
    const uint64_t pool = tbl + uint64_t(((uint32_t(hs) >> 18) | 0x3FF) & uint32_t(hs)) * 0x40;
    const uint64_t pbase = field<uint64_t>(pool, 0x10);
    const uint64_t off = uint64_t(h & 0x1FFF) * field<uint32_t>(pool, 0x30);
    const uint64_t mask = uint64_t(int64_t(field<int32_t>(pool, 0x34)));
    return (pbase + off) - (mask & field<uint64_t>(pbase + off, 8));
}
uint64_t resolve_handle_safe(uint32_t h) {
    const uint64_t tbl = *reinterpret_cast<const uint64_t*>(eb<uint64_t>(0x1A238A0));
    const int32_t hs = int32_t(h) >> 13;
    const uint64_t pool = tbl + uint64_t(((uint32_t(hs) >> 18) | 0x3FF) & uint32_t(hs)) * 0x40;
    const uint64_t pbase = field<uint64_t>(pool, 0x10);
    const uint32_t esz = field<uint32_t>(pool, 0x30);
    const uint64_t pend=field<uint64_t>(pool,0x20);
    const uint64_t off_check=uint64_t(h&0x1FFF)*esz;
    if (!pbase || esz<16 || pend<pbase || off_check>pend-pbase || pend-pbase-off_check<16) return 0;
    const uint64_t off = uint64_t(h & 0x1FFF) * esz;
    const uint64_t mask = uint64_t(int64_t(field<int32_t>(pool, 0x34)));
    return (pbase + off) - (mask & field<uint64_t>(pbase + off, 8));
}

int gnm_stage_index(uint32_t gnm_stage) { return gnm_stage == 1 ? 1 : gnm_stage == 2 ? 0 : -1; }

uint32_t vertex_count(uint32_t prim, uint32_t n) {
    switch (prim) {
    case 0: return n;
    case 1: return n * 2;
    case 2: return n + 1;
    case 3: case 6: return n * 3;
    case 4: case 5: return n + 2;
    case 7: return n * 4;
    default: return 0;
    }
}

// ---------------------------------------------------------------------------- resource registry
// The engine's GPU resources as the engine itself manages them (docs/gpu-resources.md):
// every bind / re-point / release / move / mip change of a texture (types 1-3: the object is
// the T#), vertex buffer (4), index buffer (6), pixel shader (8) or vertex shader (9) bumps
// the object's epoch and triggers background GPU preparation.
namespace resreg {
ng::ResourceRegistry<>* g_registry = nullptr;
std::atomic<uint64_t> g_events{0};
bool g_on = false;
inline uint64_t get(uint64_t key) {
    if (!g_registry || key < 0x10000) return 0;
    const auto snapshot = g_registry->get(key);
    if (!snapshot.identity) return 0;
    if (snapshot.handle != UINT32_MAX && resolve_handle_safe(snapshot.handle) != key) {
        static std::atomic<uint64_t> n{0};
        const uint64_t c = ++n;
        if ((c & (c - 1)) == 0 && c <= (1u << 20))
            log("nd: stale resource registry entries ignored: %llu (object %#llx, handle %#x)",
                (unsigned long long)c, (unsigned long long)key, snapshot.handle);
        return 0;
    }
    return g_registry->current(snapshot) ? snapshot.identity : 0;
}
uint64_t touch(uint64_t key, bool insert, uint32_t handle = UINT32_MAX) {
    uint64_t published=0;
    if (g_registry && key >= 0x10000 && g_registry->touch(key, insert, handle,&published)) ++g_events;
    return published;
}
// the resource object a (type, handle) names, 0 for the types the renderer does not track
bool tracked_type(uint32_t type) {
    return (type>=1&&type<=4)||type==6||type==8||type==9||type==12||type==13;
}
uint64_t object_of(uint32_t type, uint32_t handle) {
    if (handle == 0xFFFFFFFFu || !tracked_type(type)) return 0;
    const uint64_t o = resolve_handle_safe(handle);
    return o >= 0x10000 ? o : 0;
}
// the renderer prepares the resource's GPU copy now (its loader thread)
void prefetch(uint32_t type, uint64_t o) {
    if (!o || !ng::active()) return;
    const uint64_t res = get(o);
    if (!res) return;
    // 0x7D4B90 (type 8) binds a texture with an 8-byte tag header,
    // not a shader. Its descriptor layout is different from types 1-3.
    // Do not parse it as shader bytecode or feed it to the type-1 mip path.
    if (type == 8) return;
    if (type == 9 || type == 12 || type == 13) {
        ng::prefetch_shader(reinterpret_cast<const uint8_t*>(o), type == 9, res, o);
        return;
    }
    if (type >= 1 && type <= 3) {
        ng::native_texture_event(o, res, false);
    } else if(type==4||type==6) {
        const auto b=type==4?ng::binding::vertex_source(reinterpret_cast<const void*>(o)):
                             ng::binding::index_source(reinterpret_cast<const void*>(o));
        ng::native_buffer_event(o,res,b.base,b.size,type==6);
    }
}
void event(uint32_t type, uint32_t handle, bool insert) {
    if (const uint64_t o = object_of(type, handle)) {
        touch(o, insert, handle);
        if (insert) prefetch(type, o);
    }
}
void event_any(uint32_t handle) {
    if (handle == 0xFFFFFFFFu) return;
    if (const uint64_t o = resolve_handle_safe(handle)) touch(o, false, handle);
}
void init() {
    if (g_registry) return;
    g_registry = new ng::ResourceRegistry<>;
    g_on = true;
}
}  // namespace resreg

ng::Draw* alloc_draw();

thread_local uint64_t t_capture_dev = 0;
ng::Draw* capture(uint64_t dev,uint32_t engine_prim,uint64_t caller=0,bool capture_vertices=true,bool capture_material=true) {
    t_capture_dev = dev;
    auto* d = alloc_draw();
    d->caller = caller ? caller - g_base : 0;
    d->prim = uint8_t(eb<uint32_t>(0x15D13C0 + uint64_t(engine_prim) * 4));
    d->vs = reinterpret_cast<const uint8_t*>(field<uint64_t>(dev, kVsShader));
    d->ps = reinterpret_cast<const uint8_t*>(field<uint64_t>(dev, kPsShader));
    const uint64_t ctx = field<uint64_t>(dev, 0x50);
    d->order = ctx ? field<uint64_t>(ctx, 0x10) : 0;
    if(capture_material){
    DevState* ds = dev_state(dev);
    CueState* cs = ctx ? cue_state(ctx + 0xC0, false) : nullptr;
    {
    // The pool retirement gate covers both static geometry and texture
    // capture; capture cannot publish a lease after its epoch is retired.
    auto gate=ng::texture_leases().capture();
    uint64_t vbsig = 0;
    if(capture_vertices)if (const uint64_t decl = field<uint64_t>(dev, kVertexDecl)) {
        const uint32_t n = std::min(field<uint32_t>(decl, 0), 16u);
        d->num_vb = n;
        // A vertex declaration describes attributes, not independent resources.
        // Resolve each bound stream exactly once; all attributes in that stream
        // consume the same native allocation identity, offset and vertex count.
        struct StreamBinding {
            uint64_t base,resource_base,resource_size,identity,object;
            uint32_t count;
            bool valid;
        };
        StreamBinding streams[32];
        uint32_t prepared=0;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t fmt = field<uint32_t>(decl, 0x14 + 8 * i);
            const uint32_t off = field<uint8_t>(decl, 0x18 + 8 * i), stream = field<uint8_t>(decl, 0x19 + 8 * i) & 31;
            const uint32_t stride = field<uint32_t>(decl, 4 + 4 * stream);
            if (!stride) continue;
            auto& binding=streams[stream];
            const uint32_t bit=1u<<stream;
            if (!(prepared&bit)) {
                prepared|=bit;
                binding={};
                uint32_t h=field<uint32_t>(dev,0x1D78+4*stream);
                uint32_t stream_off=field<uint32_t>(dev,0x1DF8+4*stream);
                uint32_t vcnt=ds->vb_count[stream];
                uint64_t obj=0;
                // A declaration change clears the device's cached handles,
                // but not the bound streams. Preserve the original fallback.
                if(h==0xFFFFFFFFu&&ds->vb_handle[stream]!=0xFFFFFFFFu){
                    h=ds->vb_handle[stream];
                    stream_off=ds->vb_offset[stream];
                    obj=resolve_handle_safe(h);
                    if(obj>=0x10000){
                        uint32_t tot=field<uint32_t>(obj,0x30);
                        vcnt=ng::vertex_records(tot,stream_off,stride,ds->vb_raw_count[stream]);
                    }
                }else if(h!=0xFFFFFFFFu) obj=resolve_handle_safe(h);
                if(obj>=0x10000){
                    binding.resource_base=field<uint64_t>(obj,0x28);
                    binding.base=binding.resource_base+stream_off;
                    binding.count=vcnt;
                    if(resreg::g_on){
                        binding.identity=resreg::get(obj);
                        binding.resource_size=field<uint32_t>(obj,0);
                        binding.object=obj;
                    }
                    binding.valid=true;
                }
            }
            if(!binding.valid)continue;
            build_vsharp(d->vb[i],binding.base+off,fmt,stride,binding.count);
            vbsig = mix(vbsig, uint64_t(i) << 48 | uint64_t(d->vb[i][1] >> 16) | uint64_t(d->vb[i][3]) << 32);
            if(resreg::g_on){
                d->vres[i]={binding.identity,binding.resource_base,binding.resource_size,binding.object};
            }
        }
    }
    d->vb_sig = vbsig;
    for (int s = 0; s < 2; ++s) {
        const uint32_t gnm = s ? 1 : 2;
        for (int i = 0; i < 16; ++i) {
            if (const uint64_t t = field<uint64_t>(dev, 0x1E78 + gnm * 0x80 + i * 8); t >= 0x10000) {
                const auto before=resreg::g_on?resreg::get(t):0;
                std::memcpy(d->tex[s][i], reinterpret_cast<const void*>(t), 32);
                const auto after=resreg::g_on?resreg::get(t):0;
                // Do not relabel captured words with a different generation.
                // A concurrent transition must not turn into an unregistered read.
                if(before==after){
                    d->tex_mask[s] |= uint16_t(1u << i);
                    d->tex_res[s][i]=after;
                    d->tex_obj[s][i]=t;
                }
            }
            if (const uint64_t p = field<uint64_t>(dev, (s ? 0x2278 : 0x22F8) + i * 8); p >= 0x10000) {
                std::memcpy(d->smp[s][i], reinterpret_cast<const void*>(p), 16);
                d->smp_mask[s] |= uint16_t(1u << i);
            }
            d->cb[s][i] = cs ? cs->cb[s][i] : ds->cb[s][i];
        }
    }
    }
    }
    d->clip = field<uint32_t>(dev, 0x25D0);
    d->su_sc_mode = field<uint32_t>(dev, 0x25D4);
    d->blend = field<uint32_t>(dev, 0x25D8);
    d->depth_ctl = field<uint32_t>(dev, 0x25DC);
    d->stencil_ctl = field<uint32_t>(dev, 0x25E0);
    d->stencil_ref = field<uint32_t>(dev, 0x25E4);
    d->target_mask = eb<uint32_t>(0x15D16E0 + uint64_t(field<uint32_t>(dev, kBlendIndex)) * 0x24);
    const uint64_t rast = 0x15D2424 + uint64_t(field<uint32_t>(dev, kRasterIndex)) * 0xC;
    d->poly_offset = eb<float>(rast) * 2.0f;
    d->poly_scale = eb<float>(rast + 4) * eb<float>(0x165FE78);
    for (int i = 0; i < 4; ++i) d->vp[i] = field<int32_t>(dev, 0x2578 + 4 * i);
    d->znear = field<float>(dev, 0x2588), d->zfar = field<float>(dev, 0x258C), d->zk = eb<float>(0x165FEA0);
    for (int i = 0; i < 5; ++i) std::memcpy(d->color[i], reinterpret_cast<const void*>(dev + kColorTarget0 + 0x2C * i), 44);
    std::memcpy(d->depth, reinterpret_cast<const void*>(dev + 0x26B0), 52);
    return d;
}

std::mutex g_free_m;
std::vector<ng::Draw*> g_free;
ng::Draw* alloc_draw() {
    thread_local std::vector<ng::Draw*> cache;
    if (cache.empty()) {
        std::lock_guard lk(g_free_m);
        const size_t n = std::min<size_t>(g_free.size(), 128);
        cache.assign(g_free.end() - n, g_free.end());
        g_free.resize(g_free.size() - n);
    }
    if (cache.empty()) return new ng::Draw;
    ng::Draw* d = cache.back();
    cache.pop_back();
    d->kind = ng::Draw::Auto, d->prim = 4, d->index32 = 0, d->clear_flags = 0;
    d->count = 0, d->instances = 1, d->index_addr = 0, d->order = 0, d->seq = 0, d->frame = 0, d->caller = 0;
    d->vs = d->ps = nullptr, d->num_vb = 0;
    d->dma_src_spans.clear();d->dma_dst_spans.clear();d->dma_checked=false;
    d->shader_res[0] = d->shader_res[1] = 0;
    std::memset(d->vb, 0, sizeof d->vb);
    for (auto& r : d->vres) r = {};
    d->ires = {};
    d->native_geo_mask=0;
    d->tex_mask[0] = d->tex_mask[1] = d->smp_mask[0] = d->smp_mask[1] = 0;
    d->tex_filter_mask[0] = d->tex_filter_mask[1] = 0;
    d->native_tex_ready[0]=d->native_tex_ready[1]=0;
    d->material_ready=false;
    d->geo_captured=0;
    d->tex_sig[0] = d->tex_sig[1] = d->vb_sig = d->pipe_base_key = 0;
    d->samples_target = false;
    std::memset(d->tex_res, 0, sizeof d->tex_res);
    std::memset(d->tex_obj, 0, sizeof d->tex_obj);
    std::memset(d->tex_view, 0, sizeof d->tex_view);
    std::memset(d->tex_dk, 0, sizeof d->tex_dk);
    std::memset(d->smp_lin, 0, sizeof d->smp_lin);
    std::memset(d->smp_near, 0, sizeof d->smp_near);
    d->clear_color[0] = d->clear_color[1] = d->clear_color[2] = d->clear_color[3] = 0;
    d->clear_depth = 1, d->clear_stencil = 0;
    d->inline_vertices.clear();
    return d;
}
void release_draws(std::vector<ng::Draw*>& ds) {
    std::lock_guard lk(g_free_m);
    for (ng::Draw* d : ds) {
        if (g_free.size() >= 65536) delete d;
        else {
            if (d->inline_vertices.capacity() > 65536) d->inline_vertices = {};
            g_free.push_back(d);
        }
    }
    ds.clear();
}
void release_draw(ng::Draw* d) {
    std::vector<ng::Draw*> v{d};
    release_draws(v);
}

void push(ng::Draw* d) {
    ng::resolve_material(d);
    if (nd_push(t_capture_dev, d)) return;
    release_draw(d);
}

}  // namespace nr

using ClearFn = D1_SYSV void (*)(uint64_t, uint32_t);
using DispatchFn = D1_SYSV void (*)(uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, int32_t);
ClearFn o_clear;
DispatchFn o_dispatch;

// 0x7E16A0 sets an instance count, calls 0x7E15D0 and restores one.
// Both original paths now generate the same native record directly,
// without a console draw trampoline or thread-local instance override.
void capture_indexed(uint64_t dev,uint32_t prim,int32_t first,uint32_t count,uint32_t instances,uint64_t caller){
    if (field<uint8_t>(dev, 0x26)) {
        ng::Draw* d = nr::capture(dev,prim,caller);
        d->kind = ng::Draw::Indexed;
        {
        const uint64_t ib = field<uint64_t>(dev, 0x1D70);
        const uint32_t isz = ib ? field<uint32_t>(ib, 0x38) : 0;
        d->index32 = isz != 0;
        d->index_addr = ib ? field<uint64_t>(ib, 0x30) + uint64_t(uint32_t((isz * 2 + 2) * first)) : 0;
        if(ib&&nr::resreg::g_on){
            d->ires={nr::resreg::get(ib),field<uint64_t>(ib,0x30),uint64_t(field<uint32_t>(ib,0x3c))*(isz?4:2),ib};
            bool cpu_required=d->prim==0x13;
            for(uint32_t i=0;i<d->num_vb&&!cpu_required;++i){
                const auto* words=d->vb[i];
                const uint64_t base=uint64_t(words[0])|(uint64_t(words[1]&0xfff)<<32);
                const auto& r=d->vres[i];
                if((words[1]>>16&0x3fff)&&(!r.id||base<r.base||base-r.base>=r.size))
                    cpu_required=true;
            }
        }
        }
        d->count = count;
        d->instances=instances;
        nr::push(d);
    }
}
D1_SYSV void r_draw_indexed(uint64_t dev,uint64_t prim,uint64_t,int32_t first,uint32_t count){
    capture_indexed(dev,uint32_t(prim),first,count,1,uint64_t(__builtin_return_address(0)));
}
D1_SYSV void r_draw(uint64_t dev, uint64_t prim, uint32_t count) {
    if (field<uint8_t>(dev, 0x26)) {
        ng::Draw* d = nr::capture(dev, uint32_t(prim), uint64_t(__builtin_return_address(0)));
        d->kind = ng::Draw::Auto;
        d->count = count;
        nr::push(d);
    }
}
D1_SYSV void r_draw_instanced(uint64_t dev, uint32_t prim, uint32_t count, uint64_t, uint32_t instances) {
    if (field<uint8_t>(dev, 0x26)) {
        ng::Draw* d = nr::capture(dev, prim, uint64_t(__builtin_return_address(0)));
        d->kind = ng::Draw::Auto;
        d->count = count, d->instances = instances;
        nr::push(d);
    }
}
D1_SYSV void r_draw_indexed_instanced(uint64_t dev,uint32_t prim,uint32_t count,uint64_t,uint32_t instances,
                                     uint64_t,uint32_t first){
    capture_indexed(dev,prim,int32_t(first),count,instances?instances:1,uint64_t(__builtin_return_address(0)));
}
D1_SYSV void r_draw_quads(uint64_t dev, uint32_t prim, uint64_t first, int32_t prims) {
    ng::Draw* d = nr::capture(dev, prim, uint64_t(__builtin_return_address(0)));
    d->kind = ng::Draw::Indexed;
    d->index_addr = field<uint64_t>(field<uint64_t>(dev, 0), 0) + (first & 0xFFFFFFFF) * 2;
    d->count = nr::vertex_count(prim, uint32_t(prims));
    nr::push(d);
}
D1_SYSV void r_draw_inline(uint64_t dev, uint32_t prim, int32_t prims, const void* data, int32_t stride) {
    const uint64_t decl = field<uint64_t>(dev, kVertexDecl);
    if (decl && data) {
        ng::Draw* d = nr::capture(dev,prim,uint64_t(__builtin_return_address(0)),false);
        d->kind = ng::Draw::Auto;
        d->count = nr::vertex_count(prim, uint32_t(prims));
        d->inline_vertices.assign(static_cast<const uint8_t*>(data),
                                  static_cast<const uint8_t*>(data) + size_t(d->count) * uint32_t(stride));
        std::memset(d->vb,0,sizeof d->vb);
        // Inline offsets refer to the draw-owned vector, not the static
        // resource captured from the previous declaration.
        for(auto& resource:d->vres)resource={};
        const uint32_t n = std::min(field<uint32_t>(decl, 0), 16u);
        d->num_vb = n;
        uint64_t vbsig = 0;
        for (uint32_t i = 0; i < n; ++i) {
            nr::build_vsharp(d->vb[i], field<uint8_t>(decl, 0x18 + 8 * i),
                             field<uint32_t>(decl, 0x14 + 8 * i), uint32_t(stride), d->count);
            vbsig = mix(vbsig, uint64_t(i) << 48 | uint64_t(d->vb[i][1] >> 16) | uint64_t(d->vb[i][3]) << 32);
        }
        d->vb_sig = vbsig;
        nr::push(d);
    }
}
D1_SYSV void r_clear(uint64_t dev, uint32_t flags) {
    if (flags & 0xFC) {
        // Native Vulkan attachment clears do not consume a material or
        // vertex allocation. Do not resolve or pin any of those resources.
        ng::Draw* d = nr::capture(dev,0,0,false,false);
        d->kind = ng::Draw::Clear;
        d->clear_flags = uint8_t(flags);
        std::memcpy(d->clear_color, reinterpret_cast<const void*>(dev + 0x40), 16);
        const float z = field<float>(dev, 0x20);
        d->clear_depth = field<uint8_t>(dev, 0x24) ? z : nr::eb<float>(0x165FEB4) - z;
        d->clear_stencil = field<uint8_t>(dev, 0x25);
        d->stencil_ref = (d->stencil_ref & 0xFF00FFFFu) | uint32_t(field<uint8_t>(dev, 0x25EC)) << 16;
        nr::push(d);
    }
    o_clear(dev, flags);
}
D1_SYSV uint64_t r_alloc_constants(uint64_t dev, uint32_t stage, uint32_t slot, uint32_t bytes) {
    // The original body only allocates aligned storage, constructs a
    // constant descriptor and calls the already-native binding entry.
    const uint64_t ctx=field<uint64_t>(dev,0x50);
    const uint64_t p=uint64_t(nd::s_alloc(ctx,bytes,4));
    if(!p){log("nd: native constant allocation failed (%u bytes)",bytes);std::abort();}
    uint32_t words[4];
    ng::constant_words(words,p,bytes);
    ng::buffer_memory_type(words,0x10);
    nd::s_set_cbs(ctx+0xc0,stage,slot,1,words);
    const int s = nr::gnm_stage_index(stage);
    if (s >= 0 && slot < 16) nr::dev_state(dev)->cb[s][slot] = {p, bytes};
    return p;
}
// 0x7DEC20: Gnm V# writes were already no-ops. Publish the actual
// native stream state and mirror only the engine's binding cache fields.
D1_SYSV void r_set_vertex_buffer(uint64_t dev,uint32_t handle,uint32_t stream,uint32_t offset,int32_t count){
    const uint32_t s=uint16_t(stream);
    if(handle==UINT32_MAX||s>=32)return;
    auto* ds=nr::dev_state(dev);
    ds->vb_handle[s]=handle;
    ds->vb_offset[s]=offset;
    ds->vb_raw_count[s]=count;
    const auto decl=field<uint64_t>(dev,kVertexDecl);
    if(!decl)return;
    const auto stride=field<uint32_t>(decl,4+4*s);
    if(!stride)return;
    const auto obj=nr::resolve_handle_safe(handle);
    if(obj<0x10000)return;
    ds->vb_count[s]=ng::vertex_records(field<uint32_t>(obj,0x30),offset,stride,count);
    if(!ng::vertex_bind_changed(field<uint32_t>(dev,0x1d78+4*s),field<uint32_t>(dev,0x1df8+4*s),handle,offset))return;
    std::memcpy(reinterpret_cast<void*>(dev+0x1d78+4*s),&handle,4);
    std::memcpy(reinterpret_cast<void*>(dev+0x1df8+4*s),&offset,4);
}
// 0x7DEE30: the original packet setters are all replaced. Preserve
// the handle and resolved index object used by direct native draws.
D1_SYSV void r_set_index_buffer(uint64_t dev,uint32_t handle){
    if(field<uint32_t>(dev,0x1d68)==handle)return;
    const auto object=handle==UINT32_MAX?uint64_t(0):nr::resolve_handle_safe(handle);
    std::memcpy(reinterpret_cast<void*>(dev+0x1d68),&handle,4);
    std::memcpy(reinterpret_cast<void*>(dev+0x1d70),&object,8);
}
D1_SYSV void r_dispatch(uint64_t dev, uint64_t cs, uint32_t x, uint32_t y, uint32_t z, int32_t flag) {
    o_dispatch(dev, cs, x, y, z, flag);
}

template <class Fn>
bool hook(uint64_t off, Fn host, Fn& orig, const char* name) {
    void* t = hook_eboot_function(off, reinterpret_cast<const void*>(host));
    if (!t) {
        log("native gfx: hooking %s (eboot+%#llx) failed", name, (unsigned long long)off);
        return false;
    }
    orig = reinterpret_cast<Fn>(t);
    return true;
}

// Engine GPU-class backing (0xB4780 reserve / 0xB4630 commit).
// Keep the 0x30-byte retail descriptor and higher-level suballocation intact.
namespace asset_backing {
using ReserveFn=D1_SYSV uint64_t(*)(uint32_t,uint64_t,uint64_t,uint64_t);
using CommitFn=D1_SYSV uint64_t(*)(uint64_t,uint64_t,uint8_t);
ReserveFn original_reserve;
CommitFn original_commit;
template<class T> void store(uint64_t region,uint64_t off,T value){
    std::memcpy(reinterpret_cast<void*>(region+off),&value,sizeof value);
}
D1_SYSV uint64_t reserve(uint32_t type,uint64_t bytes,uint64_t name,uint64_t region){
    if(type!=1)return original_reserve(type,bytes,name,region);
    const uint64_t rounded=asset_region::reserve_size(bytes);
    const uint64_t base=rounded?reserve_native_asset_region(rounded):0;
    store(region,0,uint64_t(0));store(region,8,uint64_t(0));
    store(region,0x10,bytes);store(region,0x18,base);
    store(region,0x20,name);store(region,0x28,type);
    store(region,0x2c,uint8_t(0));
    return base;
}
D1_SYSV uint64_t commit(uint64_t region,uint64_t bytes,uint8_t top){
    if(field<uint32_t>(region,0x28)!=1)return original_commit(region,bytes,top);
    const uint64_t base=field<uint64_t>(region,0x18);
    const auto p=asset_region::plan(base,field<uint64_t>(region,0x10),
        field<uint64_t>(region,0),field<uint64_t>(region,8),bytes,
        top!=0,field<uint8_t>(region,0x2c)!=0);
    if(!p.address||!commit_native_asset_region(base,p.address,bytes))return 0;
    store(region,top?8:0,p.next_cursor);
    return p.address;
}
// The original global GPU region is index 2. Its outer callers still own
// the cortex allocator lock and container list. Mirror the existing fields
// rather than inventing a competing handle or independent free-list domain.
using AllocFn=D1_SYSV uint64_t(*)(uint64_t,uint32_t,uint8_t,int32_t);
using FreeFn=D1_SYSV bool(*)(uint64_t,uint64_t,uint32_t,uint8_t,int32_t);
AllocFn original_alloc;
FreeFn original_free;
uint64_t gpu_region(){return nr::g_base+0x1AF9D68+2*0x70;}
asset_suballocation::State state(uint64_t r){
    return {field<uint64_t>(r,0),field<uint64_t>(r,8),field<uint64_t>(r,0x10),
        field<uint64_t>(r,0x18),field<uint64_t>(r,0x38),
        field<uint64_t>(r,0x40),field<uint64_t>(r,0x48)};
}
void account(uint32_t tag,uint64_t bytes,bool add){
    const uint64_t counter=nr::g_base+0x1AF9FA0+uint64_t(tag)*8;
    const uint64_t old=field<uint64_t>(counter,0);
    store(counter,0,add?old+bytes:old-bytes);
}
D1_SYSV uint64_t allocate(uint64_t bytes,uint32_t tag,uint8_t top,int32_t index){
    if(index!=2)return original_alloc(bytes,tag,top,index);
    const uint64_t r=gpu_region();
    const auto s=state(r);
    const bool growable=nr::eb<uint8_t>(0x1AF9CE8)!=0;
    const auto a=asset_suballocation::allocate(s,bytes,top!=0,growable);
    if(a.path==asset_suballocation::Path::fallback)return original_alloc(bytes,tag,top,index);
    if(a.path==asset_suballocation::Path::redistribute_high){
        store(r,0x50,field<uint64_t>(r,0x50)+a.padding);
    } else if(a.path==asset_suballocation::Path::grow_high){
        // Preserve the original padding and used-side mutation even if
        // the following physical/host commit fails.
        store(r,0x50,field<uint64_t>(r,0x50)+a.padding);
        store(r,0x48,s.high_backed);
        if(!commit(r+8,a.growth,1))return 0;
        store(r,0x38,s.committed+a.growth);
    } else if(a.path==asset_suballocation::Path::grow_low){
        if(!commit(r+8,a.growth,0))return 0;
        store(r,0x38,s.committed+a.growth);
    }
    store(r,8,a.low_backed);store(r,0x10,a.high_backed);
    store(r,top?0x48:0x40,a.used);
    if(top)store(r,0x58,uint8_t(1));
    account(tag,bytes,true);
    return a.address;
}
asset_suballocation::ContainerHead container_head(uint64_t r,uint64_t address){
    using Head=asset_suballocation::ContainerHead;
    const uint16_t first=field<uint16_t>(r,0x64);
    if(first==0xffff)return Head{true};
    // 0xB2FF6: both table offsets are relative to their own fields.
    const uint64_t table=nr::g_base+0x1AD5000;
    const uint64_t stride=field<uint64_t>(table,0x10);
    const uint64_t entries=table+field<uint64_t>(table,0);
    const uint64_t links=table+8+field<uint64_t>(table,8);
    // 0xB3820 initializes 0x1D1 entries with 0x140-byte stride.
    if(stride!=0x140||first>=0x1d1)return {};
    const uint64_t addr=field<uint64_t>(entries+stride*first,0x30);
    if(addr!=address)return Head{true,true,false,addr,0};
    const uint16_t next=field<uint16_t>(links+uint64_t(first)*4,0);
    if(next==0xffff)return Head{true,true,false,addr,0};
    if(next>=0x1d1||next==first)return {};
    return Head{true,true,true,addr,field<uint64_t>(entries+stride*next,0x30)};
}
D1_SYSV bool release(uint64_t address,uint64_t bytes,uint32_t tag,uint8_t top,int32_t index){
    if(index!=2)return original_free(address,bytes,tag,top,index);
    const uint64_t r=gpu_region();
    const auto s=state(r);
    const bool growable=nr::eb<uint8_t>(0x1AF9CE8)!=0;
    const bool active=field<uint8_t>(r,0x58)!=0;
    asset_suballocation::ContainerHead head;
    if(top&&!active&&asset_suballocation::valid(s)&&
       address==s.base+(growable?s.capacity:s.committed)-s.high_used)
        head=container_head(r,address);
    const auto a=asset_suballocation::release(s,address,bytes,top!=0,
        growable,nr::eb<uint8_t>(0x1AFA150)==1,active,head);
    if(!a.handled)return original_free(address,bytes,tag,top,index);
    if(!a.success)return false;
    if(a.dirty)store(r,0x59,uint8_t(1));
    else store(r,top?0x48:0x40,a.used);
    account(tag,bytes,false);
    return true;
}
// The outer callers (including AF8F0) already hold the allocator lock.
// B34F0 is the common container-release boundary, not a GPU-only hook.
D1_SYSV bool release_container(uint32_t id){
    const uint64_t table=nr::g_base+0x1ad5000;
    // The recovered fixed table has 465 entries of 320 bytes. Reject
    // invalid IDs instead of indexing outside the native contract.
    if(id>=0x1d1||field<uint64_t>(table,0x10)!=0x140)return false;
    return ng::pool::release_container(table,nr::g_base+0x1af9d68,
        nr::g_base+0x1af9cc0,uint16_t(id),
        [](uint32_t k){return nr::eb<int32_t>(0x15b5b60+uint64_t(k)*4);},
        [](uint64_t addr,uint64_t bytes,uint32_t kind,uint8_t top,int32_t region){
            return reinterpret_cast<FreeFn>(nr::g_base+0xb2f40)(addr,bytes,kind,top,region);
        });
}
bool install(){
    // Required native production cohorts. The individual allocator
    // functions still explicitly identify unreconstructed cases.
    bool ok=hook(0xB4780,&reserve,original_reserve,"asset_region_reserve");
    ok&=hook(0xB4630,&commit,original_commit,"asset_region_commit");
    ok&=hook(0xB2BD0,&allocate,original_alloc,"asset_region_suballocate");
    ok&=hook(0xB2F40,&release,original_free,"asset_region_release");
    ok&=replace_eboot_function(0xB34F0,reinterpret_cast<const void*>(&release_container));
    if(!ok)log("nd: required native asset backing/release installation failed");
    return ok;
}
} // namespace asset_backing

// ---------------------------------------------------------------------------- engine resource events
namespace resev {
// 0x6FBC0 destroys a package datum pool, clears its occupancy bit and
// zeroes the descriptor. This is a lifetime boundary, unlike mip release.
D1_SYSV void h_pool_free(uint32_t value) {
    const uint32_t pool=value&0x3FFFu;
    auto* registry=nr::resreg::g_registry;
    const auto root=[] {return nr::eb<uint64_t>(0x1a238b8);};
    // No-op release is not a lifetime event.
    if(!ng::pool::occupied(root(),pool))return;
    bool retiring=false;
    {
        // Do not free a pool while a command-list capture is copying a
        // source version. Release the gate before original callbacks,
        // which can themselves publish texture changes.
        auto gate=ng::texture_leases().retire();
        retiring=registry&&registry->begin_pool_retire(pool);
    }
    {
        using Notify=D1_SYSV void(*)(uint32_t,uint32_t);
        using ReleaseTag=D1_SYSV bool(*)(uint32_t);
        ng::pool::destroy(value,root,[](uint32_t i,uint16_t kind){
            if(auto cb=nr::eb<uint64_t>(0x1a238f0))reinterpret_cast<Notify>(cb)(i,kind);
            if(auto cb=nr::eb<uint64_t>(0x1a238f8))reinterpret_cast<Notify>(cb)(i,kind);
        },[](uint64_t){/* 75860 is a single RET, not an allocator free. */},
          [](uint16_t tag){
              // AF8F0 supplies the existing lock domain around the
              // now-replaced B34F0 body for every container release.
              reinterpret_cast<ReleaseTag>(nr::g_base+0xaf8f0)(tag);
          });
    }
    if(retiring){
        auto gate=ng::texture_leases().retire();
        registry->end_pool_retire(pool);
    }
}
uint64_t tex_base(const uint32_t* t) { return (uint64_t(t[0]) | uint64_t(t[1] & 0xFF) << 32) << 8; }
// Both switch dispatchers and their leaves define AL, not full RAX.
// The retail high-mip bind at CBAA4 is followed immediately by E01E0.
// Keep the publication gate across that pair, not across unrelated calls.
thread_local decltype(ng::texture_leases().retire()) pending_mip_bind;
bool mip_pair_hooked=false;
bool resource_bind(uint32_t type,uint32_t handle,uint64_t addr,bool repoint,bool paired=false) {
    // Publish descriptor writes and their native version as a single
    // transaction relative to command-list capture. Other resource types
    // have their own binding contracts.
    decltype(ng::texture_leases().retire()) texture_transaction;
    if((type>=1&&type<=4)||type==6)texture_transaction=ng::texture_leases().retire();
    {
        using namespace ng::binding;
        if(!supported(type,repoint))return false;
        if(type==16)return true;
        {
            const uint64_t obj=handle==UINT32_MAX?0:nr::resolve_handle_safe(handle);
            const auto result=apply(type,repoint,reinterpret_cast<void*>(obj),addr,
                                    type==10&&obj?guest_region_extent(obj):0);
            if(result!=Result::bound)return false;
            if(nr::resreg::tracked_type(type)&&obj) {
                // Publish once from the already resolved object. Geometry
                // creation takes a native byte range directly from this event.
                if(paired&&texture_transaction.owns_lock())pending_mip_bind=std::move(texture_transaction);
                else {
                    nr::resreg::touch(obj,true,handle);
                    if(type>=1&&type<=3)ng::native_texture_event(obj,nr::resreg::get(obj),repoint);
                    else nr::resreg::prefetch(type,obj);
                }
            }
            return true;
        }
    }
}
D1_SYSV bool h_bind(uint32_t type,uint32_t handle,uint64_t addr) {
    const auto caller=uint64_t(__builtin_return_address(0));
    return resource_bind(type,handle,addr,false,mip_pair_hooked&&caller==nr::g_base+0xCBAA7&&type>=1&&type<=3);
}
D1_SYSV bool h_rebind(uint32_t type,uint32_t handle,uint64_t addr) {
    return resource_bind(type,handle,addr,true);
}
D1_SYSV void h_release(uint32_t type, uint32_t handle) {
    {
        // 0x7D2FE0: all non-texture cases are RET-only or unsupported.
        // 0x7D43E0: release a high mip only when the full chain is resident.
        // The mip callback owns the actual epoch publication. Do not retire
        // a resource identity or bump its epoch for a no-op release.
        if(handle==UINT32_MAX||type<1||type>3)return;
        const uint64_t obj=nr::resreg::object_of(type,handle);
        if(obj&&ng::binding::release_top_mip(reinterpret_cast<const void*>(obj))) {
            using Mips=D1_SYSV void(*)(uint32_t,uint64_t,uint64_t);
            const auto base=uint64_t(field<uint32_t>(obj,0))<<8;
            reinterpret_cast<Mips>(nr::g_base+0x7D4480)(handle,base,0);
        }
        return;
    }
}
// 0x7DAA60 and 0x7DB230: process-lifetime native queue storage and
// completion label. No package allocator, GPU direct-memory allocation or
// retail allocation handle is necessary for these CPU-only records.
std::mutex g_blit_storage_m;
uint8_t* g_blit_storage=nullptr;
D1_SYSV void h_blit_init(){
    using namespace ng::blit;
    std::lock_guard lock(g_blit_storage_m);
    const uint64_t original=nr::eb<uint64_t>(0x26DF268);
    if(original&&original!=uint64_t(g_blit_storage)){
        log("nd: cannot replace a previously initialized retail blitter allocation");
        std::abort();
    }
    if(!g_blit_storage){
        const uint64_t gran=os::granularity();
        const uint64_t bytes=(storage_bytes+gran-1)&~(gran-1);
        // Non-replacing hints; do not overwrite a preexisting guest mapping.
        for(uint64_t hint:{0xC80000000ull,0xC00000000ull,0xB80000000ull,0xB00000000ull}){
            void* p=os::alloc(hint,bytes,os::kRead|os::kWrite,false);
            if(p&&uint64_t(p)<(1ull<<40)-bytes){
                g_blit_storage=static_cast<uint8_t*>(p);
                register_native_host_region(uint64_t(p),bytes);
                break;
            }
            if(p)os::release(p,bytes);
        }
        if(!g_blit_storage){log("nd: native blitter storage allocation failed");std::abort();}
    } else {
        const uint64_t cursor=__atomic_load_n(reinterpret_cast<uint64_t*>(g_blit_storage+cursor_offset),__ATOMIC_ACQUIRE);
        if(!valid(cursor)||used(cursor)){log("nd: cannot reset active native blitter");std::abort();}
    }
    initialize_storage(g_blit_storage,nr::eb<uint32_t>(0x26DF280));
    auto* root=reinterpret_cast<void*>(nr::g_base+0x26DF268);
    ng::binding::store(root,0,uint64_t(g_blit_storage));
    ng::binding::store(root,8,uint16_t(0xffff)); // no retail allocation handle
    ng::binding::store(root,12,uint32_t(0));
    ng::binding::store(root,0x20,uint64_t(g_blit_storage)+label_offset);
    log("nd: GPU blitter ring and label owned by native host memory at %p",static_cast<void*>(g_blit_storage));
}
// 0x7DA8B0/0x7DA910/0x7DA960: source-defined reservation and
// enqueue using the retail consumer's exact shared cursor and record ABI.
D1_SYSV uint64_t h_blit_reserve() {
    const uint64_t base=nr::eb<uint64_t>(0x26DF268);
    if(!base)return 0;
    auto* cursor=reinterpret_cast<uint64_t*>(base+0x28000);
    uint64_t old=__atomic_load_n(cursor,__ATOMIC_ACQUIRE),updated;
    uint32_t slot;
    for(;;){
        if(!ng::blit::reserve(old,updated,slot))return 0;
        if(__atomic_compare_exchange_n(cursor,&old,updated,false,__ATOMIC_SEQ_CST,__ATOMIC_ACQUIRE))
            return base+uint64_t(slot)*0x40;
    }
}
D1_SYSV bool h_blit_can_request(uint32_t count) {
    const uint64_t base=nr::eb<uint64_t>(0x26DF268);
    return base&&ng::blit::can_request(__atomic_load_n(reinterpret_cast<uint64_t*>(base+0x28000),__ATOMIC_ACQUIRE),count);
}
D1_SYSV bool h_blit_enqueue(uint64_t dst,uint64_t src,uint32_t size,uint64_t callback,
                            const uint64_t* a,const uint64_t* b,uint64_t record) {
    if(!a||!b)return false;
    if(!record)record=h_blit_reserve();
    if(!record)return false;
    // Stack arguments are copied by value before publishing the ready bit.
    ng::blit::initialize(reinterpret_cast<void*>(record),{dst,src,size,callback,*a,*b});
    __atomic_exchange_n(reinterpret_cast<uint32_t*>(record),1u,__ATOMIC_SEQ_CST);
    return true;
}
// Batch operations retain the engine's device selection, reset and label
// ABI, but issue one ordered native copy instead of console DMA fragments.
uint64_t blit_device(){
    using DeviceFn=D1_SYSV uint64_t(*)(uint64_t);
    return reinterpret_cast<DeviceFn>(nr::g_base+0x7E3A00)(nr::eb<uint64_t>(0x26E2BE8));
}
void blit_begin_device(uint64_t dev) {
    using VoidFn=D1_SYSV void(*)(uint64_t);
    using BeginFn=D1_SYSV uint64_t(*)(uint64_t);
    reinterpret_cast<VoidFn>(nr::g_base+0x7DCAA0)(dev);
    reinterpret_cast<BeginFn>(nr::g_base+0x7DCCE0)(dev);
    reinterpret_cast<VoidFn>(nr::g_base+0x7DCAF0)(dev);
}
void blit_close_device(uint64_t dev) {
    using CloseFn=D1_SYSV void(*)(uint64_t,uint64_t,uint64_t);
    using DrainFn=D1_SYSV bool(*)(uint64_t);
    reinterpret_cast<CloseFn>(nr::g_base+0x7DCEC0)(dev,0,0);
    reinterpret_cast<DrainFn>(nr::g_base+0x7DD060)(dev);
}
D1_SYSV void h_blit_batch_begin(void* batch) {
    using namespace ng::binding;
    store(batch,0,__atomic_fetch_add(reinterpret_cast<uint32_t*>(nr::g_base+0x26DF280),1u,__ATOMIC_SEQ_CST));
    store(batch,4,uint32_t(0));
    if(!load<uint32_t>(batch,8)){
        blit_begin_device(blit_device());
        store(batch,12,uint16_t(0));store(batch,14,uint8_t(1));
    }
}
D1_SYSV void h_blit_issue(void* batch,void* record) {
    using namespace ng::binding;
    const int32_t size=load<int32_t>(record,0x18);
    if(size>0){
        const auto dev=blit_device();
        const bool owns_list=load<uint32_t>(batch,8)==0;
        if(owns_list&&load<uint8_t>(batch,12)){
            blit_begin_device(dev);
            store(batch,12,uint16_t(0));store(batch,14,uint8_t(1));
        }
        const uint64_t cb=field<uint64_t>(dev,0x50);
        if(!cb){log("nd: blitter has no active native list");std::abort();}
        // 0xF8F3A0 is already replaced by the native device's ordered DMA
        // capture. A host memmove handles both overlap directions correctly.
        using CopyFn=D1_SYSV void(*)(uint64_t,uint64_t,uint64_t,uint32_t,uint32_t);
        reinterpret_cast<CopyFn>(nr::g_base+0xF8F3A0)(cb,load<uint64_t>(record,8),load<uint64_t>(record,0x10),uint32_t(size),1);
        const uint32_t count=load<uint32_t>(batch,4);
        store(batch,4,count+1);
        if(count>=4000)store(batch,13,uint8_t(1));
        if(owns_list&&load<uint8_t>(batch,13)){
            blit_close_device(dev);
            store(batch,13,uint8_t(0));store(batch,4,uint32_t(0));
            blit_begin_device(dev);
            store(batch,12,uint8_t(0));store(batch,14,uint8_t(1));
        }
    }
    store(record,0x3c,uint8_t(1));
    store(record,0x38,load<uint32_t>(batch,0));
}
D1_SYSV void h_blit_batch_end(void* batch) {
    using namespace ng::binding;
    const auto dev=blit_device();
    const uint64_t cb=field<uint64_t>(dev,0x50);
    if(!cb){log("nd: blitter completion has no active list");std::abort();}
    using LabelFn=D1_SYSV void(*)(uint64_t,uint32_t,uint32_t,uint64_t,uint32_t,uint64_t,uint32_t,uint32_t);
    const uint64_t label=nr::eb<uint64_t>(0x26DF288);
    const uint64_t sequence=uint64_t(int64_t(load<int32_t>(batch,0)));
    reinterpret_cast<LabelFn>(nr::g_base+0xF80C90)(cb,4,0,label,1,sequence,0,0);
    if(!load<uint32_t>(batch,8)){
        blit_close_device(dev);
        using ResetFn=D1_SYSV void(*)(uint64_t);
        reinterpret_cast<ResetFn>(nr::g_base+0x7DCAA0)(dev);
        store(batch,12,uint8_t(1));store(batch,14,uint8_t(0));store(batch,13,uint8_t(0));store(batch,4,uint32_t(0));
    }
}
// The original blitter has a single consumer, which first issues a
// ready prefix and later retires the completed prefix. Keep that contract
// and the original batch device/label operations until they are migrated.
D1_SYSV bool h_blit_submit(uint32_t mode) {
    using namespace ng::blit;
    const uint64_t base=nr::eb<uint64_t>(0x26DF268);
    if(!base)return true;
    auto* cursor=reinterpret_cast<uint64_t*>(base+0x28000);
    auto state=__atomic_load_n(cursor,__ATOMIC_ACQUIRE);
    if(!valid(state))std::abort();
    uint32_t slot=head(state);
    alignas(8) uint8_t batch[16]{};
    ng::binding::store(batch,8,mode);
    using BatchFn=D1_SYSV void(*)(void*);
    using IssueFn=D1_SYSV void(*)(void*,void*);
    bool issued=false;
    while(contains(state,slot)){
        const uint64_t record=base+uint64_t(slot)*0x40;
        if(__atomic_load_n(reinterpret_cast<uint32_t*>(record),__ATOMIC_ACQUIRE)!=1)break;
        if(field<uint8_t>(record,0x3c)==0){
            if(!issued){
                reinterpret_cast<BatchFn>(nr::g_base+0x7DB2D0)(batch);
                issued=true;
            }
            reinterpret_cast<IssueFn>(nr::g_base+0x7DB340)(batch,reinterpret_cast<void*>(record));
        }
        slot=next(slot);
        state=__atomic_load_n(cursor,__ATOMIC_ACQUIRE);
        if(!valid(state))std::abort();
    }
    if(issued)reinterpret_cast<BatchFn>(nr::g_base+0x7DB4B0)(batch);
    return !issued;
}
D1_SYSV bool h_blit_complete() {
    using namespace ng::blit;
    const uint64_t base=nr::eb<uint64_t>(0x26DF268),label=nr::eb<uint64_t>(0x26DF288);
    if(!base||!label)return true;
    // Sample the completed label once, as 0x7DB290 does. Never retire
    // a later batch merely because it completed during this invocation.
    const uint32_t sample=__atomic_load_n(reinterpret_cast<uint32_t*>(label),__ATOMIC_ACQUIRE);
    auto* cursor=reinterpret_cast<uint64_t*>(base+0x28000);
    for(;;){
        uint64_t state=__atomic_load_n(cursor,__ATOMIC_ACQUIRE);
        if(!valid(state))std::abort();
        const uint32_t slot=head(state);
        if(!contains(state,slot))return true;
        const uint64_t record=base+uint64_t(slot)*0x40;
        if(__atomic_load_n(reinterpret_cast<uint32_t*>(record),__ATOMIC_ACQUIRE)!=1)return true;
        if(field<uint8_t>(record,0x3c)!=1||!completed(sample,field<uint32_t>(record,0x38)))return false;
        const uint64_t cb=field<uint64_t>(record,0x20);
        if(cb){
            using Callback=D1_SYSV void(*)(uint64_t*,uint64_t*,uint64_t,uint64_t);
            uint64_t a=field<uint64_t>(record,0x28),b=field<uint64_t>(record,0x30);
            reinterpret_cast<Callback>(cb)(&a,&b,field<uint64_t>(record,8),field<uint64_t>(record,0x10));
        }
        ng::binding::store(reinterpret_cast<void*>(record),0x3c,uint8_t(2));
        __atomic_exchange_n(reinterpret_cast<uint32_t*>(record),0u,__ATOMIC_SEQ_CST);
        // Other producers may advance tail. Only this consumer advances
        // head, so preserve the latest tail across failed CAS attempts.
        state=__atomic_load_n(cursor,__ATOMIC_ACQUIRE);
        do {
            if(head(state)!=slot||!valid(state))std::abort();
        } while(!__atomic_compare_exchange_n(cursor,&state,pack(next(slot),tail(state)),false,
                                             __ATOMIC_SEQ_CST,__ATOMIC_ACQUIRE));
    }
}
namespace relocation {
using Unary=D1_SYSV void(*)(uint64_t);
using ClockFn=D1_SYSV uint32_t(*)();
using Query=D1_SYSV uint64_t(*)(uint32_t,uint32_t,uint32_t,uint32_t);
using Split=D1_SYSV uint64_t(*)(uint64_t,uint32_t,uint32_t,uint32_t*);
using SizeFn=D1_SYSV uint64_t(*)(uint32_t*);
using Link=D1_SYSV void(*)(uint32_t,uint16_t);
using Finalize=D1_SYSV void(*)(uint32_t,uint32_t,uint32_t);
template<class Fn> Fn leaf(uint64_t offset){return reinterpret_cast<Fn>(nr::g_base+offset);}
uint64_t entry(uint32_t id){
    using namespace ng::move;
    if(!container_valid(id))return 0;
    const uint64_t table=nr::eb<uint64_t>(0x2587A08);
    return table+field<uint64_t>(table,0)+uint64_t(container_index(id))*field<uint64_t>(table,0x10);
}
uint64_t datum_slot(uint32_t h,uint64_t* pool_out=nullptr){
    using namespace ng::move;
    const uint64_t root=nr::eb<uint64_t>(0x1A238A0);
    const uint64_t tbl=field<uint64_t>(root,0);
    const uint64_t pool=tbl+uint64_t(pool_index(h))*0x40;
    if(pool_out)*pool_out=pool;
    return field<uint64_t>(pool,0x10)+datum_offset(h,field<uint32_t>(pool,0x30));
}
uint32_t primary(uint32_t h){return field<uint32_t>(datum_slot(h),0x10);}
uint32_t secondary(uint32_t h){
    using namespace ng::move;
    const uint64_t slot=datum_slot(h);
    const auto flags=field<uint16_t>(slot,4);
    const auto id=field<uint32_t>(slot,0x10);
    uint32_t indirect=0;
    if(flags&4){
        const uint64_t e=entry(id);
        if(!e)return UINT32_MAX;
        indirect=field<uint32_t>(e,0xc);
    }
    return secondary_id(flags,id,indirect);
}
uint32_t owner_token(uint32_t id){
    const uint64_t e=entry(id);
    return e?field<uint32_t>(e,8):UINT32_MAX;
}
uint32_t owner(uint32_t id){
    const uint64_t e=entry(id);
    if(!e)return UINT32_MAX;
    return field<uint32_t>(e,field<uint8_t>(e,0x14)==2?8:0xc);
}
void pin(uint64_t e){
    auto* p=reinterpret_cast<uint8_t*>(e+0x16);
    *p|=1;
}
void unpin(uint64_t e){
    auto* p=reinterpret_cast<uint8_t*>(e+0x16);
    *p=ng::move::unpin(*p);
}
void stamp_unpin(uint64_t e){
    const uint32_t value=reinterpret_cast<ClockFn>(nr::eb<uint64_t>(0x2583908))();
    ng::binding::store(reinterpret_cast<void*>(e),0x10,value);
    unpin(e);
}
uint64_t datum(uint32_t h){
    uint64_t pool=0;
    const uint64_t slot=datum_slot(h,&pool);
    return ng::move::datum_address(slot,0,field<int32_t>(pool,0x34),field<uint64_t>(slot,8));
}
uint64_t query(uint32_t a,uint32_t b,uint32_t kind){
    return reinterpret_cast<Query>(nr::eb<uint64_t>(0x254EE98))(a,b,kind,1);
}
// Native lookup and pin leaves preserve the original metadata ordering;
// the higher-level finalize and split operations remain original.
D1_SYSV void copy_complete(uint64_t* a,uint64_t* b,uint64_t,uint64_t){
    const uint32_t id=uint32_t(*a), split=uint32_t(*b);
    const uint32_t generation=owner_token(id);
    const uint32_t handle=owner(id);
    const uint64_t owner=entry(primary(handle)), moved=entry(id), temporary=entry(split);
    leaf<Finalize>(0xE00D0)(handle,generation,handle);
    unpin(owner);
    unpin(moved);
    stamp_unpin(temporary);
}
D1_SYSV void swap_complete(uint64_t* a,uint64_t*,uint64_t,uint64_t){
    const uint32_t id=uint32_t(*a);
    const uint32_t generation=owner_token(id);
    const uint32_t handle=owner(id);
    const uint64_t owner=entry(primary(handle)), moved=entry(id);
    leaf<Finalize>(0xE00D0)(handle,generation,UINT32_MAX);
    unpin(owner);
    unpin(moved);
}
void enqueue(uint64_t dst,uint64_t src,uint32_t bytes,uint64_t callback,uint64_t a,uint64_t b){
    if(!h_blit_enqueue(dst,src,bytes,callback,&a,&b,0)){
        // Both entry points have already changed the original resource
        // metadata. Continuing without the completion would strand pins.
        log("nd: resource relocation enqueue failed; refusing false completion");
        std::abort();
    }
}
void copy(uint32_t a,uint32_t b){
    using namespace ng::move;
    const uint64_t delta=query(a,b,1);
    const uint64_t destination=datum(b);
    const uint32_t first=primary(b), second=secondary(b);
    const uint64_t e1=entry(first), e2=entry(second);
    pin(e1);pin(e2);
    const uint64_t source=datum(a);
    const uint64_t bytes=query(a,b,2);
    uint32_t temporary=0;
    const auto split=leaf<Split>(0xDEB60)(e2,container_index(second),container_generation(second),&temporary);
    pin(split);
    leaf<Unary>(0xDE1D0)(a);
    const auto p=ng::move::copy(source,destination,delta,bytes);
    enqueue(p.dst,p.src,p.bytes,reinterpret_cast<uint64_t>(&copy_complete),second,temporary);
}
void swap(uint32_t a,uint32_t b){
    using namespace ng::move;
    if(a==UINT32_MAX)return;
    const uint32_t source_id=primary(b);
    // 6FB60 only reads the pool occupancy and returns a pointer.
    // The original caller discards both results; no native call is necessary.
    const uint64_t source=datum(b), delta=query(a,b,1);
    const uint64_t destination=source+delta;
    const uint32_t destination_id=primary(a);
    const auto bytes=leaf<SizeFn>(0x6DBD0)(&a);
    ng::pool::set_datum(nr::eb<uint64_t>(0x1a23898),a,destination,relocation_flags(bytes));
    ng::pool::set_datum(nr::eb<uint64_t>(0x1a23898),b,source,relocation_flags(delta));
    const uint64_t e1=entry(source_id),e2=entry(destination_id);
    ng::binding::store(reinterpret_cast<void*>(e2),0,destination);
    leaf<Link>(0xDE1E0)(destination_id,container_index(source_id));
    pin(e1);pin(e2);
    enqueue(0,0,0,reinterpret_cast<uint64_t>(&swap_complete),destination_id,0);
}
} // namespace relocation
D1_SYSV void h_copy(uint64_t a, uint64_t b) {
    relocation::copy(uint32_t(a),uint32_t(b));
    nr::resreg::event_any(uint32_t(a)), nr::resreg::event_any(uint32_t(b));
}
D1_SYSV void h_swap(uint64_t a, uint64_t b) {
    relocation::swap(uint32_t(a),uint32_t(b));
    nr::resreg::event_any(uint32_t(a)), nr::resreg::event_any(uint32_t(b));
}
namespace texture_constructor {
using namespace ng::texture_ctor;
using LayoutFn=D1_SYSV uint32_t(*)(LayoutOutput*,const LayoutInput*);
using FootprintFn=D1_SYSV uint32_t(*)(uint64_t*,uint32_t*,void*);
FootprintFn original_footprint;
uint32_t original_layout(LayoutOutput& out,const LayoutInput& in){
    return reinterpret_cast<LayoutFn>(nr::g_base+0xF85CB0)(&out,&in);
}
bool candidate_layout(LayoutOutput& out,const LayoutInput& in){
    if(in.tile>=32||uint8_t(in.format)>=0x3b)return false;
    const uint32_t table=nr::eb<uint32_t>(0x16C7880+uint64_t(in.tile)*4);
    const uint32_t mode=array_mode(table);
    if(mode>3)return false;
    const uint32_t bits=nr::eb<uint32_t>(0x1609CE0+uint64_t(uint8_t(in.format))*4);
    const uint32_t thick=mode==3?nr::eb<uint32_t>(0x160A110):1;
    return small_surface_layout(out,in,table,bits,thick);
}
uint32_t calculate_layout(LayoutOutput& out,const LayoutInput& in){
    static const bool on=[]{
        const char* e=std::getenv("D1_PC_NATIVE_SMALL_LAYOUT");
        return e&&std::atoi(e)!=0;
    }();
    if(on&&candidate_layout(out,in))return 0;
    return original_layout(out,in);
}
D1_SYSV uint32_t calculate_footprint(uint64_t* bytes,uint32_t* alignment,void* descriptor){
    if(!bytes||!alignment||!descriptor)return invalid;
    LayoutInput seed{};
    uint32_t words[8];
    std::memcpy(words,descriptor,sizeof words);
    const uint32_t tile=(words[3]>>20)&31;
    // F87240/F87270 read these bitfields from the same original table.
    const uint32_t table_word=nr::eb<uint32_t>(0x16C7880+uint64_t(tile)*4);
    const auto status=prepare(seed,words,0,0,table_word,calculate_layout);
    // The original ignores preparation errors; preserve its fallback
    // instead of consuming a potentially incomplete request.
    if(status)return original_footprint(bytes,alignment,descriptor);
    return footprint(*bytes,*alignment,words,seed,calculate_layout);
}
// Opt-in differential, executed only on the ordinary ordered renderer
// path. The original leaf is the oracle, never the renderer's source of
// layout information. Deduplicate descriptor shapes to bound diagnostics.
void compare_native_layout(const uint32_t* w,const tiling::Surface& s){
    static std::mutex m;
    static std::unordered_set<uint64_t> seen;
    static uint64_t checked=0,failed=0,oracle_errors=0,candidate_checks=0,candidate_failures=0;
    const uint32_t kind=w[3]>>28;
    if(kind<8||kind>13)return;
    uint64_t key=0;
    for(unsigned i=1;i<8;++i)key=(key^uint64_t(i==1?w[i]&0xfff00000u:w[i]))*0x9e3779b97f4a7c15ull;
    std::lock_guard lk(m);
    if(seen.size()>=32768||!seen.insert(key).second)return;
    LayoutInput seed{};
    const uint32_t tile=(w[3]>>20)&31;
    const uint32_t table_word=nr::eb<uint32_t>(0x16C7880+uint64_t(tile)*4);
    if(prepare(seed,w,0,0,table_word,original_layout)){
        ++oracle_errors;return;
    }
    uint64_t off=0;
    const uint64_t layers=slice_count(w);
    const uint32_t scale=s.block?4:1;
    bool mismatch=false;
    for(uint32_t lv=0;lv<s.levels&&lv<tiling::kMaxMips;++lv){
        auto in=seed;
        in.width=std::max(1u,seed.width>>lv);
        in.height=std::max(1u,seed.height>>lv);
        in.depth=std::max(1u,seed.depth>>lv);
        in.base_pitch=lv?((w[4]>>13)&0x3fff)+1:0;
        in.mip=lv;
        LayoutOutput out{};
        if(original_layout(out,in)){++oracle_errors;return;}
        LayoutOutput candidate{};
        if(candidate_layout(candidate,in)){
            ++candidate_checks;
            if(!small_layout_equal(candidate,out)){
                if(++candidate_failures<=16)
                    log("native small layout differential mismatch tile=%u fmt=%#x mip=%u %ux%u",in.tile,in.format,in.mip,in.width,in.height);
            }
        }
        const uint64_t size=(uint64_t(out.words[4])|(uint64_t(out.words[5])<<32))*layers;
        const auto& n=s.mips[lv];
        const uint32_t pitch=out.words[0]/scale,height=out.words[1]/scale;
        if(n.pitch!=pitch||n.height!=height||n.size!=size||n.offset!=off){
            if(failed<48&&!mismatch)
                log("native layout differential: tile %u dfmt %u %ux%u basepitch %u layers %llu mip %u | native p/h %u/%u off/size %llu/%llu | original p/h %u/%u off/size %llu/%llu mode %u",
                    tile,(w[1]>>20)&63,s.width,s.height,s.pitch,(unsigned long long)layers,lv,
                    n.pitch,n.height,(unsigned long long)n.offset,(unsigned long long)n.size,
                    pitch,height,(unsigned long long)off,(unsigned long long)size,out.words[6]);
            mismatch=true;
        }
        off+=size;
    }
    ++checked;if(mismatch)++failed;
    if((checked&(checked-1))==0)
        log("native layout differential: %llu unique descriptors, %llu mismatched, %llu original errors | small leaf %llu checked, %llu mismatched",
            (unsigned long long)checked,(unsigned long long)failed,(unsigned long long)oracle_errors,
            (unsigned long long)candidate_checks,(unsigned long long)candidate_failures);
}
uint64_t build(void* p,const Request& q){
    return construct(p,q,
        calculate_layout,
        [](uint64_t& bytes,uint32_t& alignment,void* descriptor){
            return reinterpret_cast<FootprintFn>(nr::g_base+0xF89BD0)(&bytes,&alignment,descriptor);
        });
}
D1_SYSV uint64_t build_2d(void* p,uint32_t w,uint32_t h,uint32_t slices,uint32_t mips,
                          uint32_t format,uint32_t tile,uint32_t fragments,uint8_t cube){
    return build(p,{w,h,slices,mips,format,tile,fragments,cube,false});
}
D1_SYSV uint64_t build_3d(void* p,uint32_t w,uint32_t h,uint32_t depth,uint32_t mips,
                          uint32_t format,uint32_t tile){
    return build(p,{w,h,depth,mips,format,tile,0,0,true});
}
} // namespace texture_constructor
// 0xE01E0 resolves the low/high allocation handles, while 0xE00D0
// resolves the same pair before iterating the linked texture tags. Preserve
// the exact size from the original allocation-size query (0x6DBD0) in
// this synchronous callback scope. Addresses alone cannot prove a full chain.
struct MipBounds {uint64_t low=0,high=0,low_size=0,high_size=0;};
thread_local MipBounds mip_bounds;
uint64_t mip_allocation_size(uint32_t h,uint64_t base){
    if(h==UINT32_MAX||base<0x10000)return 0;
    using SizeFn=D1_SYSV uint64_t(*)(uint32_t*);
    const uint64_t bytes=reinterpret_cast<SizeFn>(nr::g_base+0x6dbd0)(&h);
    return bytes&&bytes<=(512ull<<20)&&guest_readable_range(base,bytes)?bytes:0;
}
struct MipBoundsScope {
    MipBounds previous=mip_bounds;
    MipBoundsScope(uint32_t low,uint32_t high){
        mip_bounds={};
        if(low!=UINT32_MAX)mip_bounds.low=nr::resolve_handle_safe(low);
        if(high!=UINT32_MAX)mip_bounds.high=nr::resolve_handle_safe(high);
        mip_bounds.low_size=mip_allocation_size(low,mip_bounds.low);
        mip_bounds.high_size=mip_allocation_size(high,mip_bounds.high);
    }
    ~MipBoundsScope(){mip_bounds=previous;}
};
// Complete 0xE01E0 body (analysis/ps4-0133/pseudocode/000e0000.c):
// resolve both memory handles (invalid sentinel -> zero), then dispatch
// the registered mip callback at 0x254EE80(tag,low address,high address).
// This leaf starts with non-prologue instructions, so a trampoline is
// neither necessary nor supported. Preserve the live callback table;
// do not call h_mips directly or assume which callback is registered.
D1_SYSV void h_mip_handles(uint64_t tag,uint32_t low,uint32_t high){
    MipBoundsScope bounds(low,high);
    using Callback=D1_SYSV void(*)(uint64_t,uint64_t,uint64_t);
    const auto callback=nr::eb<Callback>(0x254EE80);
    if(callback)callback(tag,mip_bounds.low,mip_bounds.high);
    // Malformed or unbound handle: never strand the publication gate.
    if(pending_mip_bind.owns_lock())pending_mip_bind.unlock();
}
// 0xDE3B0: resolve the memory link container, optionally following
// its indirect owner. The tag's flags determine which record owns links.
uint64_t mip_link_container(uint32_t tag){
    using namespace ng::move;
    const auto slot=relocation::datum_slot(tag);
    const auto id=field<uint32_t>(slot,0x10);
    if(!memory_link_container(id))return 0;
    auto container=relocation::entry(id);
    if(container&&(field<uint16_t>(slot,4)&4))
        container=relocation::entry(field<uint32_t>(container,0xc));
    return container;
}
// Complete 0xE00D0 -> 0xDE860 -> 0xE01C0 notification path.
// The original linked-resource table and callback ordering are retained;
// no console code or trampoline is needed for this notification chain.
D1_SYSV void h_mip_relocation(uint64_t tag,uint32_t low,uint32_t high){
    MipBoundsScope bounds(low,high);
    if(uint32_t(tag)==UINT32_MAX)return;
    const auto container=mip_link_container(uint32_t(tag));
    if(!container)return;
    const uint32_t token=field<uint32_t>(container,0x10);
    const uint64_t table=nr::eb<uint64_t>(0x2587A10);
    using Callback=D1_SYSV void(*)(uint64_t,uint64_t,uint64_t);
    ng::move::visit_memory_links(token,
        [&](uint16_t i){
            const auto base=table+field<uint64_t>(table,0);
            return field<uint32_t>(base,uint64_t(i)*field<uint64_t>(table,0x10));
        },
        [&](uint16_t i){
            return field<uint16_t>(table,field<uint64_t>(table,8)+8+uint64_t(i)*4);
        },
        [&](uint32_t resource){
            const auto callback=nr::eb<Callback>(0x254EE80);
            if(callback)callback(resource,mip_bounds.low,mip_bounds.high);
        });
}
D1_SYSV void h_mips(uint32_t handle, uint64_t low_base, uint64_t hi_base) {
    // A descriptor and its two source allocations belong to one published
    // generation. Draw capture cannot observe the constructor's partial writes.
    auto transaction=pending_mip_bind.owns_lock()?std::move(pending_mip_bind):ng::texture_leases().retire();
    {
        const uint64_t obj=nr::resreg::object_of(1,handle);
        if(!obj)return;
        using namespace ng::binding;
        auto* p=reinterpret_cast<void*>(obj);
        const auto plan=mip_plan(p,low_base,hi_base);
        // Native constructor bodies retain the original layout/footprint
        // leaves until the full tile-layout contract is reconstructed.
        using Construct2D=D1_SYSV uint64_t(*)(void*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
        using Construct3D=D1_SYSV uint64_t(*)(void*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
        if(plan.volume)
            reinterpret_cast<Construct3D>(nr::g_base+0xF83AB0)(p,plan.width,plan.height,plan.depth,plan.levels,plan.format,plan.tile);
        else
            reinterpret_cast<Construct2D>(nr::g_base+0xF83530)(p,plan.width,plan.height,1,plan.levels,plan.format,plan.tile,plan.fragments,uint32_t(plan.cube));
        mip_base(p,plan.base);
    }
    if (const uint64_t o = nr::resreg::object_of(1, handle)) {
        // Do not associate this callback's addresses with a newer epoch
        // published by another resource event between touch and lookup.
        const uint64_t res=nr::resreg::touch(o,true,handle);
        if (res && ng::active() && nr::resreg::get(o)==res) ng::native_texture_event(o,res,false);
    }
}
D1_SYSV bool h_resident(uint32_t handle) {
    const auto obj=nr::resreg::object_of(1,handle);
    return obj&&ng::binding::fully_resident(reinterpret_cast<const void*>(obj));
}
D1_SYSV void h_flags(uint32_t flags,uint32_t* a,uint32_t* b) {
    const uint32_t value=1^((flags>>1)&1);
    *a=value;*b=value;
}
using TechFn = D1_SYSV uint64_t (*)(uint64_t, uint64_t);
using TechVoidFn = D1_SYSV void (*)(uint64_t, uint64_t);
TechFn o_tech_a, o_tech_b;
TechVoidFn o_tech_c;
void note_tfx_technique(uint64_t tech) {
    if (tech < 0x10000 || !ng::active()) return;
    static thread_local uint64_t last_tech = 0;
    if (tech == last_tech) return;
    last_tech = tech;
    const uint32_t vs_h = field<uint32_t>(tech, 0x28);
    const uint32_t ps_h = field<uint32_t>(tech, 0x2A8);
    const uint64_t vs_o = vs_h != 0xFFFFFFFFu ? nr::resolve_handle_safe(vs_h) : 0;
    const uint64_t ps_o = ps_h != 0xFFFFFFFFu ? nr::resolve_handle_safe(ps_h) : 0;
    if (vs_o >= 0x10000 || ps_o >= 0x10000) {
        ng::prefetch_shader_pair(vs_o >= 0x10000 ? reinterpret_cast<const uint8_t*>(vs_o) : nullptr,
                                 ps_o >= 0x10000 ? reinterpret_cast<const uint8_t*>(ps_o) : nullptr);
    }
}
D1_SYSV uint64_t h_tech_a(uint64_t ctx, uint64_t tech) {
    note_tfx_technique(tech);
    return o_tech_a(ctx, tech);
}
D1_SYSV uint64_t h_tech_b(uint64_t ctx, uint64_t tech) {
    note_tfx_technique(tech);
    return o_tech_b(ctx, tech);
}
D1_SYSV void h_tech_c(uint64_t ctx, uint64_t tech) {
    note_tfx_technique(tech);
    o_tech_c(ctx, tech);
}
D1_SYSV void h_unlock(uint64_t idx) {
    const uint64_t e = nr::g_base + 0x26F23B0 + (idx & 0xFFFFFFFFull) * 0xE0;
    const uint64_t ptr = field<uint64_t>(e, (field<uint32_t>(e, 0) & 8) ? 0x48 : 0x40);
    *reinterpret_cast<uint8_t*>(e + 2) &= 0xFB;
    ng::cpu_wrote(ptr, 0);
}
using GlyphFn = D1_SYSV void (*)(uint64_t);
GlyphFn o_glyph_upload;
std::atomic<uint64_t> g_glyph_uploads{0}, g_tiled{0};
D1_SYSV void h_glyph_upload(uint64_t self) {
    o_glyph_upload(self);
    const uint64_t c = field<uint64_t>(self, 0);
    if (c < 0x10000) return;
    const int32_t idx = field<int32_t>(c, 0x34);
    if (idx < 0 || idx > 8) return;
    for (int k = 0; k < 2; ++k) {
        const uint64_t t = field<uint64_t>(c, 0x20 + (uint64_t(idx) + k) * 8);
        if (t >= 0x10000) ng::cpu_wrote(tex_base(reinterpret_cast<const uint32_t*>(t)), 0);
    }
    ++g_glyph_uploads;
}
using TileFn = D1_SYSV uint64_t (*)(uint64_t, uint64_t, uint64_t);
TileFn o_tile;
D1_SYSV uint64_t h_tile(uint64_t out, uint64_t in, uint64_t tp) {
    const uint64_t r = o_tile(out, in, tp);
    if (out >= 0x10000) ng::cpu_wrote(out, 1);
    ++g_tiled;
    return r;
}

using RegFn = D1_SYSV void (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
RegFn o_register;
void patch_table() {
    auto slot = [](uint64_t off) { return reinterpret_cast<uint64_t*>(nr::g_base + off); };
    if (*slot(0x254EE38)) *slot(0x254EE38) = uint64_t(&h_bind);
    if (*slot(0x254EE40)) *slot(0x254EE40) = uint64_t(&h_rebind);
    if (*slot(0x254EE48)) *slot(0x254EE48) = uint64_t(&h_release);
    {
        if(*slot(0x254EE88)==nr::g_base+0x7D5520)*slot(0x254EE88)=uint64_t(&h_resident);
        if(*slot(0x254EEA0)==nr::g_base+0x7D55C0)*slot(0x254EEA0)=uint64_t(&h_flags);
    }
}
D1_SYSV void h_register(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h, uint64_t i,
                        uint64_t j, uint64_t k, uint64_t l, uint64_t mm, uint64_t n, uint64_t o, uint64_t p) {
    o_register(a, b, c, d, e, f, g, h, i, j, k, l, mm, n, o, p);
    patch_table();
    log("nd: engine resource callbacks registered: bind / re-point / release wrapped");
}
bool install() {
    bool ok = true;
    nr::resreg::init();
    // Complete source-defined bodies have no retail trampoline.
    auto replace=[&](uint64_t address,const void* fn,const char* name){
        const bool installed=replace_eboot_function(address,fn);
        if(!installed)log("nd: required native replacement %s (eboot+%#llx) failed",name,(unsigned long long)address);
        return installed;
    };
    ok &= replace(0x6FBC0,reinterpret_cast<const void*>(&h_pool_free),"resource_pool_destroy");
    ok &= hook(0xCFCC0, &h_register, o_register, "resource_callbacks_register");
    if (!replace_eboot_function(0x818FA0, reinterpret_cast<const void*>(&h_unlock))) ok = false, log("nd: replacing texture_unlock failed");
    patch_table();
    ok &= replace(0x7D3560,reinterpret_cast<const void*>(&h_copy),"resource_move_copy");
    ok &= replace(0x7D37B0,reinterpret_cast<const void*>(&h_swap),"resource_move_commit");
    const bool mip_hook=replace(0x7D4480,reinterpret_cast<const void*>(&h_mips),"texture_mips");
    const bool handle_hook=replace_eboot_function(0xE01E0,reinterpret_cast<const void*>(&h_mip_handles));
    if(!handle_hook)log("nd: replacing native texture_mip_handle_bounds (eboot+0xe01e0) failed");
    else log("nd: native texture_mip_handle_bounds replacement installed");
    mip_pair_hooked=mip_hook&&handle_hook;ok &= mip_pair_hooked;
    const bool relocation_hook=replace_eboot_function(0xE00D0,reinterpret_cast<const void*>(&h_mip_relocation));
    if(!relocation_hook)log("nd: replacing native texture_mip_relocation_bounds (eboot+0xe00d0) failed");
    ok &= relocation_hook;
    {
        ok &= hook(0xF89BD0,&texture_constructor::calculate_footprint,texture_constructor::original_footprint,"texture_footprint");
        ok &= replace_eboot_function(0xF83530,reinterpret_cast<const void*>(&texture_constructor::build_2d));
        ok &= replace_eboot_function(0xF83AB0,reinterpret_cast<const void*>(&texture_constructor::build_3d));
        ok &= replace_eboot_function(0x7DAA60,reinterpret_cast<const void*>(&h_blit_init));
        ok &= replace_eboot_function(0x7DA8B0,reinterpret_cast<const void*>(&h_blit_reserve));
        ok &= replace_eboot_function(0x7DA910,reinterpret_cast<const void*>(&h_blit_can_request));
        ok &= replace_eboot_function(0x7DA960,reinterpret_cast<const void*>(&h_blit_enqueue));
        ok &= replace_eboot_function(0x7DACF0,reinterpret_cast<const void*>(&h_blit_submit));
        ok &= replace_eboot_function(0x7DAB10,reinterpret_cast<const void*>(&h_blit_complete));
        ok &= replace_eboot_function(0x7DB2D0,reinterpret_cast<const void*>(&h_blit_batch_begin));
        ok &= replace_eboot_function(0x7DB340,reinterpret_cast<const void*>(&h_blit_issue));
        ok &= replace_eboot_function(0x7DB4B0,reinterpret_cast<const void*>(&h_blit_batch_end));
    }
    ok &= hook(0x82B490, &h_tech_a, o_tech_a, "tfx_technique_apply_a");
    ok &= hook(0x82B5C0, &h_tech_b, o_tech_b, "tfx_technique_apply_b");
    ok &= hook(0x82B730, &h_tech_c, o_tech_c, "tfx_technique_apply_c");
    ok &= hook(0x8BE050, &h_glyph_upload, o_glyph_upload, "glyph_cache_upload");
    ok &= hook(0xF8A330, &h_tile, o_tile, "tile_surface");
    log("nd: engine resource events %s", ok ? "hooked" : "NOT HOOKED");
    if (!ok) nr::resreg::g_on = false;
    return ok;
}
}  // namespace resev

// ---------------------------------------------------------------------------- native device layer
namespace nd {
bool g_on = false;
struct Item {
    ng::Draw* d = nullptr;  // null: a constant buffer binding
    nr::CbEvent cb{};
};
struct List {
    uint64_t dev = 0;
    ng::TextureLeaseSet texture_versions;
    std::vector<Item> items;
    std::vector<uint8_t*> chunks;
    size_t used = 0;
    bool eop = false;
    uint64_t made = 0;
};
// 0x7DD060 submits record+0x30 directly. The registry transfers the native
// list once for each record use.
NativeRecordRegistry<List> g_records;
constexpr int kMaxCtx = 16;
std::atomic<uint64_t> g_devs[kMaxCtx];
std::atomic<int> g_ndevs{0};
std::mutex g_devs_m;
struct Ctx { std::atomic<List*> cur{nullptr}; };
Ctx g_ctx[kMaxCtx];
std::atomic<uint64_t> g_lists_made{0}, g_lists_run{0}, g_orphans{0};

uint64_t dev_addr(int i) { return g_devs[i].load(std::memory_order_acquire); }
int ctx_of_dev(uint64_t dev) {
    if (!dev) return -1;
    const int n = g_ndevs.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (g_devs[i].load(std::memory_order_relaxed) == dev) return i;
    std::lock_guard lk(g_devs_m);
    const int m = g_ndevs.load(std::memory_order_relaxed);
    for (int i = n; i < m; ++i)
        if (g_devs[i].load(std::memory_order_relaxed) == dev) return i;
    if (m >= kMaxCtx) return -1;
    g_devs[m].store(dev, std::memory_order_relaxed);
    g_ndevs.store(m + 1, std::memory_order_release);
    log("nd: device context %d at %#llx", m, (unsigned long long)dev);
    return m;
}
int ctx_of_cb(uint64_t cb) {
    const int n = g_ndevs.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        const uint64_t d = g_devs[i].load(std::memory_order_relaxed);
        if (cb == d + 0x60 || cb == d + 0xEC0) return i;
    }
    return -1;
}
List* list_of(int c) {
    if (c < 0) return nullptr;
    List* l = g_ctx[c].cur.load(std::memory_order_relaxed);
    if (!l) {
        l = new List;
        l->dev = dev_addr(c), l->made = gpu_flip_count();
        g_ctx[c].cur.store(l, std::memory_order_relaxed);
    }
    return l;
}
void begin_list(uint64_t dev) {
    const int c = ctx_of_dev(dev);
    if (c < 0) return;
    if (List* old = g_ctx[c].cur.exchange(nullptr)) g_records.abandon(old);
    auto* l = new List;
    l->dev = dev, l->made = gpu_flip_count();
    g_ctx[c].cur.store(l, std::memory_order_relaxed);
    ++g_lists_made;
}
void clear_current(uint64_t dev, List* list) {
    const int c = ctx_of_dev(dev);
    if (c >= 0) g_ctx[c].cur.compare_exchange_strong(list, nullptr);
}

void add(int c, ng::Draw* d) {
    if (List* l = list_of(c)) l->items.push_back({d, {}});
    else nr::release_draw(d);
}

using Dev4Fn = D1_SYSV void (*)(uint64_t,uint64_t,uint64_t,uint64_t);
Dev4Fn o_begin_inherit;
uint64_t native_begin(uint64_t dev);
D1_SYSV uint64_t h_begin(uint64_t dev) {
    return native_begin(dev);
}
D1_SYSV void h_begin_inherit(uint64_t dev, uint64_t rec, uint64_t a, uint64_t saved) {
    o_begin_inherit(dev, rec, a, saved);
    begin_list(dev);
}
template<class T> void put_field(uint64_t base,uint64_t offset,T value) {
    std::memcpy(reinterpret_cast<void*>(base+offset),&value,sizeof value);
}
std::unique_ptr<ng::cbm::Pool> g_cbm_pool;
std::array<uint32_t,ng::cbm::slot_count*2> g_cbm_table{};
uint64_t g_cbm_base=0,g_cbm_manager=0;
uint32_t cbm_sample(uint64_t cbm){
    return __atomic_load_n(reinterpret_cast<uint32_t*>(field<uint64_t>(cbm,0x20)),__ATOMIC_ACQUIRE);
}
void cbm_require(uint64_t cbm){
    if(!g_cbm_pool||cbm!=g_cbm_manager){log("nd: invalid native CBM manager");std::abort();}
}
// The retail CBM still owns chunk indices and list fence bookkeeping.
// Replace its two GPU memory allocations with ordinary PC host memory.
D1_SYSV void h_cbm_init(uint64_t cbm,uint32_t bytes){
    using namespace ng::cbm;
    const auto l=layout(bytes,os::granularity());
    if(!l.bytes||field<uint64_t>(cbm,0)||field<uint64_t>(cbm,0x30)){
        log("nd: invalid or repeated native CBM initialization");std::abort();
    }
    void* memory=nullptr;
    for(uint64_t hint:{0xA80000000ull,0xA00000000ull,0x980000000ull,0x900000000ull}){
        void* p=os::alloc(hint,l.bytes,os::kRead|os::kWrite,false);
        if(p&&uint64_t(p)<(1ull<<40)-l.bytes){memory=p;break;}
        if(p)os::release(p,l.bytes);
    }
    if(!memory){log("nd: native CBM backing allocation failed");std::abort();}
    const uint64_t base=uint64_t(memory);
    register_native_host_region(base,l.bytes);
    {
        if(g_cbm_pool){log("nd: duplicate native CBM pool");std::abort();}
        g_cbm_manager=cbm;g_cbm_base=base;
        g_cbm_pool=std::make_unique<Pool>(uint32_t(bytes/chunk_bytes),field<uint32_t>(cbm,0x1c));
        for(uint32_t i=0;i<slot_count;++i){g_cbm_table[i*2]=0;g_cbm_table[i*2+1]=invalid_chain;}
        put_field(cbm,0,uint64_t(g_cbm_table.data()));
        put_field(cbm,8,uint64_t(0));put_field(cbm,0x18,uint32_t(0));
        put_field(cbm,0x20,base+l.label);put_field(cbm,0x30,uint64_t(0));
        put_field(base,l.label,uint32_t(field<uint32_t>(cbm,0x1c)-1));
        log("nd: native CBM owns %u chunks and all 512 completion slots",uint32_t(bytes/chunk_bytes));
        return;
    }
}
D1_SYSV bool h_cbm_retire(uint64_t cbm){
    cbm_require(cbm);
    return g_cbm_pool->retire(cbm_sample(cbm),[&](uint32_t slot,uint32_t head){
        g_cbm_table[slot*2]=0;g_cbm_table[slot*2+1]=ng::cbm::invalid_chain;
        put_field(cbm,8,head);
    });
}
D1_SYSV void h_cbm_wait(uint64_t cbm){
    cbm_require(cbm);
    while(!g_cbm_pool->empty()&&!h_cbm_retire(cbm))os::sleep_us(30);
}
D1_SYSV void h_cbm_reclaim(uint64_t cbm){
    while(h_cbm_retire(cbm)){}
}
D1_SYSV uint64_t h_cbm_reserve(uint64_t cbm){
    cbm_require(cbm);
    for(;;){
        auto r=g_cbm_pool->reserve([&](ng::cbm::Pool::Reservation r,uint32_t tail,uint32_t seq){
            g_cbm_table[r.slot*2]=ng::cbm::open_value(r.seq);
            g_cbm_table[r.slot*2+1]=ng::cbm::invalid_chain;
            put_field(cbm,0x1c,seq);put_field(cbm,0xc,tail);
        });
        if(r.slot!=UINT32_MAX)return r.slot;
        h_cbm_wait(cbm);
    }
}
D1_SYSV uint64_t h_cbm_chunk(uint64_t cbm,uint32_t bytes,uint32_t alignment,uint64_t rec){
    cbm_require(cbm);
    // All recovered callers request a full 512 KiB chunk, aligned to 128.
    if(bytes!=ng::cbm::chunk_bytes||alignment!=0x80||!rec){
        log("nd: unsupported native CBM allocation (%u, %u)",bytes,alignment);std::abort();
    }
    const uint32_t slot=field<uint32_t>(rec,0x1c);
    if(slot>=ng::cbm::slot_count)std::abort();
    for(uint32_t retry=0;;++retry){
        const auto id=g_cbm_pool->allocate(slot,[&](uint32_t id,uint32_t count){
            // The first 24 bytes retain the original full-chunk allocation
            // bookkeeping. The actual free-list links live in the native pool.
            put_field(rec,8,id);
            if(count==1)put_field(rec,12,id);
            put_field(rec,16,uint32_t(count*ng::cbm::chunk_bytes));
        });
        if(id==ng::cbm::Pool::invalid_slot){log("nd: native CBM allocation on a non-open slot");std::abort();}
        if(id!=ng::cbm::Pool::exhausted)return g_cbm_base+uint64_t(id)*ng::cbm::chunk_bytes;
        if(retry==1500000){log("nd: native CBM exhausted");std::abort();}
        if(!h_cbm_retire(cbm))os::sleep_us(10);
    }
}
D1_SYSV void h_cbm_close(uint64_t cbm,uint32_t emit,uint32_t index,uint64_t rec,uint64_t gfx){
    cbm_require(cbm);
    uint32_t sequence=0;
    if(!g_cbm_pool->close(index,[&](uint32_t seq,uint32_t last){
        sequence=seq;g_cbm_table[index*2+1]=last;g_cbm_table[index*2]=seq;
    })){log("nd: native CBM close without reservation");std::abort();}
    if(!rec||field<uint32_t>(rec,0x1c)!=index){log("nd: native CBM record/slot mismatch");std::abort();}
    if(emit==1){
        using LabelFn=D1_SYSV void(*)(uint64_t,uint32_t,uint32_t,uint64_t,uint32_t,uint64_t,uint32_t,uint32_t);
        reinterpret_cast<LabelFn>(nr::g_base+0xF80C90)(gfx,4,0,field<uint64_t>(cbm,0x20),1,sequence,0,0);
    }
}
D1_SYSV void h_cbm_tick(uint64_t cbm){
    h_cbm_reclaim(cbm);put_field(cbm,0x14,uint32_t(1));
}
D1_SYSV uint32_t h_cbm_block_size(uint64_t cbm){
    cbm_require(cbm);return uint32_t(ng::cbm::chunk_bytes);
}
D1_SYSV void h_cbm_destroy(uint64_t cbm){
    if(!g_cbm_pool)return;
    cbm_require(cbm);
    while(!g_cbm_pool->empty())h_cbm_wait(cbm);
    put_field(cbm,0,uint64_t(0));put_field(cbm,0x30,uint64_t(0));
    g_cbm_pool.reset();g_cbm_base=g_cbm_manager=0;
    // As with the backing-only mode, the mapped bytes are not recycled
    // during this process; delayed completion writes retain their identity.
}
std::mutex g_fifo_m;
std::unordered_map<uint64_t,std::unique_ptr<ng::NativeRecordFifo>> g_fifos;
ng::NativeRecordFifo& native_fifo(uint64_t global) {
    std::lock_guard lock(g_fifo_m);
    auto& fifo=g_fifos[global];
    if(!fifo) {
        const uint32_t head=field<uint32_t>(global,0x2CC),tail=field<uint32_t>(global,0x2C8);
        if(head!=tail){log("nd: cannot adopt nonempty original ring");std::abort();}
        fifo=std::make_unique<ng::NativeRecordFifo>(head);
    }
    return *fifo;
}
void append_record(uint64_t global,uint64_t record,bool ready) {
    auto mirror=[&](uint32_t sequence) {
        const uint32_t slot=ng::record_ring_slot(sequence);
        put_field(global,0x2D0,field<uint32_t>(global,0x2D0)+1);
        put_field(global,0xC8+uint64_t(slot)*8,record);
        put_field(global,0xC0,field<uint32_t>(global,0xC0)+1);
        put_field(record,0x930,slot);
        __atomic_store_n(reinterpret_cast<uint32_t*>(global+0x2C8),sequence+1,__ATOMIC_RELEASE);
        put_field(global,0xB4,field<uint32_t>(global,0xB4)^1u);
    };
    if(!native_fifo(global).reserve(record,ready,mirror)){
        log("nd: native record FIFO full or duplicate reservation");std::abort();
    }
}
D1_SYSV void h_record_segment(uint64_t record,uint64_t context,uint32_t kind,uint32_t hint,uint32_t final_flag){
    if(!ng::record_segment(reinterpret_cast<void*>(record),reinterpret_cast<const void*>(context),kind,hint,final_flag)){
        log("nd: native command segment ledger exhausted");std::abort();
    }
}
void reset_record(uint64_t record) {
    // 0x6AC60 initializes the common 24-byte record header.
    put_field(record,0,uint64_t(0xffffffff80000000ull));
    put_field(record,8,uint64_t(UINT64_MAX));
    put_field(record,0x10,uint64_t(0));
    __atomic_exchange_n(reinterpret_cast<uint32_t*>(record+0x18),0u,__ATOMIC_SEQ_CST);
    put_field(record,0x1C,uint32_t(0));
    put_field(record,0x24,uint32_t(0));
    put_field(record,0x28,uint8_t(0));
    put_field(record,0x730,uint32_t(0));
    put_field(record,0x830,uint32_t(0));
    put_field(record,0x930,uint32_t(0));
    std::memset(reinterpret_cast<void*>(record+0x30),0,0x604);
}
void allocate_record_label(uint64_t record) {
    using LabelFn=D1_SYSV void(*)(uint64_t,uint64_t);
    reinterpret_cast<LabelFn>(nr::g_base+0x7EF890)(nr::g_base+0x26E4840,record+0x1C);
}
uint64_t native_begin(uint64_t dev) {
    const uint64_t global=field<uint64_t>(dev,0);
    const uint32_t index=field<uint32_t>(global,0x2DC);
    // 32 full 0x938-byte records fit between +0x2E0 and +0x132E0.
    if(index>=32){log("nd: main record array exhausted");std::abort();}
    const uint64_t record=global+0x2E0+uint64_t(index)*0x938;
    put_field(global,0x2DC,index+1);
    reset_record(record);
    allocate_record_label(record);
    begin_list(dev);
    put_field(dev,0x58,record);
    using ResetFn=D1_SYSV void(*)(uint64_t,uint32_t);
    reinterpret_cast<ResetFn>(nr::g_base+0x7DC270)(dev,0);
    const uint32_t context=field<uint32_t>(global,0xB4);
    if(context>1){log("nd: invalid native context index");std::abort();}
    const uint64_t base=dev+uint64_t(context)*0xE60;
    put_field(dev,0x50,base+0x60);
    put_field(base,0x225,uint32_t(UINT32_MAX));
    put_field(base,0x229,uint16_t(UINT16_MAX));
    put_field(base,0x22B,uint8_t(UINT8_MAX));
    put_field(base,0x152,uint8_t(UINT8_MAX));
    put_field(base,0x150,uint16_t(UINT16_MAX));
    for(uint64_t off=0x130;off<=0x148;off+=8)put_field(base,off,uint64_t(UINT64_MAX));
    std::memset(reinterpret_cast<void*>(base+0x15A),0xFF,0xC4);
    put_field(record,0x20,context);
    using ContextFn=D1_SYSV void(*)(uint64_t,uint64_t);
    reinterpret_cast<ContextFn>(nr::g_base+0x7EF9C0)(nr::g_base+0x26E4840,dev+0x50);
    return record;
}
D1_SYSV void h_reserve_record(uint64_t dev,uint64_t record) {
    const uint64_t global=field<uint64_t>(dev,0);
    put_field(global,0x2D8,nr::eb<uint8_t>(0x18DCC30));
    reset_record(record);
    append_record(global,record,false);
    allocate_record_label(record);
}
// 01.33 0x7DD060: retain original compatibility fields, but consume
// native record identities rather than importing a Gnm submission. The
// original scheduler lock and cooperative progress helper are still required.
D1_SYSV bool h_drain(uint64_t dev) {
    const uint64_t global=field<uint64_t>(dev,0);
    using LockFn=D1_SYSV void(*)(uint32_t,uint32_t);
    using HelpFn=D1_SYSV bool(*)();
    using TickFn=D1_SYSV uint64_t(*)();
    using FrequencyFn=D1_SYSV int64_t(*)();
    const auto lock=reinterpret_cast<LockFn>(nr::g_base+0xF2C60);
    const auto unlock=reinterpret_cast<LockFn>(nr::g_base+0xF2DC0);
    const auto help=reinterpret_cast<HelpFn>(nr::g_base+0xFB860);
    const auto ticks=reinterpret_cast<TickFn>(nr::g_base+0x68E80);
    const auto frequency=reinterpret_cast<FrequencyFn>(nr::g_base+0x68EA0);
    const bool threaded=field<uint8_t>(global,0x2D8)!=0;
    const uint32_t lock_id=field<uint32_t>(global,0x2D4);
    if(threaded)lock(lock_id,0);
    auto sample_addr=[&](uint32_t slot){return global+0x13310+uint64_t(slot&7)*0x20;};
    const auto status=native_flip_completion();
    if(status.arg>=0) {
        const auto start=uint32_t(field<uint64_t>(global,0x13410))&7;
        for(uint32_t i=0;i<8;++i) {
            const auto addr=sample_addr(start+i);
            if(field<int64_t>(addr,0)!=status.arg)continue;
            if(!field<uint64_t>(addr,0x10)){
                put_field(addr,0x10,status.tsc);
                put_field(addr,0x18,status.queue_count);
                put_field(addr,0x1C,status.pending_count);
            }
            break;
        }
    }
    for(;;){
        uint64_t record=0;
        auto mirror=[&](uint32_t head){
            put_field(global,0xC8+uint64_t(ng::record_ring_slot(head))*8,uint64_t(0));
            put_field(global,0x2CC,head+1);
        };
        record=native_fifo(global).take_ready(mirror);
        if(!record)break;
        for(;;){
            const auto progress=field<uint64_t>(nr::g_base+0x26E4840,0x20);
            if(ng::progress_ready(__atomic_load_n(reinterpret_cast<uint32_t*>(progress),__ATOMIC_ACQUIRE),
                                   field<uint32_t>(global,0xBC)))break;
            if(!help())os::sleep_us(300);
        }
        const bool flip=field<uint8_t>(record,0x28)!=0;
        uint64_t arg=0;
        if(flip){
            const uint64_t stamp=ticks();
            const auto cursor=__atomic_fetch_add(reinterpret_cast<uint64_t*>(global+0x13410),uint64_t(1),__ATOMIC_SEQ_CST);
            arg=__atomic_fetch_add(reinterpret_cast<uint64_t*>(global+0x13418),uint64_t(1),__ATOMIC_SEQ_CST)+1;
            const auto addr=sample_addr(uint32_t(cursor));
            put_field(addr,0,arg);put_field(addr,8,stamp);
            put_field(addr,0x10,uint64_t(0));put_field(addr,0x18,uint64_t(0));
            const auto start=uint32_t(field<uint64_t>(global,0x13410))&7;
            for(uint32_t i=0;i<8;++i){
                const auto sample=sample_addr(start+i);
                const uint64_t finished=field<uint64_t>(sample,0x10);
                const int64_t id=field<int64_t>(sample,0);
                if(!finished||!ng::flip_sample_newer(id,field<int64_t>(global,0x13420)))continue;
                if(!field<uint32_t>(global,0x13428))put_field(global,0x13428,uint32_t(1));
                else {
                    const auto hz=frequency();
                    if(hz>0&&double(finished-field<uint64_t>(sample,8))/double(hz)>57.0)
                        put_field(global,0x13428,uint32_t(0));
                }
                put_field(global,0x13430,finished);
                put_field(global,0x13420,id);
            }
            native_flip_rate_hint(field<uint32_t>(global,0x13428));
        }
        native_queue_submit(native_device_capture_record(record),flip,
                            int32_t(field<uint32_t>(global,0xB0)),int64_t(arg));
    }
    if(threaded)unlock(lock_id,0);
    return true;
}
// Source-defined close for ordinary and worker command lists. The engine
// still owns its ordered ring and its progress labels, but native work does
// not need a fake nonzero DCB extent. Preserve any genuine segment metadata.
void native_close(uint64_t dev,uint32_t kind,bool worker) {
    const int c=ctx_of_dev(dev);
    const uint64_t global=field<uint64_t>(dev,0),gfx=field<uint64_t>(dev,0x50),rec=field<uint64_t>(dev,0x58);
    if(c<0||!global||!gfx||!rec) {
        log("nd: invalid native close context");
        std::abort();
    }
    List* list=list_of(c);
    const uint64_t label_state=nr::g_base+0x26E4840;
    const uint32_t label_index=field<uint32_t>(rec,0x1C);
    const auto plan=ng::close_plan(kind,nr::eb<uint8_t>(0x26DF291),worker);
    // 0x7EF3A0 updates the engine's dependency label and invokes the
    // already-native EOP callback before the record is made ready.
    using CloseLabel=D1_SYSV void(*)(uint64_t,uint32_t,uint32_t,uint64_t,uint64_t);
    reinterpret_cast<CloseLabel>(nr::g_base+0x7EF3A0)(label_state,plan.label_flag,label_index,rec,gfx);
    uint32_t completed=0,previous=0;
    if(worker) {
        previous=field<uint32_t>(global,0xBC);
        completed=field<uint32_t>(field<uint64_t>(label_state,0x20),0);
    }
    if(plan.update_progress) {
        put_field(global,0xBC,field<uint32_t>(global,0xB8));
        put_field(global,0xB8,field<uint32_t>(field<uint64_t>(label_state,0),uint64_t(label_index)*8));
    }
    // Preserve actual ranges for remaining engine readers. No synthetic
    // command extent is required for native queue publication.
    h_record_segment(rec,gfx,plan.segment_kind,plan.segment_hint,1);
    put_field(gfx,0,field<uint64_t>(gfx,0x10));
    put_field(gfx,0x80,field<uint64_t>(gfx,0x90));
    if(!worker)put_field(rec,0x28,uint8_t(plan.flip));
    // All close-time events are captured now. Publish before either the
    // already-enqueued worker record or the main ring entry becomes ready.
    if(!g_records.publish(rec,list)) {
        log("nd: native close record reused before submission (%#llx)",(unsigned long long)rec);
        std::abort();
    }
    clear_current(dev,list);
    auto ready=[&]{__atomic_exchange_n(reinterpret_cast<uint32_t*>(rec+0x18),1u,__ATOMIC_SEQ_CST);};
    if(plan.append_ring){ready();append_record(global,rec,true);}
    else {
        if(!native_fifo(global).publish(rec,ready)){
            log("nd: worker closed without a native FIFO reservation");std::abort();
        }
    }
    put_field(dev,0x50,uint64_t(0));
    put_field(dev,0x58,uint64_t(0));
    // Worker records were enqueued by 0x7DEA90 before recording started.
    if(worker&&ng::worker_should_submit(completed,previous,nr::eb<uint8_t>(0x18DCC30)))
        h_drain(dev);
}
D1_SYSV void h_end(uint64_t dev, uint64_t, uint64_t kind) {
    native_close(dev,uint32_t(kind),false);
}
D1_SYSV void h_end_submit(uint64_t dev) {
    native_close(dev,0,true);
}

D1_SYSV uint64_t s_noop() { return 0; }
constexpr uint64_t kArenaChunk = 256 << 10, kArenaSize = 1ull << 30;
std::mutex g_arena_m;
uint8_t* g_arena = nullptr;
std::vector<uint32_t> g_arena_free;
uint8_t* arena_chunk() {
    std::lock_guard lk(g_arena_m);
    if (!g_arena) {
        for (uint64_t at : {0xF80000000ull, 0xF00000000ull, 0xE80000000ull, 0xE00000000ull, 0xD00000000ull}) {
            void* p = os::alloc(at, kArenaSize, os::kRead | os::kWrite, true);
            if (p == reinterpret_cast<void*>(at)) { g_arena = static_cast<uint8_t*>(p); break; }
            if (p) os::release(p, kArenaSize);
        }
        if (!g_arena) { log("nd: no command-buffer arena below 1 TiB"); return nullptr; }
        register_native_host_region(uint64_t(g_arena), kArenaSize);
        for (uint32_t i = uint32_t(kArenaSize / kArenaChunk); i-- > 0;) g_arena_free.push_back(i);
        log("nd: command-buffer arena at %p (%llu MiB)", (void*)g_arena, (unsigned long long)(kArenaSize >> 20));
    }
    if (g_arena_free.empty()) return nullptr;
    const uint32_t i = g_arena_free.back();
    g_arena_free.pop_back();
    return g_arena + uint64_t(i) * kArenaChunk;
}
void arena_free(uint8_t* p) {
    std::lock_guard lk(g_arena_m);
    g_arena_free.push_back(uint32_t(uint64_t(p - g_arena) / kArenaChunk));
}
std::mutex g_global_alloc_m;
std::vector<uint8_t*> g_global_alloc;
D1_SYSV void* s_alloc(uint64_t cb, uint32_t bytes, uint32_t align_log2) {
    const uint64_t align = uint64_t(1) << std::min(align_log2, 12u);
    const int c = ctx_of_cb(cb);
    List* l = list_of(c);
    const size_t need = size_t(bytes) + align;
    if (need > kArenaChunk) return nullptr;
    if (!l) {
        std::lock_guard lk(g_global_alloc_m);
        static uint8_t* cur = nullptr;
        static size_t used = 0;
        if (!cur || used + need > kArenaChunk) {
            uint8_t* ch = arena_chunk();
            if (!ch) return nullptr;
            g_global_alloc.push_back(ch), cur = ch, used = 0;
        }
        const uint64_t p = (uint64_t(cur) + used + align - 1) & ~(align - 1);
        used = size_t(p - uint64_t(cur)) + bytes;
        return reinterpret_cast<void*>(p);
    }
    if (l->chunks.empty() || l->used + need > kArenaChunk) {
        uint8_t* ch = arena_chunk();
        if (!ch) return nullptr;
        l->chunks.push_back(ch);
        l->used = 0;
    }
    const uint64_t base = uint64_t(l->chunks.back());
    const uint64_t p = (base + l->used + align - 1) & ~(align - 1);
    l->used = size_t(p - base) + bytes;
    return reinterpret_cast<void*>(p);
}
D1_SYSV void s_eop_label(uint64_t cb, uint32_t, uint32_t, uint64_t addr, uint32_t data_sel, uint64_t data, uint32_t,
                         uint32_t int_sel) {
    List* l = list_of(ctx_of_cb(cb));
    if (!l) return;
    if (addr >= 0x10000 && (data_sel == 1 || data_sel == 2 || data_sel == 3)) {
        ng::Draw* d = nr::alloc_draw();
        d->kind = ng::Draw::WriteData;
        d->prim = ng::kEopLabel;
        d->caller = addr;
        d->inline_vertices.resize(data_sel == 1 ? 4 : 8);
        d->count = data_sel == 3;
        std::memcpy(d->inline_vertices.data(), &data, d->inline_vertices.size());
        l->items.push_back({d, {}});
    }
    if (int_sel) l->eop = true;
}
D1_SYSV void s_eop_interrupt(uint64_t cb) {
    if (List* l = list_of(ctx_of_cb(cb))) l->eop = true;
}
D1_SYSV void s_zpass(uint64_t cb, uint32_t end, uint64_t results) {
    const int c = ctx_of_cb(cb);
    if (c < 0 || results < 0x10000) return;
    ng::Draw* d = nr::alloc_draw();
    d->kind = ng::Draw::ZPass;
    d->index_addr = end == 1 ? results + 8 : results;
    add(c, d);
}
D1_SYSV void s_pred_on(uint64_t cb, uint64_t addr, uint32_t, uint32_t visible) {
    *reinterpret_cast<uint8_t*>(cb + 0x28) |= 1;
    const int c = ctx_of_cb(cb);
    if (c < 0) return;
    ng::Draw* d = nr::alloc_draw();
    d->kind = ng::Draw::PredOn;
    d->index_addr = addr & ~15ull, d->count = visible & 1;
    add(c, d);
}
D1_SYSV void s_pred_off(uint64_t cb) {
    *reinterpret_cast<uint8_t*>(cb + 0x28) &= ~1;
    const int c = ctx_of_cb(cb);
    if (c < 0) return;
    ng::Draw* d = nr::alloc_draw();
    d->kind = ng::Draw::PredOff;
    add(c, d);
}
D1_SYSV void s_dma(uint64_t cb, uint64_t dst, uint64_t src, uint32_t bytes) {
    const int c = ctx_of_cb(cb);
    if (c < 0 || !bytes) return;
    ng::Draw* d = nr::alloc_draw();
    d->kind = ng::Draw::Dma;
    d->index_addr = src, d->caller = dst, d->count = bytes, d->prim = 0, d->index32 = 0;
    d->dma_checked=true;
    guest_capture_move_spans(dst,src,bytes,d->dma_dst_spans,d->dma_src_spans);
    add(c, d);
}
D1_SYSV void s_set_cbs(uint64_t cue, uint32_t stage, uint32_t first, uint32_t count, const uint32_t* vs) {
    const int s = nr::gnm_stage_index(stage);
    if (s < 0) return;
    nr::CueState* cst = nr::cue_state(cue, true);
    List* l = list_of(ctx_of_cb(cue - 0xC0));
    for (uint32_t k = 0; k < count && first + k < 16; ++k) {
        ng::Draw::Cb& cbv = cst->cb[s][first + k];
        if (!vs) {
            cbv = {};
        } else {
            const uint32_t* v = vs + 4 * k;
            const uint64_t base = uint64_t(v[0]) | uint64_t(v[1] & 0xFFF) << 32;
            const uint32_t stride = v[1] >> 16 & 0x3FFF, records = v[2];
            const uint64_t bytes = stride ? uint64_t(stride) * records : records;
            cbv = {base, uint32_t(std::min<uint64_t>(bytes, 1u << 20))};
        }
        if (l) l->items.push_back({nullptr, nr::CbEvent{0, 0, uint8_t(s), uint8_t(first + k), cbv}});
    }
}

std::mutex g_retired_m;
std::deque<std::pair<uint64_t, std::vector<uint8_t*>>> g_retired;
void retire_chunks(std::vector<uint8_t*>&& cs) {
    const uint64_t now = gpu_flip_count();
    std::lock_guard lk(g_retired_m);
    // Constants can remain inherited across arbitrarily many flips. Do not
    // recycle their backing chunks solely because three frames have elapsed.
    // Other captured pointers retain the existing conservative grace period;
    // full capture ownership will replace it in the native allocator.
    auto inherited = [](uint8_t* chunk) {
        const uint64_t base = uint64_t(chunk);
        for (int s = 0; s < ng::kStages; ++s)
            for (int i = 0; i < 16; ++i) {
                const uint64_t addr = nr::g_ce[s][i].addr;
                if (nr::g_ce_known[s][i] && addr >= base && addr - base < kArenaChunk)
                    return true;
            }
        return false;
    };
    for (auto it = g_retired.begin(); it != g_retired.end();) {
        if (it->first + 3 > now) break;
        auto& chunks = it->second;
        for (size_t i = 0; i < chunks.size();) {
            if (inherited(chunks[i])) { ++i; continue; }
            arena_free(chunks[i]);
            chunks[i] = chunks.back();
            chunks.pop_back();
        }
        if (chunks.empty()) it = g_retired.erase(it);
        else ++it;
    }
    if (!cs.empty()) g_retired.emplace_back(now, std::move(cs));
}
void free_list(List* l) {
    retire_chunks(std::move(l->chunks));
    std::vector<ng::Draw*> ds;
    for (Item& it : l->items)
        if (it.d) ds.push_back(it.d);
    nr::release_draws(ds);
    delete l;
}


bool install() {
    bool ok = resev::install();
    auto rep = [&](uint64_t off, const void* fn) {
        if (!replace_eboot_function(off, fn)) ok = false, log("nd: replacing eboot+%#llx failed", (unsigned long long)off);
    };
    static const uint32_t kNoops[] = {
        0xF7E740, 0xF7E7C0, 0xF7E840, 0xF7E8D0, 0xF7E950, 0xF7E9B0, 0xF7EA20, 0xF7EA80, 0xF7EAF0, 0xF7EB50, 0xF7EBB0,
        0xF7EC30, 0xF7ED40, 0xF7EEC0, 0xF7F1E0, 0xF7F260, 0xF7F320, 0xF7F4A0, 0xF7F790, 0xF7F7E0, 0xF7F840, 0xF7F890,
        0xF7FA90, 0xF7FCD0, 0xF7FD40, 0xF7FE20, 0xF80340, 0xF80440, 0xF804F0, 0xF80550, 0xF805C0, 0xF80640, 0xF806D0,
        0xF80730, 0xF80790, 0xF80830, 0xF808A0, 0xF80910, 0xF80E00, 0xF80F10, 0xF81090, 0xF81220,
        0xF7E5E0, 0xF84DB0, 0xF811C0,
        0xF8AFD0, 0xF8B0C0, 0xF8B1F0, 0xF8B2E0, 0xF8B410, 0xF8B600, 0xF8B790, 0xF8B8C0, 0xF8B980, 0xF8DF60, 0xF8E680,
        0xF8E880, 0xF8F0E0, 0xF8F4B0};
    for (uint32_t off : kNoops) rep(off, reinterpret_cast<const void*>(&s_noop));
    rep(0xF7FEB0, reinterpret_cast<const void*>(&s_alloc));
    rep(0xF80C90, reinterpret_cast<const void*>(&s_eop_label));
    rep(0xF80D70, reinterpret_cast<const void*>(&s_eop_interrupt));
    rep(0xF80AE0, reinterpret_cast<const void*>(&s_zpass));
    rep(0xF809F0, reinterpret_cast<const void*>(&s_pred_on));
    rep(0xF80A80, reinterpret_cast<const void*>(&s_pred_off));
    rep(0xF8F3A0, reinterpret_cast<const void*>(&s_dma));
    rep(0xF8B500, reinterpret_cast<const void*>(&s_set_cbs));
    rep(0x7DB810,reinterpret_cast<const void*>(&h_record_segment));
    rep(0x7DCCE0,reinterpret_cast<const void*>(&h_begin));
    ok &= hook(0x7DD6A0, &h_begin_inherit, o_begin_inherit, "cmdlist_begin_inherit");
    rep(0x7DCEC0,reinterpret_cast<const void*>(&h_end));
    rep(0x7DE950,reinterpret_cast<const void*>(&h_end_submit));
    rep(0x7DD060,reinterpret_cast<const void*>(&h_drain));
    rep(0x7DEA90,reinterpret_cast<const void*>(&h_reserve_record));
    rep(0x7EF7D0,reinterpret_cast<const void*>(&h_cbm_init));
    {
        rep(0x7EF180,reinterpret_cast<const void*>(&h_cbm_wait));
        rep(0x7EF2A0,reinterpret_cast<const void*>(&h_cbm_retire));
        rep(0x7EEF90,reinterpret_cast<const void*>(&h_cbm_reclaim));
        rep(0x7EF460,reinterpret_cast<const void*>(&h_cbm_reserve));
        rep(0x7EF8B0,reinterpret_cast<const void*>(&h_cbm_chunk));
        rep(0x7EF3A0,reinterpret_cast<const void*>(&h_cbm_close));
        rep(0x7EF630,reinterpret_cast<const void*>(&h_cbm_tick));
        rep(0x7EFC00,reinterpret_cast<const void*>(&h_cbm_block_size));
        rep(0x7EF5C0,reinterpret_cast<const void*>(&h_cbm_destroy));
    }
    g_on = ok;
    log("nd: native device layer: %zu Gnm entry points replaced%s",
        sizeof kNoops / sizeof kNoops[0] + 8, ok ? "" : " - REQUIRED REPLACEMENTS FAILED");
    return ok;
}
}  // namespace nd

ng::TextureLeaseSet* nd_texture_leases(uint64_t dev){
    if(!nd::g_on)return nullptr;
    auto* list=nd::list_of(nd::ctx_of_dev(dev));
    return list?&list->texture_versions:nullptr;
}

bool nd_push(uint64_t dev, ng::Draw* d) {
    if (!nd::g_on) return false;
    d->frame = uint32_t(gpu_flip_count());
    nd::add(nd::ctx_of_dev(dev), d);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------- write tracking
namespace wt {
constexpr uint64_t kPage = 4096, kChunkBits = 30, kPagesPerChunk = 1ull << (kChunkBits - 12), kChunks = 128;
constexpr uint64_t kBlockPages = 16;
constexpr uint64_t kCoarsePages = 256;
enum : uint8_t { kOff = 0, kProtected = 1, kUnprotecting = 2 };
struct Chunk {
    std::atomic<uint32_t> gen[kPagesPerChunk];
    std::atomic<uint32_t> coarse[kPagesPerChunk / kCoarsePages];
    std::atomic<uint8_t> state[kPagesPerChunk];
    std::atomic<uint8_t> watched[kPagesPerChunk];
};
std::atomic<Chunk*> g_chunks[kChunks];
std::mutex g_alloc_m;
std::atomic<uint64_t> g_faults{0};
const bool g_log_faults = getenv("D1_NG_LOG_FAULTS") != nullptr || getenv("D1_NG_CENSUS") != nullptr;
std::mutex g_census_m;
std::unordered_map<uint64_t, uint64_t> g_census;
void print_census() {
    std::vector<std::pair<uint64_t, uint64_t>> v;
    {
        std::lock_guard lk(g_census_m);
        v.assign(g_census.begin(), g_census.end());
        g_census.clear();
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::string out;
    for (size_t i = 0; i < v.size() && i < 16; ++i) {
        char b[48];
        std::snprintf(b, sizeof b, " %llx:%llu", (unsigned long long)(v[i].first << 16), (unsigned long long)v[i].second);
        out += b;
    }
    log("native gfx: write faults by 64 KiB region (%zu regions):%s", v.size(), out.c_str());
}

Chunk* chunk(uint64_t page, bool create) {
    const uint64_t c = page >> (kChunkBits - 12);
    if (c >= kChunks) return nullptr;
    Chunk* ch = g_chunks[c].load(std::memory_order_acquire);
    if (ch || !create) return ch;
    std::lock_guard lk(g_alloc_m);
    ch = g_chunks[c].load(std::memory_order_acquire);
    if (!ch) {
        ch = new Chunk();
        g_chunks[c].store(ch, std::memory_order_release);
    }
    return ch;
}

uint64_t fine_version(uint64_t addr, uint64_t size) {
    uint64_t v = 0;
    for (uint64_t pg = addr / kPage, end = (addr + size + kPage - 1) / kPage; pg < end; ++pg) {
        Chunk* ch = chunk(pg, false);
        if (!ch) return ~0ull;
        const uint64_t i = pg & (kPagesPerChunk - 1);
        if (ch->state[i].load(std::memory_order_acquire) != kProtected) return ~0ull;
        v += ch->gen[i].load(std::memory_order_relaxed);
    }
    return v;
}

uint64_t coarse_sum(uint64_t addr, uint64_t size) {
    uint64_t v = 0;
    const uint64_t first = addr / kPage, end = (addr + size + kPage - 1) / kPage;
    for (uint64_t pg = first & ~(kCoarsePages - 1); pg < end; pg += kCoarsePages) {
        Chunk* ch = chunk(pg, false);
        if (!ch) return ~0ull;
        v += ch->coarse[(pg & (kPagesPerChunk - 1)) / kCoarsePages].load(std::memory_order_acquire);
    }
    return v;
}

struct VCached { uint64_t addr, size, coarse, version; };
thread_local std::unordered_map<uint64_t, VCached> g_vcache;
uint64_t version(uint64_t addr, uint64_t size) {
    const uint64_t c = coarse_sum(addr, size);
    if (c == ~0ull) return ~0ull;
    const uint64_t key = (addr * 0x9E3779B97F4A7C15ull) ^ size;
    auto it = g_vcache.find(key);
    if (it != g_vcache.end() && it->second.addr == addr && it->second.size == size && it->second.coarse == c)
        return it->second.version;
    const uint64_t v = fine_version(addr, size);
    if (v == ~0ull) {
        if (it != g_vcache.end()) g_vcache.erase(it);
        return v;
    }
    if (g_vcache.size() > (1u << 20)) g_vcache.clear();
    g_vcache[key] = {addr, size, c, v};
    return v;
}

void describe(uint64_t addr, char* out, size_t n) {
    const uint64_t pg = addr / kPage;
    Chunk* ch = chunk(pg, false);
    if (!ch) {
        std::snprintf(out, n, "no chunk");
        return;
    }
    const uint64_t i = pg & (kPagesPerChunk - 1);
    std::snprintf(out, n, "state %u gen %llu watched %u", unsigned(ch->state[i].load()),
                  (unsigned long long)ch->gen[i].load(), unsigned(ch->watched[i].load()));
}

uint64_t watch(uint64_t addr, uint64_t size) {
    const uint64_t first = addr / kPage, end = (addr + size + kPage - 1) / kPage;
    uint64_t run = 0, run_len = 0;
    auto flush = [&] {
        if (run_len) os::protect(reinterpret_cast<void*>(run * kPage), run_len * kPage, os::kRead);
        run_len = 0;
    };
    for (uint64_t pg = first; pg < end; ++pg) {
        Chunk* ch = chunk(pg, true);
        if (!ch) return ~0ull;
        const uint64_t i = pg & (kPagesPerChunk - 1);
        uint8_t expect = kOff;
        if (ch->state[i].compare_exchange_strong(expect, kProtected)) {
            ch->watched[i].store(1, std::memory_order_relaxed);
            if (run_len && run + run_len == pg) ++run_len;
            else flush(), run = pg, run_len = 1;
        } else if (expect == kUnprotecting) {
            flush();
            return ~0ull;
        }
    }
    flush();
    return version(addr, size);
}

void forget(uint64_t addr, uint64_t size) {
    for (uint64_t pg = addr / kPage, end = (addr + size + kPage - 1) / kPage; pg < end; ++pg) {
        Chunk* ch = chunk(pg, false);
        if (!ch) {
            pg |= kPagesPerChunk - 1;
            continue;
        }
        const uint64_t i = pg & (kPagesPerChunk - 1);
        ch->gen[i].fetch_add(1, std::memory_order_relaxed);
        ch->coarse[i / kCoarsePages].fetch_add(1, std::memory_order_release);
        ch->state[i].store(kOff, std::memory_order_release);
    }
}

bool on_fault(uint64_t addr) {
    const uint64_t pg = addr / kPage;
    Chunk* ch = chunk(pg, false);
    if (!ch) return false;
    const uint64_t i = pg & (kPagesPerChunk - 1);
    uint8_t expect = kProtected;
    if (ch->state[i].compare_exchange_strong(expect, kUnprotecting)) {
        const uint64_t blk = i & ~uint64_t(kBlockPages - 1);
        uint32_t taken = 1u << (i - blk);
        for (uint64_t k = 0; k < kBlockPages; ++k) {
            if (blk + k == i) continue;
            uint8_t e = kProtected;
            if (ch->state[blk + k].compare_exchange_strong(e, kUnprotecting)) taken |= 1u << k;
        }
        for (uint64_t k = 0; k < kBlockPages; ++k)
            if (taken >> k & 1) ch->gen[blk + k].fetch_add(1, std::memory_order_relaxed);
        ch->coarse[blk / kCoarsePages].fetch_add(1, std::memory_order_release);
        for (uint64_t k = 0; k < kBlockPages;) {
            if (!(taken >> k & 1)) {
                ++k;
                continue;
            }
            uint64_t e = k;
            while (e < kBlockPages && (taken >> e & 1)) ++e;
            const uint64_t first = (pg & ~(kPagesPerChunk - 1)) + blk + k;
            os::protect(reinterpret_cast<void*>(first * kPage), (e - k) * kPage, os::kRead | os::kWrite);
            k = e;
        }
        for (uint64_t k = 0; k < kBlockPages; ++k)
            if (taken >> k & 1) ch->state[blk + k].store(kOff, std::memory_order_release);
        g_faults.fetch_add(1, std::memory_order_relaxed);
        if (g_log_faults) {  // D1_NG_LOG_FAULTS: which 64 KiB regions keep faulting
            std::lock_guard lk(g_census_m);
            ++g_census[pg >> 4];
        }
        return true;
    }
    // another thread is unprotecting it (or just did): retry the access
    if (expect == kUnprotecting) {
        while (ch->state[i].load(std::memory_order_acquire) == kUnprotecting) {}
        return true;
    }
    return ch->watched[i].load(std::memory_order_relaxed) != 0;
}
}  // namespace wt

// D1_NG_LOG_FAULTS: per writing code address, the faults and a sample address (what the
// renderer keeps there is named in the periodic census)
namespace faultlog {
std::mutex m;
struct Site { uint64_t n = 0, sample = 0, lo = ~0ull, hi = 0; };
struct Key {
    uint64_t rip, c1, c2;
    bool operator<(const Key& o) const { return std::tie(rip, c1, c2) < std::tie(o.rip, o.c1, o.c2); }
};
std::map<Key, Site> sites;
// D1_NG_CENSUS=<file>: every writer since the start (not only the last interval's top 24), with
// the two guest callers above it (frame pointers) and the range of addresses it wrote
const char* const g_census = getenv("D1_NG_CENSUS");
static uint64_t rel(uint64_t a) { return a >= nr::g_base && a < nr::g_base + 0x4000000 ? a - nr::g_base : a | 1ull << 63; }
void note(void* uc, uint64_t addr) {
    uint64_t rip = 0, c1 = 0, c2 = 0;
#if defined(__linux__)
    const auto& gr = static_cast<ucontext_t*>(uc)->uc_mcontext.gregs;
    rip = uint64_t(gr[REG_RIP]);
    if (g_census) {  // the guest's frame chain (the engine keeps frame pointers)
        uint64_t bp = uint64_t(gr[REG_RBP]);
        const uint64_t sp = uint64_t(gr[REG_RSP]);
        uint64_t* out[2] = {&c1, &c2};
        // (a frame pointer is trusted while it stays on this thread's stack, above the stack
        // pointer; memcpy keeps none, so the first frame is its caller's)
        for (int k = 0, got = 0; k < 24 && got < 2 && !(bp & 7) && bp >= sp && bp < sp + (4u << 20); ++k) {
            const uint64_t ret = reinterpret_cast<const uint64_t*>(bp)[1];
            const uint64_t next = reinterpret_cast<const uint64_t*>(bp)[0];
            if (ret >= nr::g_base && ret < nr::g_base + 0x4000000) *out[got++] = ret - nr::g_base;
            if (next <= bp) break;
            bp = next;
        }
    }
#else
    (void)uc;
#endif
    std::lock_guard lk(m);
    Site& s = sites[{rel(rip), c1, c2}];
    if (!s.n++) s.sample = addr;
    s.lo = std::min(s.lo, addr), s.hi = std::max(s.hi, addr);
}
void print() {
    std::vector<std::pair<Key, Site>> v;
    {
        std::lock_guard lk(m);
        v.assign(sites.begin(), sites.end());
        if (!g_census) sites.clear();
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.n > b.second.n; });
    FILE* f = g_census ? std::fopen(g_census, "w") : nullptr;
    for (size_t i = 0; i < v.size() && (f || i < 24); ++i) {
        char owner[160] = "";
        ng::describe_owner(v[i].second.sample, owner, sizeof owner);
        char where[200] = "";
        const uint64_t rip = v[i].first.rip;
#if defined(__linux__)
        if (rip >> 63) {
            Dl_info di{};
            const uint64_t a = rip & ~(1ull << 63);
            if (dladdr(reinterpret_cast<void*>(a), &di) && di.dli_fname)
                std::snprintf(where, sizeof where, " [%s+%#llx %s]", di.dli_fname, (unsigned long long)(a - uint64_t(di.dli_fbase)),
                              di.dli_sname ? di.dli_sname : "");
        }
#endif
        if (f)
            std::fprintf(f, "%s%#llx%s <- %#llx <- %#llx | %llu faults | %#llx..%#llx | e.g. %#llx: %s\n", rip >> 63 ? "host " : "eboot+",
                         (unsigned long long)(rip & ~(1ull << 63)), where, (unsigned long long)v[i].first.c1, (unsigned long long)v[i].first.c2,
                         (unsigned long long)v[i].second.n, (unsigned long long)v[i].second.lo, (unsigned long long)v[i].second.hi,
                         (unsigned long long)v[i].second.sample, owner);
        else
            log("native gfx: write faults from %s%#llx%s: %llu (e.g. %#llx: %s)", rip >> 63 ? "host " : "eboot+",
                (unsigned long long)(rip & ~(1ull << 63)), where, (unsigned long long)v[i].second.n, (unsigned long long)v[i].second.sample, owner);
    }
    if (f) std::fclose(f);
}
}  // namespace faultlog
bool native_gfx_on_fault(void* uc, void* addr) {
    if (!nr::g_base || !ng::active() || !wt::on_fault(uint64_t(addr))) return false;
    if (wt::g_log_faults) faultlog::note(uc, uint64_t(addr));
    return true;
}
bool native_gfx_active() { return nr::g_base && ng::active(); }
void native_gfx_on_unmap(uint64_t addr, uint64_t size) {
    if (native_gfx_active()) wt::forget(addr, size);
}
bool native_gfx_research_fault(void*, void*) { return false; }

bool native_gfx_install() {
    const char* mode = getenv("D1_NATIVE_GFX");
    if (mode && std::strcmp(mode, "render") != 0) {
        log("native gfx: compatibility GPU mode is no longer supported; native Vulkan is required");
        return false;
    }
    nr::g_base = rt().modules[0].base;
    if (getenv("D1_NG_EVERY") || getenv("D1_NG_EVERY_WORLD"))
        log("native gfx: legacy frame-skipping controls removed; all frames use the native device");
    if (const char* v = getenv("D1_NG_DUMP_EVERY")) nr::g_dump_every = std::max(0, std::atoi(v));
    ng::Options opt;
    opt.frames_dir = getenv("D1_NG_FRAMES");
    opt.no_cull = getenv("D1_NG_NOCULL") != nullptr;
    opt.dump_targets = getenv("D1_NG_TARGETS") != nullptr;
    if(getenv("D1_NG_COMPARE_LAYOUT"))opt.compare_layout=&resev::texture_constructor::compare_native_layout;
    const Settings& st = settings();
    opt.present_mode = st.vsync == "on" ? "Fifo" : st.vsync == "off-tearing" ? "Immediate" : "Mailbox";
    static std::string cache_file;
    cache_file = rt().data_root + "/shader_cache";
    {
        std::error_code ec;
        std::filesystem::create_directories(cache_file, ec);
    }
    cache_file += "/native_pipelines.bin";
    opt.pipeline_cache = cache_file.c_str();
    opt.watch = [](uint64_t a, uint64_t n) { return wt::watch(a, n); };
    opt.version = [](uint64_t a, uint64_t n) { return wt::version(a, n); };
    opt.res_current = [](uint64_t obj) { return nr::resreg::get(obj); };
    opt.describe = [](uint64_t a, char* out, size_t n) { wt::describe(a, out, n); };
    if (const char* v = getenv("D1_NG_FRAMES_IN_FLIGHT")) opt.frames_in_flight = uint32_t(std::max(1, std::atoi(v)));
#ifdef D1_FRONTEND_SDL
    if (frontend::open(st.window_width, st.window_height, st.fullscreen)) {
        opt.window_handles = [](ng::WindowHandles& out) {
            frontend::Handles h;
            if (!frontend::handles(h)) return false;
            out.type = h.type == frontend::Handles::kWin32 ? ng::WindowHandles::kWin32 : ng::WindowHandles::kXlib;
            out.display = h.display;
            out.window = h.window;
            return true;
        };
        opt.window_size = [](uint32_t& w, uint32_t& h) { frontend::drawable_size(w, h); };
    }
#endif
    if (!ng::init(opt)) {
        log("native gfx: renderer unavailable; not capturing");
        if (opt.window_handles)
            os::error_box("The renderer could not start, so nothing can be drawn.\n\nIt needs a GPU driver with "
                          "Vulkan 1.4 (update the graphics driver). Details are in d1-native.log.");
        return false;
    }
    bool ok = true;
    ok &= replace_eboot_function(0x7E15D0,reinterpret_cast<const void*>(&r_draw_indexed));
    ok &= replace_eboot_function(0x7E17C0,reinterpret_cast<const void*>(&r_draw));
    ok &= replace_eboot_function(0x7E1710,reinterpret_cast<const void*>(&r_draw_instanced));
    ok &= replace_eboot_function(0x7E16A0,reinterpret_cast<const void*>(&r_draw_indexed_instanced));
    ok &= replace_eboot_function(0x7E1840,reinterpret_cast<const void*>(&r_draw_quads));
    ok &= replace_eboot_function(0x7E1960,reinterpret_cast<const void*>(&r_draw_inline));
    ok &= hook(0x7E2A00, &r_clear, o_clear, "clear");
    ok &= replace_eboot_function(0xF7CF90,reinterpret_cast<const void*>(&nr::build_vsharp));
    ok &= replace_eboot_function(0xF7D180,reinterpret_cast<const void*>(&nr::build_csharp));
    ok &= replace_eboot_function(0xF7D010,reinterpret_cast<const void*>(&nr::set_buffer_memory_type));
    ok &= replace_eboot_function(0x7DF630,reinterpret_cast<const void*>(&r_alloc_constants));
    ok &= replace_eboot_function(0x7DEC20,reinterpret_cast<const void*>(&r_set_vertex_buffer));
    ok &= replace_eboot_function(0x7DEE30,reinterpret_cast<const void*>(&r_set_index_buffer));
    ok &= hook(0x7E0780, &r_dispatch, o_dispatch, "dispatch");
    ok &= asset_backing::install();
    ok &= nd::install();
    log("native gfx: %s", ok ? "native device ready" : "required native device hooks failed");
    return ok;
}

bool native_device_active() { return nd::g_on; }
void gnm_signal_eop();  // hle_gpu.cpp
// The queue owns detached native lists, not pointers into the engine's
// reusable console-shaped command buffers. The retail record address is the boundary key.
struct NativeSubmission {
    std::vector<nd::List*> lists;
    std::unordered_map<const ng::Draw*, uint64_t> completion_mappings;
};
static NativeSubmission* capture_native_list(nd::List* submitted) {
    if(!submitted) {
        log("nd: unknown or already consumed native submission record");
        std::abort();
    }
    auto batch=std::make_unique<NativeSubmission>();
    nd::clear_current(submitted->dev, submitted);
    batch->lists.push_back(submitted);
    // These labels outlive the submission API. Capture the destination's
    // identity here, before the GPU queue is allowed to delay the operation.
    for (const nd::List* list : batch->lists)
        for (const nd::Item& item : list->items)
            if (item.d && item.d->kind == ng::Draw::WriteData && item.d->prim == ng::kEopLabel)
                batch->completion_mappings.emplace(item.d,
                    guest_mapping_identity(item.d->caller, item.d->inline_vertices.size()));
    return batch.release();
}
NativeSubmission* native_device_capture_record(uint64_t record) {
    return capture_native_list(nd::g_records.take_record(record));
}
NativeSubmission* native_device_capture_submit(uint32_t count, uint32_t* const* dcbs, const uint32_t* sizes) {
    const uint64_t array=uint64_t(dcbs);
    auto* list=count<=64 && array>0x30
        ? nd::g_records.take_record(array-0x30) : nd::g_records.take(array,count);
    if(!list)log("nd: unknown retail submission record %#llx (%u segments)",(unsigned long long)array,count);
    return capture_native_list(list);
}
void native_device_execute_submit(NativeSubmission* batch) {
    if (!batch) return;
    std::unique_ptr<NativeSubmission> owned(batch);
    auto& lists = owned->lists;
    // Only abandoned *unpublished* lists are reclaimed here. A published
    // record may legitimately wait behind a worker; never age it out.
    std::vector<nd::List*> orphans = nd::g_records.take_abandoned();
    for (nd::List* l : orphans) {
        ++nd::g_orphans;
        nd::free_list(l);
    }
    std::vector<ng::Draw*> draws;
    struct PendingLabel { uint64_t addr, mapping; bool timestamp; std::vector<uint8_t> bytes; };
    std::vector<PendingLabel> labels;
    if (!ng::active()) {
        log("native GPU: submission lost its native Vulkan device");
        std::abort();
    }

    auto flush_draws = [&] {
        if (draws.empty()) return;
        const auto t0 = std::chrono::steady_clock::now();
        ng::execute(draws);
        nr::g_exec_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count());
        nr::g_frame_executed = true;
        draws.clear();
    };
    // One engine submission becomes one native submission. Its completion labels and end-of-pipe
    // events are written once all of its work has finished on the GPU: never before the work that
    // precedes them, and never split into a Vulkan submission per label (the console engine issues
    // about 166 of them per frame; each Vulkan submission costs a driver call and a fence).
    unsigned eops = 0;
    auto complete = [&](bool eop) { eops += eop; };
    auto finish = [&] {
        flush_draws();
        if (labels.empty() && !eops) return;
        ng::after_gpu([pending = std::move(labels), eops]() mutable {
            for (auto& label : pending) {
                auto& bytes = label.bytes;
                if (label.timestamp && bytes.size() >= 8) {
                    const uint64_t t = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()).count());
                    std::memcpy(bytes.data(), &t, 8);
                }
                if (!guest_write_current_mapping(label.addr, bytes.size(), label.mapping,
                        [](void* dest, void* data) {
                            const auto& value = *static_cast<const std::vector<uint8_t>*>(data);
                            std::memcpy(dest, value.data(), value.size());
                        }, &bytes)) {
                    static std::atomic<unsigned> expired{0};
                    if (expired.fetch_add(1, std::memory_order_relaxed) < 16)
                        log("nd: expired EOP destination %#llx; not writing into a different allocation",
                            (unsigned long long)label.addr);
                }
            }
            for (unsigned k = 0; k < eops; ++k) gnm_signal_eop();
        });
        labels.clear();
        eops = 0;
    };

    for (nd::List* l : lists) {
        for (nd::Item& it : l->items) {
            if (!it.d) {
                nr::g_ce[it.cb.stage][it.cb.slot] = it.cb.cb;
                nr::g_ce_known[it.cb.stage][it.cb.slot] = true;
                continue;
            }
            ng::Draw* d = it.d;
            if (d->kind == ng::Draw::WriteData && d->prim == ng::kEopLabel) {
                auto stamp = owned->completion_mappings.find(d);
                labels.push_back({d->caller, stamp == owned->completion_mappings.end() ? 0 : stamp->second,
                                  d->count != 0, d->inline_vertices});
                complete(false);
                continue;
            }
            if (d->kind <= ng::Draw::Clear)
                for (int st = 0; st < 2; ++st)
                    for (int sl = 0; sl < 16; ++sl)
                        if (nr::g_ce_known[st][sl]) d->cb[st][sl] = nr::g_ce[st][sl];
            draws.push_back(d);
        }
        if (l->eop) complete(true);
    }
    finish();
    nd::g_lists_run += lists.size();
    for (nd::List* l : lists) nd::free_list(l);
}

void native_gfx_flip(void* addr, uint32_t width, uint32_t height) {
    if (!nr::g_base || !ng::active()) return;
    if (nr::g_frame_executed) {
        ++nr::g_executed_frames;
        bool dump = nr::g_dump_every && nr::g_executed_frames % uint64_t(nr::g_dump_every) == 0;
        static const char* snap = getenv("D1_NG_SNAP");
        static int snap_phase = 0;
        static int snap_more = 0;
        if (snap_phase == 2 && snap_more > 0) dump = true, --snap_more;
        if (snap && snap_phase < 2) {
            static std::chrono::steady_clock::time_point t_in{};
            const char* colon = std::strrchr(snap, ':');
            const size_t n = colon ? size_t(colon - snap) : std::strlen(snap);
            const char* gs = game_state();
            if (snap_phase == 1) {
                dump = true, snap_phase = 2;
                log("native gfx: snapshot at state '%s'", gs ? gs : "?");
                if (const char* v = getenv("D1_NG_SNAP_FRAMES")) snap_more = std::atoi(v);
            } else if (gs && std::strncmp(gs, snap, n) == 0) {
                if (t_in == std::chrono::steady_clock::time_point{}) t_in = std::chrono::steady_clock::now();
                else if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t_in).count() > (colon ? std::atof(colon + 1) : 0)) {
                    ng::request_passlog(getenv("D1_NG_PASSLOG_N") ? uint32_t(std::max(1, std::atoi(getenv("D1_NG_PASSLOG_N")))) : 1u);
                    snap_phase = 1;
                }
            }
        }
        const auto f0 = std::chrono::steady_clock::now();
        ng::flip(uint64_t(addr), width, height, gpu_flip_count(), dump);
        const uint64_t flip_ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - f0).count());
        nr::g_flip_ns += flip_ns;
        {
            static auto last = std::chrono::steady_clock::now();
            static uint64_t exec0 = 0, faults0 = 0;
            static ng::Counters c0 = ng::counters();
            const auto now = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - last).count();
            const ng::Counters c = ng::counters();
            const uint64_t faults = wt::g_faults.load();
            static int told = 0;
            if (ms > 40.0 && told < 300) {
                ++told;
                const char* gs = game_state();
                log("slow frame: %.1f ms | recording %.1f ms, present %.1f ms | new pipelines %llu, shaders %llu, textures %llu "
                    "(%.1f MiB) | %llu draws, %llu write faults (%s)",
                    ms, double(nr::g_exec_ns >= exec0 ? nr::g_exec_ns - exec0 : nr::g_exec_ns) / 1e6, double(flip_ns) / 1e6,
                    (unsigned long long)(c.pipelines - c0.pipelines), (unsigned long long)(c.shaders - c0.shaders),
                    (unsigned long long)(c.textures - c0.textures), double(c.tex_bytes - c0.tex_bytes) / 1048576.0,
                    (unsigned long long)(c.draws - c0.draws), (unsigned long long)(faults - faults0), gs ? gs : "?");
            }
            last = now, exec0 = nr::g_exec_ns, faults0 = faults, c0 = c;
        }
        static auto t0 = std::chrono::steady_clock::now();
        static uint64_t n0 = 0;
        const auto now = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(now - t0).count();
        if (secs >= 1.0) {
            const uint64_t n = nr::g_executed_frames;
            const double fps = double(n - n0) / secs;
#ifdef D1_FRONTEND_SDL
            char title[256];
            std::snprintf(title, sizeof title, "Destiny (d1-native) - %.0f fps (%.1f ms) - %s%s", fps,
                          fps > 0 ? 1000.0 / fps : 0.0, ng::gpu_name(), ng::debug_toggle_text());
            frontend::set_status(title);
#endif
            static int report = 0;
            if (++report % (getenv("D1_NG_STATS") ? 5 : 15) == 1) {
                const double frames = double(std::max<uint64_t>(1, n - n0));
                static uint64_t faults0 = 0, fault_frames0 = 0;
                const uint64_t faults = wt::g_faults.load();
                const double fault_frames = double(std::max<uint64_t>(1, n - fault_frames0));
                fault_frames0 = n;
                log("native gfx: %.1f fps | per frame: record %.1f ms, present+wait %.1f ms, %.0f write faults",
                    fps, double(nr::g_exec_ns) / 1e6 / frames, double(nr::g_flip_ns) / 1e6 / frames,
                    double(faults - faults0) / fault_frames);
                auto& gp = render::perf();
                static uint64_t gf0 = 0, gn0 = 0, gw0 = 0, qw0 = 0;
                const uint64_t gf = gp.guest_frames, gn = gp.guest_ns, gw = gp.guest_wait_ns, qw = gpu_queue_wait_ns();
                const double gfr = double(std::max<uint64_t>(1, gf - gf0));
                log("native gfx: game frame %.1f ms (waiting for GPU events %.1f ms, for the GPU queue %.1f ms)",
                    double(gn - gn0) / 1e6 / gfr, double(gw - gw0) / 1e6 / gfr, double(qw - qw0) / 1e6 / gfr);
                gf0 = gf, gn0 = gn, gw0 = gw, qw0 = qw;
                faults0 = faults;
                ng::print_stats();
                if (wt::g_log_faults) wt::print_census(), faultlog::print();
                log("nd: texture updates announced at their source: %llu glyph cache uploads, %llu tiled surfaces",
                    (unsigned long long)resev::g_glyph_uploads.exchange(0), (unsigned long long)resev::g_tiled.exchange(0));
            }
            t0 = now, n0 = n;
            nr::g_exec_ns = nr::g_flip_ns = 0;
        }
    }
    nr::g_frame_executed = false;
}

}  // namespace d1
