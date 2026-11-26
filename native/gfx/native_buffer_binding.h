#pragma once
#include <cstdint>
namespace d1::ng {
// Source-defined Gnm descriptor leaves from 01.33 F7CF90/F7D180/F7D010.
// Retained only at the original ABI boundary; no original code is called.
constexpr void vertex_words(uint32_t* out,uint64_t address,uint32_t format,uint32_t stride,uint32_t count){
    out[0]=uint32_t(address);
    out[1]=(uint32_t(address>>32)&0xfffu)|((stride&0x3fffu)<<16);
    const uint32_t low=format&0xffu;
    const uint32_t special=(!(format&0x800u)&&low&&low<15)?(format&15u)<<15:0;
    out[2]=count;
    out[3]=((format&0x700u)<<4)|((format>>12)&0xfffu)|special|0x20000000u;
}
constexpr void constant_words(uint32_t* out,uint64_t address,uint32_t bytes){
    out[0]=uint32_t(address);
    out[1]=(uint32_t(address>>32)&0xfffu)|0x100000u;
    out[2]=(bytes+15u)>>4;
    out[3]=0x20077facu;
}
constexpr void buffer_memory_type(uint32_t* out,uint32_t type){
    out[3]=((type&0x1cu)<<25)|(out[3]&0xc7ffffffu);
    out[1]=((type&0x60u)<<7)|((type&3u)<<14)|(out[1]&0xffff0fffu);
}
// Original vertex binding uses the full count if the byte offset is zero,
// even when a nonzero explicit count is provided.
constexpr uint32_t vertex_records(uint32_t full,uint32_t offset,uint32_t stride,int32_t requested){
    return !offset?full:requested?uint32_t(requested):stride?full-offset/stride:full;
}
constexpr bool vertex_bind_changed(uint32_t old_handle,uint32_t old_offset,uint32_t handle,uint32_t offset){
    return handle!=UINT32_MAX&&(old_handle!=handle||old_offset!=offset);
}
} // namespace d1::ng
