#pragma once
#include <cstdint>
namespace d1::ng {
// Facts from eboot 01.33 0x7DCEC0/0x7DE950/0x7DD060. Native list
// readiness is separate from the byte extent of the original command buffers.
struct ClosePlan {
    uint32_t label_flag, segment_kind, segment_hint;
    bool update_progress, flip, append_ring;
};
constexpr ClosePlan close_plan(uint32_t kind,uint8_t flag,bool worker) {
    if(worker)return {uint32_t(flag^1u),3,0x18,false,false,false};
    return {uint32_t(flag==0||kind==1),2,kind==1?0x118u:0x18u,kind==1,kind!=0,true};
}
constexpr uint32_t record_ring_slot(uint32_t tail) {return tail&63u;}
// The original signed comparison is the sign bit of a wrapping 32-bit
// subtraction, not a signed C++ subtraction that could overflow.
constexpr bool worker_should_submit(uint32_t completed,uint32_t previous,uint8_t enabled) {
    return enabled!=0&&((completed-previous)&0x80000000u)==0;
}
} // namespace d1::ng
