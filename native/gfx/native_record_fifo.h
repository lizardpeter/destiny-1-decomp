#pragma once
#include <array>
#include <cstdint>
#include <mutex>
namespace d1::ng {
// A worker reserves its position before recording; close publishes readiness.
// No later ready list may overtake an earlier unfinished reservation.
class NativeRecordFifo {
    struct Entry { uint64_t record=0; bool ready=false; };
    std::array<Entry,64> entries_{};
    uint32_t head_,tail_;
    std::mutex mutex_;
public:
    explicit NativeRecordFifo(uint32_t sequence=0):head_(sequence),tail_(sequence){}
    template<class Mirror>
    bool reserve(uint64_t record,bool ready,Mirror mirror) {
        if(!record)return false;
        std::lock_guard lock(mutex_);
        if(uint32_t(tail_-head_)>=entries_.size())return false;
        for(uint32_t i=head_;i!=tail_;++i)
            if(entries_[i&63].record==record)return false;
        const uint32_t sequence=tail_++;
        entries_[sequence&63]={record,ready};
        mirror(sequence);
        return true;
    }
    template<class Mirror>
    bool publish(uint64_t record,Mirror mirror) {
        std::lock_guard lock(mutex_);
        for(uint32_t i=head_;i!=tail_;++i){
            auto& e=entries_[i&63];
            if(e.record!=record)continue;
            if(e.ready)return false;
            e.ready=true;mirror();return true;
        }
        return false;
    }
    template<class Mirror>
    uint64_t take_ready(Mirror mirror) {
        std::lock_guard lock(mutex_);
        if(head_==tail_)return 0;
        auto& e=entries_[head_&63];
        if(!e.ready)return 0;
        const auto record=e.record;
        e={};mirror(head_++);return record;
    }
};
} // namespace d1::ng
