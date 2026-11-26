#include "native_shader_slots.h"
#include <cstdlib>
#include <cstdio>
using d1::ng::shader_slots;
int main(){
    for(uint32_t mask=0;mask<65536;++mask){
        uint64_t expanded=0;
        for(unsigned i=0;i<16;++i)if(mask&(1u<<i))expanded|=0xfull<<(4*i);
        if(shader_slots(expanded)!=mask)std::abort();
    }
    if(shader_slots(~0ull)!=0xffff||shader_slots(0)!=0)std::abort();
    std::puts("65536 translated texture-slot masks verified");
}
