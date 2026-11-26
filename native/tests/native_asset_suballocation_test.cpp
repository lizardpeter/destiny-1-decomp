#include "native_asset_suballocation.h"
#include <cstdlib>
#include <cstdio>
using namespace d1::asset_suballocation;
using d1::asset_region::alignment;
static void check(bool x){if(!x)std::abort();}
int main(){
    constexpr uint64_t b=0x500000000ull,a=alignment;
    uint64_t cases=0;
    for(uint64_t cap=1;cap<=12;++cap)
        for(uint64_t lo=0;lo<=cap;++lo)
            for(uint64_t hi=0;hi<=cap-lo;++hi)
                for(uint64_t lu=0;lu<=lo;++lu)
                    for(uint64_t hu=0;hu<=hi;++hu)
                        for(uint64_t n=1;n<=cap+1;++n){
                            State s{b,lo*a,hi*a,cap*a,(lo+hi)*a,lu*a,hu*a};
                            auto l=allocate(s,n*a,false,true);
                            if(l.path!=Path::fallback){
                                check(l.address==b+lu*a&&l.used==(lu+n)*a);
                                check(l.low_backed>=l.used&&l.high_backed>=hu*a);
                                check(l.low_backed+l.high_backed<=cap*a);
                                check(l.low_backed+l.high_backed==s.committed+l.growth);
                                State next{b,l.low_backed,l.high_backed,s.capacity,s.committed+l.growth,l.used,s.high_used};
                                auto r=release(next,l.address,n*a,false,true,false,false);
                                check(r.handled&&r.success&&r.used==s.low_used);
                            }
                            auto h=allocate(s,n*a,true,true);
                            if(n<=hi-hu){
                                check(h.path==Path::ready&&h.address==b+(cap-hu-n)*a);
                                s.high_used=h.used;
                                auto r=release(s,h.address,n*a,true,true,false,true);
                                check(r.handled&&r.success&&!r.dirty&&r.used==hu*a);
                            }else if(lo+hi==cap&&n<=lo-lu){
                                check(h.path==Path::redistribute_high&&h.growth==0);
                                check(h.padding==(hi-hu)*a&&h.used==(hi+n)*a);
                                check(h.low_backed==(lo-n)*a&&h.high_backed==(hi+n)*a);
                                check(h.address==b+(cap-hi-n)*a);
                            }else if(n<=cap-lo-hi){
                                check(h.path==Path::grow_high&&h.growth==n*a);
                                check(h.padding==(hi-hu)*a&&h.used==(hi+n)*a);
                                check(h.address==b+(cap-hi-n)*a);
                                State next{b,h.low_backed,h.high_backed,s.capacity,s.committed+h.growth,s.low_used,h.used};
                                auto r=release(next,h.address,n*a,true,true,false,true);
                                check(r.handled&&r.success&&!r.dirty&&r.used==hi*a);
                            }else check(h.path==Path::fallback);
                            ++cases;
                        }
    State s{b,2*a,a,3*a,3*a,a,a};
    auto x=allocate(s,128,false,true);
    check(x.path==Path::ready&&x.used==a+128);
    check(!release(s,b+128,128,false,true,false,true).success);
    check(release(s,b+128,128,false,true,true,true).success);
    auto dirty=release(s,b,128,true,true,false,true);
    check(dirty.handled&&dirty.success&&dirty.dirty);
    check(!release(s,b+2*a,128,true,true,false,false).handled);
    auto inactive=release(s,b+2*a,128,true,true,false,false,{true});
    check(inactive.handled&&inactive.success&&inactive.used==0);
    inactive=release(s,b+2*a,128,true,true,false,false,{true,true,true,b+2*a,b+2*a+128});
    check(inactive.handled&&inactive.success&&inactive.used==a-128);
    inactive=release(s,b+2*a,128,true,true,false,false,{true,true,false,b+2*a+256,0});
    check(inactive.handled&&inactive.success&&inactive.used==a-256);
    inactive=release(s,b+2*a,128,true,true,false,false,{true,true,false,b+2*a,0});
    check(inactive.handled&&inactive.success&&inactive.used==0);
    inactive=release(s,b,128,true,true,false,false);
    check(inactive.handled&&inactive.success&&inactive.used==a&&!inactive.dirty);
    State grow{b,a,a,4*a,2*a,128,128};
    auto high=allocate(grow,a,false,true);
    check(high.path==Path::grow_low&&high.growth==a);
    high=allocate(grow,a,true,true);
    check(high.path==Path::grow_high&&high.padding==a-128&&high.growth==a);
    check(high.address==b+2*a&&high.used==2*a);
    State broken=s;broken.committed=1;
    check(allocate(broken,128,false,true).path==Path::fallback);
    std::printf("native asset suballocation: %llu layout cases passed\n",(unsigned long long)cases);
}
