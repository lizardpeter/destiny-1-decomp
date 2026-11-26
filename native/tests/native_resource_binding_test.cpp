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
        check(apply(15,move,obj.data(),addr)==Result::unsupported&&obj==expected);
    }
    for(unsigned i=0;i<100000;++i){
        std::array<uint8_t,1152> obj{},expected{};
        for(auto& b:obj)b=uint8_t(rng());
        const uint64_t addr=rng()|1;
        const uint32_t types[]={7,9,10,11,12,13,14,16};
        const uint32_t t=types[i%8];
        const bool move=(i&8)!=0;
        if(i%4==0)store(obj.data(),0x10,uint32_t(2));
        expected=obj;
        auto* e=expected.data();
        auto pair=[&](size_t off,uint64_t base){store(e,off,uint32_t(base>>8));store(e,off+4,uint32_t(base>>40));};
        if(t==7){store(e,0,uint32_t(addr));store(e,4,(load<uint32_t>(e,4)&~0xfffu)|uint32_t(addr>>32&0xfff));}
        else if(t==9){pair(0x1c,addr);store(e,8,load<uint32_t>(e,0x10)==2?uint64_t(0):addr+load<uint32_t>(e,4));}
        else if(t==10){
            const uint32_t n=e[3],off=n*4;
            const uint32_t m=(4u*(e[off+0x3b]+e[off+0x5c])+2u*e[off+0x5d]+43u)/4*4;
            pair(8,addr);pair(off+0x40,addr+((m+311+off)/256*256));
        } else if(t==11||(t==14&&!move)){pair(0x18,addr);store(e,8,addr+load<uint32_t>(e,4));}
        else if(t==12||t==13)pair(8,addr);
        const auto result=apply(t,move,obj.data(),addr,obj.size());
        check(result==(t==14&&move?Result::unsupported:Result::bound)&&obj==expected);
    }
    check(apply(16,false,nullptr,0)==Result::bound);
    std::array<uint8_t,80> obj{};
    check(apply(10,false,obj.data(),1,obj.size())==Result::rejected);
    check(apply(4,false,obj.data(),1)==Result::rejected);
    check(apply(6,true,obj.data(),0)==Result::rejected);
    check(apply(1,true,obj.data(),0)==Result::rejected);
    check(apply(1,false,obj.data(),0)==Result::bound);
    check(apply(18,true,obj.data(),0)==Result::bound);
    std::puts("200000 source-derived resource binding and byte preservation cases passed");
}
