#pragma once
#include <cstdint>
#include "native_asset_region.h"

namespace d1::asset_suballocation {
// Global region entry 0x70 bytes. These are the fields of the original
// allocator, not a new independent allocator or a second set of cursors.
struct State {
    uint64_t base=0, low_backed=0, high_backed=0, capacity=0;
    uint64_t committed=0, low_used=0, high_used=0;
};
enum class Path { fallback, ready, grow_low, grow_high, redistribute_high };
struct Allocation {
    Path path=Path::fallback;
    uint64_t address=0, low_backed=0, high_backed=0, used=0, growth=0, padding=0;
};
constexpr bool valid(const State& s) {
    return s.base&&s.base<asset_region::address_limit&&s.capacity<=asset_region::address_limit-s.base&&
        s.committed<=s.capacity&&s.low_backed<=s.capacity&&s.high_backed<=s.capacity-s.low_backed&&
        s.low_backed+s.high_backed==s.committed&&
        s.low_used<=s.low_backed&&s.high_used<=s.high_backed;
}
constexpr Allocation allocate(const State& s,uint64_t bytes,bool top,bool growable) {
    if(!valid(s)||!bytes)return {};
    if(!growable) {
        if(s.high_used>s.committed-s.low_used||bytes>s.committed-s.low_used-s.high_used)return {};
        return {Path::ready,top?s.base+s.committed-s.high_used-bytes:s.base+s.low_used,
            s.low_backed,s.high_backed,(top?s.high_used:s.low_used)+bytes,0};
    }
    if(top) {
        const uint64_t slack=s.high_backed-s.high_used;
        if(bytes<=slack)
            return {Path::ready,s.base+s.capacity-s.high_used-bytes,
                s.low_backed,s.high_backed,s.high_used+bytes,0};
        // 0xB2CA4..0xB2CB3: GPU region discards the entire remaining
        // high-side slack into its padding ledger before committing.
        // At full commitment, 0xB2CDA moves the logical split. The
        // retail fit comparison is made before consuming the padding.
        // Do not adopt anomalous states where that comparison would
        // permit the resulting high side to overlap the live low side.
        if(s.committed==s.capacity) {
            if(bytes>s.low_backed-s.low_used)return {};
            return {Path::redistribute_high,s.base+s.capacity-s.high_backed-bytes,
                s.low_backed-bytes,s.high_backed+bytes,s.high_backed+bytes,0,slack};
        }
        // Multi-iteration exhaustion retains the original slow path.
        const uint64_t gap=s.capacity-s.committed;
        if(bytes>gap)return {};
        const uint64_t rounded=(bytes+asset_region::alignment-1)&~(asset_region::alignment-1);
        const uint64_t growth=rounded<gap?rounded:gap;
        if(growth<bytes||(growth&(asset_region::alignment-1)))return {};
        return {Path::grow_high,s.base+s.capacity-s.high_backed-bytes,
            s.low_backed,s.high_backed+growth,s.high_backed+bytes,growth,slack};
    }
    if(bytes>s.capacity-s.low_used-s.high_used)return {};
    const uint64_t end=s.low_used+bytes;
    if(end<=s.low_backed)
        return {Path::ready,s.base+s.low_used,s.low_backed,s.high_backed,end,0};
    const uint64_t missing=end-s.low_backed;
    if(s.committed==s.capacity) {
        // The original may move the logical split when all pages are backed.
        return {Path::ready,s.base+s.low_used,end,s.capacity-end,end,0};
    }
    // 0xB2E4B: round growth to 64 KiB, capped at the uncommitted gap.
    const uint64_t gap=s.capacity-s.committed;
    const uint64_t rounded=(missing+asset_region::alignment-1)&~(asset_region::alignment-1);
    const uint64_t growth=rounded<gap?rounded:gap;
    if(!growth||(growth&(asset_region::alignment-1)))return {};
    // If another iteration / split would be needed, retain the original
    // slow path before mutating any state.
    if(growth<missing)return {};
    return {Path::grow_low,s.base+s.low_used,s.low_backed+growth,s.high_backed,end,growth};
}
struct Release { bool handled=false, success=false, dirty=false; uint64_t used=0; };
// Inactive top-side release follows the first descriptor's address, or
// its successor if the released address equals the first. The original
// list is read by the adapter only when this branch actually needs it.
struct ContainerHead {
    bool known=false, first_present=false, next_present=false;
    uint64_t first_address=0, next_address=0;
};
constexpr Release release(const State& s,uint64_t address,uint64_t bytes,bool top,
                          bool growable,bool relaxed_low,bool top_active,ContainerHead head={}) {
    if(!valid(s)||!bytes)return {};
    if(!top) {
        if(bytes>s.low_used)return {};
        const uint64_t used=s.low_used-bytes;
        if(!relaxed_low&&address!=s.base+used)return {true,false,false,s.low_used};
        return {true,true,false,used};
    }
    const uint64_t end=s.base+(growable?s.capacity:s.committed);
    if(!top_active) {
        if(address!=end-s.high_used)return {true,true,false,s.high_used};
        if(!head.known)return {};
        if(!head.first_present)return {true,true,false,0};
        if(head.first_address==address&&!head.next_present)return {true,true,false,0};
        const uint64_t next=head.first_address==address?head.next_address:head.first_address;
        // 0xB3044 adds address-next to the existing high-used count.
        if(next<s.base+s.low_used||next>end)return {};
        return {true,true,false,end-next};
    }
    if(address!=end-s.high_used)return {true,true,true,s.high_used};
    if(bytes>s.high_used)return {};
    return {true,true,false,s.high_used-bytes};
}
} // namespace d1::asset_suballocation
