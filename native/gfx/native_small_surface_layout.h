#pragma once
#include "native_texture_constructor.h"
#include <numeric>
namespace d1::ng::texture_ctor {
// F85CB0 modes 0..3, bounded successful single-sample, uncompressed,
// non-volume requests. Original table supplies bits and array thickness.
// Unsupported inputs return false, without modifying the output.
inline bool small_surface_layout(LayoutOutput& out,const LayoutInput& in,
                                 uint32_t table,uint32_t bits,uint32_t thick){
    const uint32_t mode=array_mode(table),df=in.format&255;
    if(in.tile>=32||mode>3||df>=0x3b||(df>=0x23&&df<0x2a)||
       (in.flags&~0x1000u)||in.depth!=1||in.samples!=1||in.mip>15||
       !in.width||!in.height||in.width>16384||in.height>16384||
       (in.mip?in.base_pitch<in.width||in.base_pitch>16384:in.base_pitch!=0)||
       bits<8||bits>128||!std::has_single_bit(bits)||
       thick!=(mode==3?4u:1u))return false;
    uint32_t w=in.mip?std::max(1u,in.base_pitch>>in.mip):in.width;
    uint32_t h=in.height;
    if(in.flags&0x1000){w=next_pow2(w);h=next_pow2(h);}
    const uint32_t bytes=bits/8;
    const uint32_t pa=mode==0?1u:mode==1?std::max(8u,64u/bytes):8u;
    uint32_t ha=mode<2?1u:8u;
    const uint32_t ba=mode==0?bytes:256u;
    uint32_t p=(w+pa-1)&~(pa-1);
    h=(h+ha-1)&~(ha-1);
    if(mode==1){
        const uint32_t group=std::max(64u,256u/bytes);
        while(uint64_t(p)*h%group)p+=pa;
        ha=group/std::gcd(group,p);
    }else if(mode>=2){
        while((uint64_t(p)*h*bytes*thick)%256)p+=pa;
        // The original micro-tile multiply is 32-bit before shifting.
        // Leave overflow cases to the original until separately verified.
        if(uint64_t(p)*h*bits>UINT32_MAX)return false;
    }
    const uint64_t size=uint64_t(p)*h*bytes*thick;
    out.words[0]=(in.flags&0x1000)?next_pow2(p):p;
    out.words[1]=h;out.words[2]=thick;
    out.words[4]=uint32_t(size);out.words[5]=uint32_t(size>>32);
    out.words[6]=mode;out.words[7]=ba;out.words[8]=pa;out.words[9]=ha;
    out.words[10]=thick;out.words[11]=bits;out.words[12]=in.tile;
    return true;
}
inline bool small_layout_equal(const LayoutOutput& a,const LayoutOutput& b){
    for(unsigned i:{0u,1u,2u,4u,5u,6u,7u,8u,9u,10u,11u,12u})
        if(a.words[i]!=b.words[i])return false;
    return true;
}
} // namespace d1::ng::texture_ctor
