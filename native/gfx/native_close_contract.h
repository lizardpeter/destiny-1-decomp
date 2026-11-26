#pragma once
#include <cstdint>
#include <cstring>
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
// 0x7DD060 drains only the ready FIFO prefix. The exact original
// unsigned half-range rule excludes the ambiguous distance 0x80000000.
constexpr bool ring_pending(uint32_t head,uint32_t tail) {
    return uint32_t(head-tail)>0x80000000u;
}
constexpr bool progress_ready(uint32_t completed,uint32_t previous) {
    return ((completed-previous)&0x80000000u)==0;
}
constexpr bool flip_sample_newer(int64_t id,int64_t last) {
    return id>last||(id<1000&&last>1000000);
}
// 0x7DB810: compatibility ledger for the remaining engine record fields.
// Native list readiness is independent of this ledger. The original uses
// low 32-bit pointer differences and stores aligned lengths, not packets.
template<class T> T segment_read(const void* base,size_t off){
    T v;std::memcpy(&v,static_cast<const uint8_t*>(base)+off,sizeof v);return v;
}
template<class T> void segment_write(void* base,size_t off,T value){
    std::memcpy(static_cast<uint8_t*>(base)+off,&value,sizeof value);
}
inline bool record_segment(void* record,const void* context,uint32_t kind,uint32_t hint,uint32_t final_flag){
    const auto slot=segment_read<uint32_t>(record,0x24);
    if(slot>=64)return false;
    const auto dcb=segment_read<uint64_t>(context,0);
    const auto ccb=segment_read<uint64_t>(context,0x80);
    const auto dn=uint32_t(segment_read<uint64_t>(context,0x10))-uint32_t(dcb);
    const auto cn=uint32_t(segment_read<uint64_t>(context,0x90))-uint32_t(ccb);
    segment_write(record,0x630+slot*4,kind);
    segment_write(record,0x730+slot*4,hint);
    segment_write(record,0x830+slot*4,final_flag);
    if((dn|cn)>3){
        segment_write(record,0x430+slot*4,dn&~3u);
        segment_write(record,0x30+slot*8,dcb);
        segment_write(record,0x530+slot*4,cn&~3u);
        segment_write(record,0x230+slot*8,(cn&~3u)?ccb:uint64_t(0));
        segment_write(record,0x24,slot+1);
    }
    return true;
}
} // namespace d1::ng
