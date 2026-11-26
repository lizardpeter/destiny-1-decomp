#pragma once
#include <cstdint>
namespace d1::ng::cbm {
constexpr uint64_t chunk_bytes=0x80000,slot_count=512,slot_bytes=8;
constexpr uint32_t invalid_chain=0xcdcdcdcd,open_bias=0x80000;
struct Layout {uint64_t data=0,label=0,bytes=0;};
constexpr Layout layout(uint64_t data,uint64_t gran){
    if(!data||data%chunk_bytes||gran<sizeof(uint32_t)||(gran&(gran-1)))return {};
    if(data>UINT64_MAX-(sizeof(uint32_t)+gran-1))return {};
    return {data,data,(data+sizeof(uint32_t)+gran-1)&~(gran-1)};
}
constexpr uint32_t next(uint32_t i){return (i+1)&(slot_count-1);}
constexpr bool completed(uint32_t sample,uint32_t seq){return ((sample-seq)&0x80000000u)==0;}
constexpr uint32_t open_value(uint32_t seq){return seq+open_bias;}
constexpr uint32_t closed_value(uint32_t seq){return seq-open_bias;}
} // namespace d1::ng::cbm
