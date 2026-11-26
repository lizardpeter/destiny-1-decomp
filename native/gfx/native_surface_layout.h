#pragma once
#include "native_texture_constructor.h"
#include <numeric>

namespace d1::ng::surface_layout {
using texture_ctor::LayoutInput;
using texture_ctor::LayoutOutput;
constexpr uint32_t round_up(uint32_t n,uint32_t a){return (n+a-1)&~(a-1);}
// Initial proven subset of F85CB0, modes 0 and 1. The immutable original
// format and tile tables supply bpp and array mode. Other modes and exotic
// formats remain outside this candidate; do not guess their alignment.
inline bool linear(LayoutOutput& out,const LayoutInput& q,uint32_t bits,uint32_t mode){
    if(mode>1||q.tile>=32||q.depth!=1||q.samples!=1||
       (q.flags&~0x1000u)||!q.width||!q.height||q.width>16384||q.height>16384||
       q.mip>15||(q.mip==0?q.base_pitch!=0:q.base_pitch<q.width)||
       q.base_pitch>16384||!std::has_single_bit(bits)||bits<8||bits>128||
       uint8_t(q.format)>=35)return false;
    uint32_t pitch=q.mip?std::max(1u,q.base_pitch>>q.mip):q.width;
    uint32_t height=q.height;
    if(q.flags&0x1000){
        pitch=texture_ctor::next_pow2(pitch);
        height=texture_ctor::next_pow2(height);
    }
    const uint32_t byte_width=bits/8;
    uint32_t pitch_align=1,height_align=1,base_align=byte_width;
    if(mode==1){
        base_align=256;
        pitch_align=std::max(8u,64u/byte_width);
        pitch=round_up(pitch,pitch_align);
        // F869FF..F86A29: ensure slice element count is divisible by
        // max(64, 256/bytesPerElement), then compute the row-group count.
        const uint32_t group=std::max(64u,256u/byte_width);
        const uint32_t required=group/std::gcd(group,height);
        pitch=round_up(pitch,std::max(pitch_align,required));
        height_align=group/std::gcd(group,pitch);
    }
    const uint64_t size=uint64_t(pitch)*height*byte_width;
    out.words[0]=pitch;out.words[1]=height;out.words[2]=1;
    out.words[4]=uint32_t(size);out.words[5]=uint32_t(size>>32);
    out.words[6]=mode;out.words[7]=base_align;out.words[8]=pitch_align;
    out.words[9]=height_align;out.words[10]=1;out.words[11]=bits;
    out.words[12]=q.tile;
    return true;
}
inline bool equivalent(const LayoutOutput& a,const LayoutOutput& b){
    // Original writes these fields; reserved words aren't compared.
    for(unsigned i: {0u,1u,2u,4u,5u,6u,7u,8u,9u,10u,11u,12u})
        if(a.words[i]!=b.words[i])return false;
    return true;
}
} // namespace d1::ng::surface_layout
