#include "native_descriptor_slots.h"
#include <cstdlib>
#include <cstdio>
using namespace d1::ng;
static void check(bool v){if(!v)std::abort();}
int main(){
    DescriptorSlots<12,16384> slots;
    for(unsigned b=0;b<12;++b){
        for(unsigned i=1;i<16384;++i)check(slots.acquire(b)==i);
        check(!slots.acquire(b)&&slots.live(b)==16383&&slots.peak(b)==16383);
    }
    for(unsigned i=1;i<16384;++i)slots.release(3,uint16_t(i));
    check(!slots.live(3)&&slots.live(2)==16383&&!slots.acquire(2));
    for(unsigned i=16383;i;--i)check(slots.acquire(3)==i);
    check(!slots.acquire(3)&&!slots.acquire(12));
    std::puts("196596 binding-local descriptors, independent exhaustion and recycling passed");
}
