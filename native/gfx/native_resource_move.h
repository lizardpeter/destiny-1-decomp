#pragma once
#include <cstdint>
namespace d1::ng::move {
// Original pool index encoding (0x7D35A1..0x7D35AE).
constexpr uint32_t pool_index(uint32_t handle) {
    return (handle>>13)&((handle&0x80000000u)?0x3fffu:0x3ffu);
}
constexpr uint32_t datum_offset(uint32_t handle,uint32_t stride) {
    return (handle&0x1fffu)*stride; // retail 32-bit IMUL
}
constexpr uint64_t datum_address(uint64_t base,uint32_t offset,int32_t mask,uint64_t header) {
    return base+offset-(uint64_t(int64_t(mask))&header);
}
constexpr uint32_t relocation_flags(uint64_t bytes) {
    return uint32_t(bytes)|0xc0000000u;
}
constexpr uint16_t container_index(uint32_t id){return uint16_t(id);}
constexpr uint8_t container_generation(uint32_t id){return uint8_t(id>>16);}
struct Copy {
    uint64_t dst,src;
    uint32_t bytes;
};
constexpr Copy copy(uint64_t source,uint64_t destination,uint64_t delta,uint64_t bytes) {
    return {destination+delta,source,uint32_t(bytes)};
}
} // namespace d1::ng::move
