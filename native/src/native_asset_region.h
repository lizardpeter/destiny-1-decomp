#pragma once
#include <cstdint>

namespace d1::asset_region {
constexpr uint64_t alignment=0x10000;
constexpr uint64_t address_limit=uint64_t(1)<<40;
constexpr uint64_t reserve_size(uint64_t bytes) {
    return bytes && bytes<=address_limit-alignment
        ? (bytes+alignment-1)&~(alignment-1) : 0;
}
struct Commit {
    uint64_t address=0, next_cursor=0;
};
// The retail descriptor has low/high committed byte counts, not allocation
// offsets. Its owner may redistribute them once the entire region is backed.
constexpr Commit plan(uint64_t base,uint64_t capacity,uint64_t low,uint64_t high,
                      uint64_t bytes,bool top,bool blocked) {
    if(blocked||!base||base>=address_limit||capacity>address_limit-base||
       !bytes||low>capacity||high>capacity-low||bytes>capacity-low-high)
        return {};
    const uint64_t offset=top?capacity-high-bytes:low;
    if((base|offset|bytes)&(alignment-1))return {};
    return {base+offset,(top?high:low)+bytes};
}
constexpr bool overlaps(uint64_t a,uint64_t n,uint64_t b,uint64_t m) {
    return n&&m&&(a<=b?b-a<n:a-b<m);
}
} // namespace d1::asset_region
