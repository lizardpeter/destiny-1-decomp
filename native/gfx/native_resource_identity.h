#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>

namespace d1::ng {
// Native resource identities are distinct from legacy object addresses.
// One writer lock; readers take a coherent atomic snapshot, then the
// caller checks the engine handle still names the object. Slots are not
// removed until a true resource-destruction event is recovered.
template<unsigned Bits=18,size_t Probe=64>
class ResourceRegistry {
    static_assert(Bits>0&&Bits<31);
    static constexpr size_t Slots=size_t(1)<<Bits;
    struct Slot {
        std::atomic<uint64_t> key{0}, sequence{0}, identity{0};
        std::atomic<uint32_t> handle{UINT32_MAX};
    };
    std::unique_ptr<Slot[]> slots_=std::make_unique<Slot[]>(Slots);
    std::mutex mutex_;
    uint32_t next_id_=0;
    size_t used_=0;
    static size_t home(uint64_t key) { return size_t((key*0x9E3779B97F4A7C15ull)>>(64-Bits)); }
public:
    struct Snapshot { uint64_t identity=0; uint32_t handle=UINT32_MAX; };
    Snapshot get(uint64_t key) const {
        if(!key)return {};
        const size_t h=home(key);
        for(size_t i=0;i<Probe&&i<Slots;++i) {
            const auto& s=slots_[(h+i)&(Slots-1)];
            const auto k=s.key.load(std::memory_order_acquire);
            if(!k)return {};
            if(k!=key)continue;
            // Sequential consistency makes it impossible to observe a new
            // handle paired with the previous identity during publication.
            for(unsigned retry=0;retry<8;++retry) {
                const auto seq=s.sequence.load();
                if(seq&1)continue;
                Snapshot v{s.identity.load(),s.handle.load()};
                if(s.sequence.load()==seq)return v;
            }
            // An unstable entry is not safe to cache.
            return {};
        }
        return {};
    }
    bool touch(uint64_t key,bool insert,uint32_t handle=UINT32_MAX) {
        if(!key)return false;
        std::lock_guard lock(mutex_);
        const size_t h=home(key);
        for(size_t i=0;i<Probe&&i<Slots;++i) {
            auto& s=slots_[(h+i)&(Slots-1)];
            const auto k=s.key.load(std::memory_order_relaxed);
            if(k&&k!=key)continue;
            if(!k&&(!insert||used_*10>=Slots*7))return false;
            const auto old=s.identity.load();
            const auto old_handle=s.handle.load();
            // Different handle at the same address is a different lifetime.
            // Non-inserting events cannot claim a recycled address.
            const bool changed=k&&handle!=UINT32_MAX&&old_handle!=UINT32_MAX&&handle!=old_handle;
            if(changed&&!insert)return false;
            const bool fresh=!k||changed||uint32_t(old)==UINT32_MAX;
            if(fresh&&next_id_==UINT32_MAX)return false;
            const auto id=fresh?(uint64_t(++next_id_)<<32)|1:old+1;
            s.sequence.fetch_add(1);
            if(handle!=UINT32_MAX)s.handle.store(handle);
            s.identity.store(id);
            s.sequence.fetch_add(1);
            if(!k){++used_;s.key.store(key,std::memory_order_release);}
            return true;
        }
        return false;
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
