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
    struct Slot {Phase phase=Phase::free;uint32_t seq=0;std::vector<uint32_t> chunks;};
    std::mutex mutex_;
    std::array<Slot,slot_count> slots_;
    std::vector<uint32_t> free_;
    uint32_t head_=0,tail_=0,seq_;
public:
    struct Reservation {uint32_t slot=UINT32_MAX,seq=0;};
    explicit Pool(uint32_t chunks,uint32_t seq=1):seq_(seq){
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
        if(index>=slot_count||slots_[index].phase!=Phase::open||free_.empty())return UINT32_MAX;
        auto& chunks=slots_[index].chunks;
        const uint32_t chunk=free_.back();
        chunks.push_back(chunk);free_.pop_back();
        mirror(chunk,uint32_t(chunks.size()));
        return chunk;
    }
    template<class Mirror> bool close(uint32_t index,Mirror mirror){
        std::lock_guard lk(mutex_);
        if(index>=slot_count||slots_[index].phase!=Phase::open)return false;
        auto& slot=slots_[index];
        slot.phase=Phase::closed;
        mirror(slot.seq,slot.chunks.empty()?UINT32_MAX:slot.chunks.back());
        return true;
    }
    template<class Mirror> bool retire(uint32_t sample,Mirror mirror){
        std::lock_guard lk(mutex_);
        if(head_==tail_)return false;
        auto& slot=slots_[head_];
        if(slot.phase!=Phase::closed||!completed(sample,slot.seq))return false;
        for(auto id:slot.chunks)free_.push_back(id);
        slot.chunks.clear();slot.phase=Phase::free;
        uint32_t old=head_;head_=next(head_);
        mirror(old,head_);
        return true;
    }
    bool empty(){std::lock_guard lk(mutex_);return head_==tail_;}
    uint32_t available(){std::lock_guard lk(mutex_);return uint32_t(free_.size());}
};
} // namespace d1::ng::cbm
