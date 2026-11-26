#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace d1::ng::texture_ctor {
// Stack layouts verified from the instructions at F83657..F836F9 and
// F83BB9..F83C54. These are the input/output of the original layout leaf.
struct alignas(32) LayoutInput {
    uint32_t tile,format,width,height,depth,samples,base_pitch,mip,unused,flags;
};
struct alignas(32) LayoutOutput { uint32_t words[16]; };
static_assert(sizeof(LayoutInput)==64&&offsetof(LayoutInput,flags)==36);
static_assert(sizeof(LayoutOutput)==64);
struct Request {
    uint32_t width,height,depth,levels,format,tile,fragments;
    uint8_t cube;
    bool volume;
};
constexpr uint32_t next_pow2(uint32_t n){
    --n; n|=n>>1;n|=n>>2;n|=n>>4;n|=n>>8;n|=n>>16;return n+1;
}
inline bool accepted(const Request& q) {
    // Original converts the unsigned dimension to float, extracts the
    // exponent and performs an unsigned comparison (including zero).
    const uint32_t maxdim=q.volume?std::max({q.width,q.height,q.depth}):std::max(q.width,q.height);
    const uint32_t maxlevels=(std::bit_cast<uint32_t>(float(maxdim))>>23)-126u;
    return q.levels&&q.levels<=maxlevels&&(q.volume||
        ((q.cube!=1||q.width==q.height)&&(q.levels==1||q.fragments==0)));
}
constexpr LayoutInput input(const Request& q) {
    return {q.tile,q.format,q.width,q.height,q.volume?q.depth:1u,
        q.volume?1u:1u<<(q.fragments&31),0,0,0,
        (uint32_t(q.levels>1)<<12)|(q.volume?0x20u:uint32_t(q.cube)<<4)};
}
constexpr uint32_t initial_word(const Request& q){return (2u|uint32_t(q.levels>1))<<25;}
constexpr uint32_t type(const Request& q){
    if(q.volume)return 10;
    if(q.cube)return 11;
    if(int32_t(q.fragments)>0)return q.depth==1?14:15;
    return (q.height==1?8u:9u)+(q.depth==1?0u:4u);
}
// A freshly zeroed 32-byte descriptor, with the exact original masks.
inline void encode(uint32_t (&w)[8],const Request& q,uint32_t pitch,uint32_t tile_index){
    w[1]=((q.format&63)<<20)|((q.format&0xf00)<<18);
    w[2]=((q.width-1)&0x3fff)|(((q.height-1)&0x3fff)<<14);
    const uint32_t slices=q.volume||q.levels<=1?q.depth:next_pow2(q.depth);
    w[4]=((pitch-1)&0x3fff)<<13 | ((slices-1)&0x1fff);
    const uint32_t last=(!q.volume&&int32_t(q.fragments)>0)?q.fragments:q.levels-1;
    w[3]=(type(q)<<28)|initial_word(q)|((q.format>>12)&0xfff)|
        ((last&15)<<16)|((tile_index&31)<<20);
    // F83916 clears bit 24 of the old word before inserting the tile.
    w[5]=q.volume?0:(((q.cube?q.depth*6:q.depth)-1)&0x1fff)<<13;
}
constexpr uint64_t packed_result(uint64_t bytes,uint32_t alignment){
    return (uint64_t(alignment)<<32)|(bytes<=UINT32_MAX?bytes:0);
}
// Retain exact early-reject / zeroed-partial / complete-descriptor
// failure stages. Layout and footprint callbacks may still be retail.
template<class Layout,class Footprint>
uint64_t construct(void* descriptor,const Request& q,Layout layout,Footprint footprint){
    if(!accepted(q))return 0;
    uint32_t w[8]{};
    w[3]=initial_word(q);
    std::memcpy(descriptor,w,sizeof w);
    const auto in=input(q);
    LayoutOutput out{};
    if(layout(out,in)!=0)return 0;
    encode(w,q,out.words[0],out.words[12]);
    std::memcpy(descriptor,w,sizeof w);
    uint64_t bytes=0;uint32_t alignment=0;
    if(footprint(bytes,alignment,descriptor)!=0)return 0;
    return packed_result(bytes,alignment);
}
} // namespace d1::ng::texture_ctor
