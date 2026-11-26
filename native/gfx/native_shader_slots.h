#pragma once
#include <cstdint>
namespace d1::ng {
// The translator publishes a four-bit pipeline signature mask per slot.
// Convert its low bits into the actual 16-bit texture-slot set without
// scanning sixteen entries for every draw. Unknown (~0) stays conservative.
constexpr uint16_t shader_slots(uint64_t m){
    m &= 0x1111111111111111ull;
    m=(m|(m>>3))&0x0303030303030303ull;
    m=(m|(m>>6))&0x000f000f000f000full;
    m=(m|(m>>12))&0x000000ff000000ffull;
    m=(m|(m>>24))&0xffffull;
    return uint16_t(m);
}
} // namespace d1::ng
