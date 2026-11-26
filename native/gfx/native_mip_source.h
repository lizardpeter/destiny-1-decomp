#pragma once
#include <cstdint>
namespace d1::ng {
// A full-resolution descriptor does not itself prove that its entire
// mip chain belongs to the allocation. The original handle-size query
// provides that evidence, unlike the extent of the encompassing CPU pool.
enum class MipSource : uint8_t { invalid, low_chain, split_chain, high_only, contiguous_high };
constexpr MipSource mip_source(bool low_descriptor,bool high_descriptor,
                               uint64_t low_base,uint64_t high_base,
                               uint64_t low_bytes,uint64_t high_bytes,
                               uint64_t descriptor_bytes,uint64_t top_bytes){
    if(!top_bytes||descriptor_bytes<top_bytes)return MipSource::invalid;
    if(high_descriptor&&high_base){
        if(high_bytes>=descriptor_bytes)return MipSource::contiguous_high;
        if(high_bytes&&high_bytes<top_bytes)return MipSource::invalid;
        if(low_base&&low_base!=high_base)return MipSource::split_chain;
        return MipSource::high_only;
    }
    if(low_descriptor&&low_base&&(!low_bytes||low_bytes>=descriptor_bytes))
        return MipSource::low_chain;
    return MipSource::invalid;
}
} // namespace d1::ng
