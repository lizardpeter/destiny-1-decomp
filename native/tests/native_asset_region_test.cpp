#include "native_asset_region.h"
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using namespace d1::asset_region;
static void check(bool pass){if(!pass)std::abort();}
int main(){
    constexpr uint64_t b=0x500000000ull, a=alignment;
    check(!reserve_size(0)&&reserve_size(1)==a&&reserve_size(a+1)==2*a);
    check(!reserve_size(UINT64_MAX));
    check(!plan(b,4*a,0,0,a,false,true).address);
    check(!plan(b,4*a,3*a,2*a,a,false,false).address);
    check(!plan(b,4*a,0,0,0,false,false).address);
    check(!plan(b,4*a,0,0,a-1,false,false).address);
    check(!plan(b+1,4*a,0,0,a,false,false).address);
    check(!plan(address_limit-a,2*a,0,0,a,false,false).address);
    check(!overlaps(b,a,b+a,a)&&overlaps(b,a,b+a-1,a));
    check(overlaps(UINT64_MAX-10,10,UINT64_MAX-5,1));
    uint64_t cases=0;
    for(uint64_t cap=1;cap<=32;++cap)
        for(uint64_t low=0;low<=cap;++low)
            for(uint64_t high=0;high<=cap-low;++high)
                for(uint64_t n=1;n<=cap+1;++n)
                    for(bool top:{false,true}){
                        auto p=plan(b,cap*a,low*a,high*a,n*a,top,false);
                        if(n>cap-low-high)check(!p.address);
                        else{
                            check(p.address==b+(top?cap-high-n:low)*a);
                            check(p.next_cursor==(top?high+n:low+n)*a);
                            check(!overlaps(p.address,n*a,b,low*a));
                            check(!overlaps(p.address,n*a,b+(cap-high)*a,high*a));
                        }
                        ++cases;
                    }
    std::printf("native asset region: %llu layout cases passed\n",(unsigned long long)cases);
}
