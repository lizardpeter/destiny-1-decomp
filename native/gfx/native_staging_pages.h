#pragma once
#include <cstdint>
#include <algorithm>
namespace d1::ng {
// A staging page is exclusively owned by one recording submission until its fence.
constexpr uint64_t kGeometryStagingPage=4ull<<20;
constexpr uint64_t kGeometryStagingBudget=128ull<<20;
struct StagingSpan { uint64_t offset=0, size=0; bool valid=false; };
constexpr StagingSpan staging_span(uint64_t capacity,uint64_t used,uint64_t bytes) {
    if(!bytes||bytes>UINT64_MAX-3||used>capacity||used>UINT64_MAX-3)return {};
    const uint64_t off=(used+3)&~3ull, size=(bytes+3)&~3ull;
    if(off>capacity||size>capacity-off)return {};
    return {off,size,true};
}
constexpr uint64_t staging_page_size(uint64_t bytes,uint64_t committed) {
    if(committed>kGeometryStagingBudget||!bytes||bytes>UINT64_MAX-3)return 0;
    const uint64_t padded=(bytes+3)&~3ull, remaining=kGeometryStagingBudget-committed;
    if(padded>remaining)return 0;
    return std::min(remaining,std::max(kGeometryStagingPage,padded));
}
} // namespace d1::ng
