#pragma once
#include <cstdint>
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
} // namespace d1
