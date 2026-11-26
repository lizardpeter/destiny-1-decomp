#include "native_small_surface_layout.h"
#include <cstdlib>
#include <cstdio>
using namespace d1::ng::texture_ctor;
static void check(bool v){if(!v)std::abort();}
int main(){
    unsigned count=0;
    for(uint32_t mode=0;mode<4;++mode)
        for(uint32_t bits:{8u,16u,32u,64u,128u})
            for(uint32_t w=1;w<=128;++w)
                for(uint32_t h=1;h<=128;++h){
                    const uint32_t thick=mode==3?4:1;
                    LayoutInput in{8,1,w,h,1,1,0,0,0,0};
                    LayoutOutput out{};
                    out.words[3]=0xdeadbeef;out.words[13]=0x12345678;
                    check(small_surface_layout(out,in,mode<<2,bits,thick));
                    const auto* a=out.words;
                    check(a[0]>=w&&a[1]>=h&&a[2]==thick&&a[6]==mode&&a[11]==bits&&a[12]==8);
                    check(a[3]==0xdeadbeef&&a[13]==0x12345678);
                    const uint64_t size=uint64_t(a[4])|(uint64_t(a[5])<<32);
                    check(size==uint64_t(a[0])*a[1]*bits/8*thick);
                    check(a[0]%a[8]==0);
                    if(mode)check(size%256==0);
                    if(mode==0)check(a[0]==w&&a[1]==h&&a[7]==bits/8);
                    if(mode==1)check(uint64_t(a[0])*a[9]%std::max(64u,256u/(bits/8))==0);
                    if(mode>=2)check(a[1]%8==0);
                    ++count;
                }
    LayoutInput mip{8,1,17,9,1,1,128,1,0,0};
    LayoutOutput out{};
    check(small_surface_layout(out,mip,0,32,1)&&out.words[0]==64&&out.words[1]==9);
    mip.flags=0x1000;check(small_surface_layout(out,mip,0,32,1)&&out.words[1]==16);
    mip.flags=2;check(!small_surface_layout(out,mip,0,32,1));
    mip.flags=0;mip.format=0x23;check(!small_surface_layout(out,mip,0,8,1));
    std::printf("%u small surface layout invariant cases passed\n",count);
}
