#pragma once
#include <cstdint>
#include "native_resource_binding.h"
namespace d1::ng::blit {
constexpr uint32_t capacity=0xa00;
constexpr uint64_t pack(uint32_t head,uint32_t tail) {return uint64_t(tail)<<32|head;}
constexpr uint32_t head(uint64_t cursor){return uint32_t(cursor);}
constexpr uint32_t tail(uint64_t cursor){return uint32_t(cursor>>32);}
constexpr uint32_t next(uint32_t index){return index==capacity-1?0:index+1;}
constexpr bool valid(uint64_t cursor){return head(cursor)<capacity&&tail(cursor)<capacity;}
constexpr uint32_t used(uint64_t cursor){
    return (tail(cursor)+capacity-head(cursor))%capacity;
}
// 0x7DA910 accepts count+used == capacity (unlike reservation, which
// keeps one slot empty). Preserve the original unsigned wrapping add.
constexpr bool can_request(uint64_t cursor,uint32_t count){
    return valid(cursor)&&uint32_t(used(cursor)+count)<=capacity;
}
constexpr bool contains(uint64_t cursor,uint32_t slot) {
    return valid(cursor)&&slot<capacity&&(slot+capacity-head(cursor))%capacity<used(cursor);
}
constexpr bool completed(uint32_t sample,uint32_t sequence){
    return ((sample-sequence)&0x80000000u)==0;
}
constexpr bool reserve(uint64_t cursor,uint64_t& updated,uint32_t& slot){
    if(!valid(cursor)||next(tail(cursor))==head(cursor))return false;
    slot=tail(cursor);updated=pack(head(cursor),next(slot));return true;
}
struct Request {
    uint64_t destination,source;
    uint32_t bytes;
    uint64_t callback,arg0,arg1;
};
inline void initialize(void* record,const Request& r){
    using binding::store;
    store(record,8,r.destination);store(record,0x10,r.source);
    store(record,0x18,r.bytes);store(record,0x20,r.callback);
    store(record,0x28,r.arg0);store(record,0x30,r.arg1);
    store(record,0x3c,uint8_t(0));
}
} // namespace d1::ng::blit
