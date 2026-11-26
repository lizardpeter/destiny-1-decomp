#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <cstddef>
namespace d1::ng {
// Descriptor indices are local to a Vulkan binding, not global across
// differently typed image arrays. Slot zero is permanently the dummy.
template<size_t Bindings,uint32_t Capacity>
class DescriptorSlots {
    static_assert(Capacity>1&&Capacity<=65536);
    struct Pool {uint32_t next=1,live=0,peak=0;std::vector<uint16_t> free;};
    std::array<Pool,Bindings> pools_{};
public:
    uint16_t acquire(size_t binding){
        if(binding>=Bindings)return 0;
        auto& p=pools_[binding];
        uint16_t id=0;
        if(!p.free.empty()){id=p.free.back();p.free.pop_back();}
        else if(p.next<Capacity)id=uint16_t(p.next++);
        if(id){++p.live;if(p.live>p.peak)p.peak=p.live;}
        return id;
    }
    // Caller releases once, after the last submission that used the view.
    void release(size_t binding,uint16_t id){
        if(binding>=Bindings||!id)return;
        auto& p=pools_[binding];
        if(id>=p.next||!p.live)return;
        p.free.push_back(id);--p.live;
    }
    uint32_t live(size_t b)const{return pools_[b].live;}
    uint32_t peak(size_t b)const{return pools_[b].peak;}
};
} // namespace d1::ng
