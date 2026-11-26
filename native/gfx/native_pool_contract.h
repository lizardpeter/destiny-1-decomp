#pragma once
#include <cstdint>
#include <cstring>
#include "native_resource_move.h"
namespace d1::ng::pool {
template<class T> T read(uint64_t p,uint64_t off=0){
    T v;std::memcpy(&v,reinterpret_cast<const void*>(p+off),sizeof v);return v;
}
template<class T> void write(uint64_t p,uint64_t off,T v){
    std::memcpy(reinterpret_cast<void*>(p+off),&v,sizeof v);
}
constexpr uint32_t index(uint32_t v){return v&0x3fff;}
constexpr uint32_t bit(uint32_t v){return 1u<<(v&31);}
inline bool occupied(uint64_t root,uint32_t i){
    return root&&i<16384&&(read<uint32_t>(read<uint64_t>(root,0x40010),uint64_t(i>>5)*4)&bit(i));
}
inline uint64_t descriptor(uint64_t root,uint32_t i){
    return read<uint64_t>(root,0x402a0)+uint32_t(i*read<uint32_t>(root,0x402b0));
}
// 6FB60: occupancy-checked lookup, no mutation.
inline uint64_t lookup(uint64_t root,uint32_t i){
    return occupied(root,i)?read<uint64_t>(descriptor(root,i),0x18):0;
}
// 6F5A0: the resource datum header stores the slot-relative address,
// not the absolute allocation pointer. The two IMULs wrap at 32 bits.
inline void set_datum(uint64_t table,uint32_t handle,uint64_t address,uint32_t flags){
    const auto off=uint32_t(move::pool_index(handle)*read<uint32_t>(table,0x10));
    const auto pool=read<uint64_t>(read<uint64_t>(table),uint64_t(off)+0x18);
    const auto slot=read<uint64_t>(pool,8)+move::datum_offset(handle,read<uint32_t>(pool,0x10));
    write(slot,8,slot-address);write(slot,0,flags);
}
// 6FBC0: callbacks may mutate global pool state, so re-read the root
// at the same point as retail. The datum allocator and tag release are
// supplied independently; the pool teardown body is entirely native.
template<class Root,class Notify,class Free,class ReleaseTag>
bool destroy(uint32_t value,Root root,Notify notify,Free free,ReleaseTag release_tag){
    const uint32_t i=index(value);
    const uint64_t initial=root();
    if(!occupied(initial,i))return false;
    const uint64_t entry=descriptor(initial,i);
    const uint16_t kind=read<uint16_t>(entry,0x3a);
    notify(i,kind);
    const uint64_t datum=read<uint64_t>(entry,0x18);
    free(read<uint64_t>(datum));
    write(datum,8,uint64_t(0));write(datum,0x34,UINT32_MAX);
    write(datum,0x40,uint8_t(0));
    std::memset(reinterpret_cast<void*>(entry+0x10),0,0x20);
    release_tag(read<uint16_t>(entry,0x38));
    write(entry,0x38,uint16_t(0xffff));
    const uint64_t current=root(),node=current+uint64_t(i)*0x40;
    const auto next=read<uint16_t>(node,0x3c),prev=read<uint16_t>(node,0x3e);
    if(next!=0xffff)write(current+uint64_t(next)*0x40,0x3e,prev);
    if(prev==0xffff)write(current,0x402c2,next);
    else write(current+uint64_t(prev)*0x40,0x3c,next);
    const auto bitmap=read<uint64_t>(current,0x40010);
    const auto word=uint64_t(i>>5)*4;
    write(bitmap,word,read<uint32_t>(bitmap,word)&~bit(i));
    std::memset(reinterpret_cast<void*>(node),0,0x40);
    return true;
}
} // namespace d1::ng::pool
