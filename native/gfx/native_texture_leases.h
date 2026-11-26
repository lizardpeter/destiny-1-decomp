#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

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
public:
    explicit TextureLeaseSet(TextureLeaseRegistry& registry=texture_leases()):registry_(registry){}
    TextureLeaseSet(const TextureLeaseSet&)=delete;
    TextureLeaseSet& operator=(const TextureLeaseSet&)=delete;
    ~TextureLeaseSet(){for(auto id:ids_)registry_.unpin(id);}
    // One reference per distinct epoch per list, not per draw or binding.
    bool pin(uint64_t id){if(!id||!ids_.insert(id).second)return false;registry_.pin(id);return true;}
};
} // namespace d1::ng
