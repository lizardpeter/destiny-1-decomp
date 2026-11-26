#pragma once
#include <cstdint>
#include <array>
namespace d1::ng {
// A split texture may change its physical tile mode, pow2 padding and
// 1D/2D descriptor type as the engine replaces the resident mip range.
// Those are source layout attributes, not image identity attributes.
inline std::array<uint32_t,8> texture_identity_words(const uint32_t* t){
    std::array<uint32_t,8> c{};
    for(unsigned i=0;i<8;++i)c[i]=t[i];
    const uint32_t type=t[3]>>28;
    c[0]=0;c[1]&=~0xfffffu;
    if(type==10||type==12||type==13){c[3]&=~0xf000u;return c;}
    c[2]=0;c[3]&=~0xff000u;c[4]&=0x1fffu;
    if(type==8||type==9||type==11){
        c[3]&=~0x03f00000u;
        if(type==8)c[3]=(c[3]&0x0fffffffu)|(9u<<28);
    }
    return c;
}
// Same byte interpretation, ignoring resource address and view-only fields.
inline bool texture_source_layout_equal(const uint32_t* a,const uint32_t* b){
    return (a[1]&0xfff00000u)==(b[1]&0xfff00000u)&&a[2]==b[2]&&
           (a[3]&0xf3ff0000u)==(b[3]&0xf3ff0000u)&&a[4]==b[4]&&
           a[5]==b[5]&&a[6]==b[6]&&a[7]==b[7];
}
} // namespace d1::ng
