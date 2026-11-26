#include "native_resource_move.h"
#include <cstdio>
#include <cstdlib>
using namespace d1::ng::move;
static void check(bool x){if(!x)std::abort();}
int main(){
    uint64_t cases=0;
    for(uint32_t p=0;p<0x80000;++p){
        const uint32_t h=(p<<13)|0x123;
        const auto signed_part=int32_t(h)>>13;
        const uint32_t expected=(uint32_t(signed_part)>>18|0x3ff)&uint32_t(signed_part);
        check(pool_index(h)==expected);
        check(datum_offset(h,0x140)==0x123*0x140);
        ++cases;
    }
    check(datum_offset(0x1fff,0xffffffff)==uint32_t(0u-0x1fff));
    check(datum_address(0x500000000ull,0x100,0xff,0x1234)==0x5000000ccull);
    check(datum_address(0x500000000ull,0x100,-1,0x34)==0x5000000ccull);
    check(relocation_flags(0x100000001ull)==0xc0000001u);
    check(container_index(0xabcdef12)==0xef12&&container_generation(0xabcdef12)==0xcd);
    auto p=copy(0x1000,0x2000,0x80,0x100000040ull);
    check(p.src==0x1000&&p.dst==0x2080&&p.bytes==0x40);
    std::printf("native resource move: %llu handle cases passed\n",(unsigned long long)cases);
}
