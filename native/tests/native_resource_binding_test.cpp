#include "native_resource_binding.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::ng::binding;
static void check(bool b){if(!b)std::abort();}
int main(){
    std::mt19937_64 rng(0xd10133);
    for(unsigned i=0;i<100000;++i){
        std::array<uint8_t,80> obj{},expected{};
        for(auto& b:obj)b=uint8_t(rng());
        const uint64_t addr=rng()|1;
        const uint32_t type[]={1,2,3,4,6,8,18};
        const uint32_t t=type[i%7];
        const bool move=(i&1)!=0;
        if(t==4)store(obj.data(),4,uint32_t(rng())|1);
        expected=obj;
        auto* e=expected.data();
        if(t<=3){
            store(e,0,uint32_t(addr>>8));
            e[4]&=0xc0;
            store(e,4,load<uint32_t>(e,4)&0x3fffff3fu);
            store(e,12,(load<uint32_t>(e,12)&0xfbffffffu)|0x04000000u);
        } else if(t==4){
            store(e,0x28,addr);
            if(!move)store(e,0x30,load<uint32_t>(e,0)/load<uint32_t>(e,4));
        } else if(t==6){
            store(e,0x30,addr);
            if(!move){
                const uint32_t width=(e[1]&1)?4:2;
                store(e,0x3c,uint32_t(std::bit_cast<int64_t>(load<uint64_t>(e,8))/width));
                store(e,0x38,uint32_t(width==4));
            }
        } else if(t==8){store(e,8,uint32_t(addr>>8));store(e,12,uint32_t(addr>>40));}
        else store(e,8,addr);
        check(apply(t,move,obj.data(),addr)==Result::bound&&obj==expected);
        if(t==4){auto b=vertex_source(obj.data());check(b.base==addr&&b.size==load<uint32_t>(e,0)&&b.count==load<uint32_t>(e,0x30));}
        if(t==6){auto b=index_source(obj.data());check(b.base==addr&&b.size==uint64_t(b.count)*b.width);}
        check(apply(9,move,obj.data(),addr)==Result::unsupported&&obj==expected);
    }
    std::array<uint8_t,80> obj{};
    check(apply(4,false,obj.data(),1)==Result::rejected);
    check(apply(6,true,obj.data(),0)==Result::rejected);
    check(apply(1,true,obj.data(),0)==Result::rejected);
    check(apply(1,false,obj.data(),0)==Result::bound);
    check(apply(18,true,obj.data(),0)==Result::bound);
    std::puts("100000 source-derived resource binding and byte preservation cases passed");
}
