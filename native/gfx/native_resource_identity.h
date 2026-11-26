#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>

namespace d1::ng {
// Retail handle partition recovered from the signed shift and mask in
// the 01.33 handle resolver. Positive handles use the 1024 small pools;
// negative handles can select all 16384 pool descriptors.
constexpr uint32_t handle_pool(uint32_t handle) {
    const uint32_t hs=(handle>>13)|((handle&0x80000000u)?0xFFF80000u:0);
    return ((hs>>18)|0x3FFu)&hs;
}
// Native resource identities are distinct from legacy addresses and pool
// lifetimes. A pool destroy invalidates all of its entries in O(1).
template<unsigned Bits=18,size_t Probe=64>
class ResourceRegistry {
    static_assert(Bits>0&&Bits<31);
    static constexpr size_t Slots=size_t(1)<<Bits;
    struct Slot {
        std::atomic<uint64_t> key{0}, sequence{0}, identity{0}, pool_epoch{0};
        std::atomic<uint32_t> handle{UINT32_MAX};
    };
    std::unique_ptr<Slot[]> slots_=std::make_unique<Slot[]>(Slots);
    std::unique_ptr<std::atomic<uint64_t>[]> pools_=std::make_unique<std::atomic<uint64_t>[]>(16384);
    std::mutex mutex_;
    uint32_t next_id_=0;
    static size_t home(uint64_t key) { return size_t((key*0x9E3779B97F4A7C15ull)>>(64-Bits)); }
    uint64_t epoch(uint32_t handle) const {
        return handle==UINT32_MAX?0:pools_[handle_pool(handle)].load();
    }
    bool stale(const Slot& s) const {
        const auto h=s.handle.load();
        const auto e=epoch(h);
        return (e&1)||s.pool_epoch.load()!=e;
    }
public:
    struct Snapshot { uint64_t identity=0; uint32_t handle=UINT32_MAX; uint64_t pool_epoch=0; };
    bool current(const Snapshot& s) const {
        const auto e=epoch(s.handle);
        return s.identity&&!(e&1)&&e==s.pool_epoch;
    }
    bool begin_pool_retire(uint32_t pool) {
        if(pool>=16384)return false;
        std::lock_guard lock(mutex_);
        const auto e=pools_[pool].load();
        if(e&1)return false;
        pools_[pool].store(e+1);
        return true;
    }
    void end_pool_retire(uint32_t pool) {
        if(pool>=16384)return;
        std::lock_guard lock(mutex_);
        const auto e=pools_[pool].load();
        if(e&1)pools_[pool].store(e+1);
    }
    Snapshot get(uint64_t key) const {
        if(!key)return {};
        const size_t h=home(key);
        for(size_t i=0;i<Probe&&i<Slots;++i) {
            const auto& s=slots_[(h+i)&(Slots-1)];
            const auto k=s.key.load();
            if(!k)return {};
            if(k!=key)continue;
            for(unsigned retry=0;retry<8;++retry) {
                const auto seq=s.sequence.load();
                if(seq&1)continue;
                const auto actual=s.key.load();
                Snapshot v{s.identity.load(),s.handle.load(),s.pool_epoch.load()};
                if(s.sequence.load()!=seq)continue;
                if(actual!=key)break; // reclaimed slot; keep probing
                return current(v)?v:Snapshot{};
            }
        }
        return {};
    }
    bool touch(uint64_t key,bool insert,uint32_t handle=UINT32_MAX,uint64_t* published=nullptr) {
        if(published)*published=0;
        if(!key)return false;
        std::lock_guard lock(mutex_);
        const size_t h=home(key);
        Slot* selected=nullptr;
        Slot* reusable=nullptr;
        for(size_t i=0;i<Probe&&i<Slots;++i) {
            auto& s=slots_[(h+i)&(Slots-1)];
            const auto k=s.key.load();
            if(k==key){selected=&s;break;}
            if(!k){if(!reusable)reusable=&s;break;}
            if(!reusable&&stale(s))reusable=&s;
        }
        if(!selected){
            if(!insert||!reusable)return false;
            selected=reusable;
        }
        auto& s=*selected;
        const bool same=s.key.load()==key;
        const auto old=s.identity.load();
        const auto old_handle=s.handle.load();
        const auto new_handle=handle!=UINT32_MAX?handle:(same?old_handle:UINT32_MAX);
        const auto e=epoch(new_handle);
        if(e&1)return false;
        const bool changed=same&&handle!=UINT32_MAX&&old_handle!=UINT32_MAX&&handle!=old_handle;
        const bool expired=same&&stale(s);
        if((changed||expired)&&!insert)return false;
        const bool fresh=!same||changed||expired||uint32_t(old)==UINT32_MAX;
        if(fresh&&next_id_==UINT32_MAX)return false;
        const auto id=fresh?(uint64_t(++next_id_)<<32)|1:old+1;
        s.sequence.fetch_add(1);
        s.key.store(key);
        s.handle.store(new_handle);
        s.pool_epoch.store(e);
        s.identity.store(id);
        s.sequence.fetch_add(1);
        if(published)*published=id;
        return true;
    }
};
// Store exact key fields; hash is only for bucket selection, not identity.
struct GeometryKey {
    uint64_t resource=0,offset=0,size=0;
    uint32_t index_width=0,index_count=0;
    bool operator==(const GeometryKey&) const = default;
};
struct GeometryKeyHash {
    size_t operator()(const GeometryKey& k) const noexcept {
        auto mix=[](uint64_t a,uint64_t b) {
            a=(a*0xC2B2AE3D27D4EB4Full+b)*0x9E3779B97F4A7C15ull;
            return a^(a>>31);
        };
        return size_t(mix(mix(mix(k.resource,k.offset),k.size),uint64_t(k.index_width)<<32|k.index_count));
    }
};
} // namespace d1::ng
