#include "native_memory_policy.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::ng;
static void check(bool v){if(!v)std::abort();}
int main(){
    const uint32_t flags[]={1,2,6,7,14};
    auto o=memory_type_order(31,flags,5,6,1);
    check(o.count==3&&o.indices[0]==3&&o.indices[1]==2&&o.indices[2]==4);
    check(memory_type_order(3,flags,5,6,1).count==0);
    std::mt19937 rng(0xD10133);
    for(unsigned it=0;it<100000;++it){
        uint32_t f[32]; for(auto& v:f)v=rng()&31;
        uint32_t bits=rng(),req=rng()&31,pref=rng()&31,n=rng()%33;
        auto r=memory_type_order(bits,f,n,req,pref);
        uint32_t seen=0; bool unpreferred=false;
        for(size_t j=0;j<r.count;++j){
            uint32_t i=r.indices[j],mask=uint32_t(1)<<i;
            check(i<n&&(bits&mask)&&!(seen&mask)&&(f[i]&req)==req);
            seen|=mask;
            if((f[i]&pref)!=pref)unpreferred=true;
            else check(!unpreferred);
        }
        for(uint32_t i=0;i<n;++i)check(bool(seen&(uint32_t(1)<<i))==bool((bits&(uint32_t(1)<<i))&&(f[i]&req)==req));
    }
    std::puts("100000 native Vulkan memory property selection cases passed");
}
