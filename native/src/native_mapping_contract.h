#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
namespace d1 {
// Subtraction-based range checks avoid overflow in base+extent or addr+size.
// Mapping identity zero is not a valid lifetime token.
constexpr bool mapping_contains(uint64_t base,uint64_t extent,uint64_t addr,uint64_t size) {
    if(!size||addr<base||extent>UINT64_MAX-base)return false;
    const uint64_t offset=addr-base;
    return offset<extent&&size<=extent-offset;
}
constexpr bool mapping_allows(uint64_t base,uint64_t extent,uint64_t current,uint64_t addr,
                              uint64_t size,uint64_t expected,unsigned protection,unsigned required) {
    return expected&&current==expected&&mapping_contains(base,extent,addr,size)&&
           (protection&required)==required;
}
struct MappingSpan { uint64_t base, size, identity; };
using MappingSpans = std::vector<MappingSpan>;
// Lookup returns the writable region at each address, or false for a hole.
// Capture each mapping's identity rather than conflating adjacent mappings.
template<class Lookup>
bool capture_mapping_spans(uint64_t addr,uint64_t size,MappingSpans& spans,Lookup lookup){
    spans.clear();
    if(!size||size>UINT64_MAX-addr)return false;
    while(size){
        MappingSpan r{};
        if(!lookup(addr,r)||!r.identity||!mapping_contains(r.base,r.size,addr,1)){
            spans.clear();return false;
        }
        const uint64_t n=std::min(size,r.size-(addr-r.base));
        spans.push_back({addr,n,r.identity});
        addr+=n;size-=n;
    }
    return true;
}
template<class Lookup>
bool validate_mapping_spans(uint64_t addr,uint64_t size,const MappingSpans& spans,Lookup lookup){
    if(!size||size>UINT64_MAX-addr)return false;
    for(const auto& span:spans){
        if(span.base!=addr||!span.size)return false;
        const uint64_t n=std::min(size,span.size);
        MappingSpan r{};
        if(!lookup(addr,r)||!span.identity||r.identity!=span.identity||
           !mapping_contains(r.base,r.size,addr,n))return false;
        addr+=n;size-=n;
        if(!size)return true;
    }
    return false;
}
} // namespace d1
