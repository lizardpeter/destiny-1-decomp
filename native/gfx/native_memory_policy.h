#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace d1::ng {
// Required properties are never relaxed. Preferred properties only decide
// attempt order; a failed allocation may try another compatible memory type.
struct MemoryTypeOrder {
    std::array<uint32_t,32> indices{};
    size_t count=0;
};
inline MemoryTypeOrder memory_type_order(uint32_t bits,const uint32_t* flags,
                                        uint32_t count,uint32_t required,uint32_t preferred) {
    MemoryTypeOrder result;
    if(count>32) count=32;
    for(unsigned pass=0;pass<2;++pass) {
        for(uint32_t i=0;i<count;++i) {
            if(!(bits&(uint32_t(1)<<i))||(flags[i]&required)!=required) continue;
            const bool best=(flags[i]&preferred)==preferred;
            if(best!=(pass==0)) continue;
            result.indices[result.count++]=i;
        }
    }
    return result;
}
} // namespace d1::ng
