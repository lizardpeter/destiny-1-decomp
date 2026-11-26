#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <bit>

namespace d1::ng::binding {
// 01.33 resource object layouts. Writes deliberately preserve the fields
// read by the remaining retail callers; this is not a new GPU descriptor ABI.
template<class T> inline T load(const void* object,size_t off) {
    T value;std::memcpy(&value,static_cast<const uint8_t*>(object)+off,sizeof value);return value;
}
template<class T> inline void store(void* object,size_t off,T value) {
    std::memcpy(static_cast<uint8_t*>(object)+off,&value,sizeof value);
}
struct BufferSource { uint64_t base=0,size=0; uint32_t count=0,width=0; };
inline BufferSource vertex_source(const void* object) {
    return {load<uint64_t>(object,0x28),load<uint32_t>(object,0),load<uint32_t>(object,0x30),load<uint32_t>(object,4)};
}
inline BufferSource index_source(const void* object) {
    const uint32_t width=load<uint32_t>(object,0x38)?4:2,count=load<uint32_t>(object,0x3c);
    return {load<uint64_t>(object,0x30),uint64_t(count)*width,count,width};
}
inline void texture_base(void* object,uint64_t address) {
    // 0x7D61A0 -> 0xF841B0 / 0xF839A0: truncate address>>8 to 32 bits,
    // clear base high bits, set memory type 0x10 without touching other bits.
    store(object,0,uint32_t(address>>8));
    store(object,4,load<uint32_t>(object,4)&0x3fffff00u);
    store(object,12,load<uint32_t>(object,12)|0x04000000u);
}
inline void index_fields(void* object,uint32_t count,uint64_t address,uint32_t width) {
    store(object,0x3c,count);store(object,0x30,address);store(object,0x38,uint32_t(width!=2));
}
enum class Result { unsupported, rejected, bound };
constexpr bool supported(uint32_t type,bool repoint) {
    return (type>=1&&type<=4)||(type>=6&&type<=13)||(type==14&&!repoint)||type==16||type==18;
}
inline Result apply(uint32_t type,bool repoint,void* object,uint64_t address,size_t extent=0) {
    if(!supported(type,repoint))return Result::unsupported;
    if(type==16)return Result::bound; // 0x1F70: constant true, no object access
    if(!object)return Result::rejected;
    if(type==18){store(object,8,address);return Result::bound;}
    // Initial texture bind has no address-null guard; re-point does.
    if(!address&&(repoint||type>=4))return Result::rejected;
    switch(type){
        case 1:case 2:case 3:texture_base(object,address);break;
        case 4:{
            const uint32_t stride=load<uint32_t>(object,4);
            if(!repoint&&!stride)return Result::rejected; // avoid invalid original divide
            store(object,0x28,address);
            if(!repoint)store(object,0x30,load<uint32_t>(object,0)/stride);
            break;
        }
        case 6:{
            if(repoint)store(object,0x30,address);
            else {
                const uint32_t width=2+2*(load<uint8_t>(object,1)&1);
                const int64_t bytes=std::bit_cast<int64_t>(load<uint64_t>(object,8));
                index_fields(object,uint32_t(bytes/int64_t(width)),address,width);
            }
            break;
        }
        case 7:
            store(object,0,uint32_t(address));
            store(object,4,(load<uint32_t>(object,4)&0xfffff000u)|uint32_t((address>>32)&0xfff));
            break;
        case 8:case 12:case 13:
            store(object,8,uint32_t(address>>8));store(object,12,uint32_t(address>>40));break;
        case 9:
            store(object,0x1c,uint32_t(address>>8));store(object,0x20,uint32_t(address>>40));
            store(object,8,load<uint32_t>(object,0x10)==2?uint64_t(0):address+load<uint32_t>(object,4));
            break;
        case 11:case 14:
            store(object,0x18,uint32_t(address>>8));store(object,0x1c,uint32_t(address>>40));
            store(object,8,address+load<uint32_t>(object,4));break;
        case 10:{
            // GS includes an aligned second program after its variable metadata.
            // 0x7D4AB0 uses the high byte of word zero as the metadata count.
            if(extent<4)return Result::rejected;
            const uint32_t n=load<uint8_t>(object,3),off=4*n;
            if(extent<off+0x5e)return Result::rejected;
            const uint32_t metadata=((uint32_t(load<uint8_t>(object,off+0x3b))+
                                     load<uint8_t>(object,off+0x5c))*4+0x2b+
                                     uint32_t(load<uint8_t>(object,off+0x5d))*2)&0xfffffffcu;
            const uint64_t second=address+((metadata+off+0x137)&0xffffff00u);
            store(object,8,uint32_t(address>>8));store(object,12,uint32_t(address>>40));
            store(object,off+0x40,uint32_t(second>>8));store(object,off+0x44,uint32_t(second>>40));
            break;
        }
        default:break;
    }
    return Result::bound;
}
} // namespace d1::ng::binding
