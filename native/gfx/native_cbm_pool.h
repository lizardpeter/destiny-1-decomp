#pragma once
#include "native_cbm_contract.h"
#include <array>
#include <mutex>
#include <vector>
#include <cstdint>
namespace d1::ng::cbm {
// Native ownership is indexed by reservation slot, not a recyclable
// engine record pointer. A chunk belongs to exactly one live slot.
class Pool {
    enum class Phase:uint8_t {free,open,closed};
    static constexpr uint32_t none=UINT32_MAX;
    struct Slot {Phase phase=Phase::free;uint32_t seq=0,first=none,count=0;};
    std::mutex mutex_;
    std::array<Slot,slot_count> slots_;
    std::vector<uint32_t> free_,links_;
    uint32_t head_=0,tail_=0,seq_;
public:
    struct Reservation {uint32_t slot=UINT32_MAX,seq=0;};
    static constexpr uint32_t exhausted=UINT32_MAX,invalid_slot=UINT32_MAX-1;
    explicit Pool(uint32_t chunks,uint32_t seq=1):links_(chunks,none),seq_(seq){
        free_.reserve(chunks);
        for(uint32_t i=chunks;i>0;--i)free_.push_back(i-1);
    }
    template<class Mirror> Reservation reserve(Mirror mirror){
        std::lock_guard lk(mutex_);
        auto& slot=slots_[tail_];
        if(next(tail_)==head_||slot.phase!=Phase::free)return {};
        Reservation result{tail_,seq_++};
        slot.phase=Phase::open;slot.seq=result.seq;
        tail_=next(tail_);
        mirror(result,tail_,seq_);
        return result;
    }
    template<class Mirror> uint32_t allocate(uint32_t index,Mirror mirror){
        std::lock_guard lk(mutex_);
        if(index>=slot_count||slots_[index].phase!=Phase::open)return invalid_slot;
        if(free_.empty())return exhausted;
        auto& slot=slots_[index];
        const uint32_t chunk=free_.back();
        free_.pop_back();
        links_[chunk]=slot.first;slot.first=chunk;++slot.count;
        mirror(chunk,slot.count);
        return chunk;
    }
    template<class Mirror> bool close(uint32_t index,Mirror mirror){
        std::lock_guard lk(mutex_);
        if(index>=slot_count||slots_[index].phase!=Phase::open)return false;
        auto& slot=slots_[index];
        slot.phase=Phase::closed;
        mirror(slot.seq,slot.first);
        return true;
    }
    template<class Mirror> bool retire(uint32_t sample,Mirror mirror){
        std::lock_guard lk(mutex_);
        if(head_==tail_)return false;
        auto& slot=slots_[head_];
        if(slot.phase!=Phase::closed||!completed(sample,slot.seq))return false;
        for(uint32_t id=slot.first;id!=none;){
            const uint32_t following=links_[id];
            links_[id]=none;free_.push_back(id);id=following;
        }
        slot.first=none;slot.count=0;slot.phase=Phase::free;
        uint32_t old=head_;head_=next(head_);
        mirror(old,head_);
        return true;
    }
    bool empty(){std::lock_guard lk(mutex_);return head_==tail_;}
    uint32_t available(){std::lock_guard lk(mutex_);return uint32_t(free_.size());}
};
} // namespace d1::ng::cbm
