#include "native_surface_layout.h"
#include <cstdio>
#include <cstdlib>
using namespace d1::ng::surface_layout;
static void check(bool x){if(!x)std::abort();}
int main(){
    uint64_t cases=0;
    for(uint32_t bits:{8u,16u,32u,64u,128u})
    for(uint32_t w=1;w<=257;++w)
    for(uint32_t h=1;h<=129;++h)
    for(uint32_t mode=0;mode<2;++mode)
    for(uint32_t pad:{0u,0x1000u}){
        LayoutInput q{8,10,w,h,1,1,0,0,0,pad};
        LayoutOutput o{};
        check(linear(o,q,bits,mode));
        check(o.words[0]>=w&&o.words[1]>=h&&o.words[2]==1);
        check(o.words[0]%o.words[8]==0&&o.words[6]==mode&&o.words[12]==8);
        const uint64_t size=uint64_t(o.words[4])|(uint64_t(o.words[5])<<32);
        check(size==uint64_t(o.words[0])*o.words[1]*(bits/8));
        check(size%o.words[7]==0);
        if(mode==1)check(uint64_t(o.words[0])*o.words[1]%std::max(64u,256u/(bits/8))==0);
        if(!mode&&!pad)check(o.words[0]==w&&o.words[1]==h);
        check(equivalent(o,o));
        ++cases;
    }
    LayoutInput q{8,10,32,16,1,1,64,1,0,0};
    LayoutOutput o{};
    check(linear(o,q,32,1)&&o.words[0]==32&&o.words[4]==2048);
    q.samples=4;check(!linear(o,q,32,1));
    q.samples=1;q.format=35;check(!linear(o,q,4,1));
    q.format=10;check(!linear(o,q,32,4));
    std::printf("native linear surface layout: %llu candidate cases passed\n",(unsigned long long)cases);
}
