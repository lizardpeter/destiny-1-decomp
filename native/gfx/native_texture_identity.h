#pragma once
#include <cstdint>
#include <array>
#include <cstddef>
namespace d1::ng {
// A split texture may change its physical tile mode, pow2 padding and
// 1D/2D descriptor type as the engine replaces the resident mip range.
// Those are source layout attributes, not image identity attributes.
inline std::array<uint32_t,8> texture_identity_words(const uint32_t* t){
    std::array<uint32_t,8> c{};
    for(unsigned i=0;i<8;++i)c[i]=t[i];
    const uint32_t type=t[3]>>28;
    c[0]=0;c[1]&=~0xfffffu;
    // Component selection belongs to the VkImageView, not the stored image.
    // All swizzle variants of a registered source share the same allocation.
    c[3]&=~0xfffu;
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
// Native image identity is an exact key; a hash chooses a bucket, never
// decides whether a different asset or descriptor owns the same VkImage.
struct TextureKey {
    uint32_t resource=0;
    std::array<uint32_t,8> words{};
    bool operator==(const TextureKey&) const = default;
};
inline TextureKey texture_key(const uint32_t* t,uint64_t epoch){
    TextureKey key{};
    key.resource=uint32_t(epoch>>32);
    if(epoch)key.words=texture_identity_words(t);
    else for(unsigned i=0;i<8;++i)key.words[i]=t[i];
    return key;
}
struct TextureKeyHash {
    size_t operator()(const TextureKey& key) const noexcept {
        uint64_t h=key.resource;
        for(unsigned i=0;i<8;i+=2){
            const uint64_t v=uint64_t(key.words[i])|(uint64_t(key.words[i+1])<<32);
            h=(h*0xC2B2AE3D27D4EB4Full+v)*0x9E3779B97F4A7C15ull;
            h^=h>>31;
        }
        return size_t(h);
    }
};
} // namespace d1::ng
