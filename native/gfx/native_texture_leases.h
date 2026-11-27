#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include "native_texture_identity.h"
#include "native_resource_identity.h"

namespace d1::ng {
// Native command-list ownership of captured texture versions. A lease ends
// when the CPU list is consumed/discarded; the Vulkan submission's retirement
// fence subsequently owns the actual image and descriptor lifetime.
// The capture gate closes the lookup->pin race against cache retirement.
class TextureLeaseRegistry {
    std::shared_mutex gate_;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t,size_t> refs_;
public:
    auto capture() { return std::shared_lock(gate_); }
    auto retire() { return std::unique_lock(gate_); }
    void pin(uint64_t id) {
        if(!id)return;
        std::lock_guard lk(mutex_);
        ++refs_[id];
    }
    void unpin(uint64_t id) {
        std::lock_guard lk(mutex_);
        auto it=refs_.find(id);
        if(it!=refs_.end()&&!--it->second)refs_.erase(it);
    }
    bool pinned(uint64_t id) const {
        std::lock_guard lk(mutex_);
        return refs_.contains(id);
    }
};
inline TextureLeaseRegistry& texture_leases(){
    // Process lifetime, including late command-list teardown.
    static auto* registry=new TextureLeaseRegistry;
    return *registry;
}
class TextureLeaseSet {
    TextureLeaseRegistry& registry_;
    std::unordered_set<uint64_t> ids_;
    // A resource epoch can be bound through more than one physical source
    // layout. Keep one lifetime lease per epoch but one preparation request
    // per exact source. The small memo avoids a hash-table walk on repeated
    // material binds; collisions fall back to the exact set.
    std::unordered_set<TextureSourceKey,TextureSourceKeyHash> sources_;
    std::array<TextureSourceKey,128> recent_sources_{};
public:
    explicit TextureLeaseSet(TextureLeaseRegistry& registry=texture_leases()):registry_(registry){}
    TextureLeaseSet(const TextureLeaseSet&)=delete;
    TextureLeaseSet& operator=(const TextureLeaseSet&)=delete;
    ~TextureLeaseSet(){for(auto id:ids_)registry_.unpin(id);}
    // One reference per distinct epoch per list, not per draw or binding.
    bool pin(uint64_t id){if(!id||!ids_.insert(id).second)return false;registry_.pin(id);return true;}
    bool pin_source(const uint32_t* words,uint64_t id){
        if(!id)return false;
        const auto key=texture_source_key(words,id);
        auto& recent=recent_sources_[(id^(id>>32))&(recent_sources_.size()-1)];
        if(recent==key)return false;
        recent=key;
        if(!sources_.insert(key).second)return false;
        pin(id);
        return true;
    }
};
} // namespace d1::ng
