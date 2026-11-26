#include "native_resource_binding.h"
#include "native_mip_source.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::ng::binding;
static void check(bool b){if(!b)std::abort();}
int main(){
    using d1::ng::MipSource;
    using d1::ng::mip_source;
    static_assert(mip_source(false,true,0,0x10000,0,640,640,512)==MipSource::contiguous_high);
    static_assert(mip_source(false,true,0x20000,0x10000,128,640,640,512)==MipSource::contiguous_high);
    static_assert(mip_source(false,true,0x20000,0x10000,128,512,640,512)==MipSource::split_chain);
    static_assert(mip_source(false,true,0,0x10000,0,512,640,512)==MipSource::high_only);
    static_assert(mip_source(false,true,0,0x10000,0,511,640,512)==MipSource::invalid);
    static_assert(mip_source(true,false,0x20000,0,128,0,128,64)==MipSource::low_chain);
    static_assert(mip_source(true,false,0x20000,0,127,0,128,64)==MipSource::invalid);

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
        if((i&16)==0)store(obj.data(),0x10,uint32_t(2));
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
    for(uint32_t type=0;type<16;++type)
        for(uint32_t mip=0;mip<16;++mip)
            for(uint32_t full=0;full<256;++full)
                for(uint32_t high=0;high<2;++high) {
                    std::array<uint8_t,80> object{};
                    store(object.data(),12,(type<<28)|(mip<<16));
                    object[0x31]=uint8_t(full);object[0x34]=uint8_t(high);
                    check(last_mip(load<uint32_t>(object.data(),12))==(type>=14?0:mip));
                    check(release_top_mip(object.data())==(high!=0&&full==(type>=14?1:mip+1)));
                }
    for(unsigned i=0;i<100000;++i){
        std::array<uint8_t,80> object{};
        for(auto& byte:object)byte=uint8_t(rng());
        const uint32_t kind=i%3+1;
        store(object.data(),0x40,kind);
        const uint32_t w=load<uint16_t>(object.data(),0x28),h=load<uint16_t>(object.data(),0x2a);
        const uint32_t d=load<uint16_t>(object.data(),0x2c),l=object[0x31];
        const uint32_t word1=load<uint32_t>(object.data(),4),word3=load<uint32_t>(object.data(),12);
        const uint64_t low=rng(),high=(i&1)?rng()|1:0;
        auto p=mip_plan(object.data(),low,high);
        auto half=[&](uint32_t n){return n<=3?1:n/2;};
        auto rounded=[&](uint32_t n){return object[0x35]?((half(n)+3)/4*4):half(n);};
        check(p.width==(high?w:rounded(w))&&p.height==(high?h:rounded(h)));
        check(p.depth==(kind==3?(high?d:half(d)):1));
        check(p.levels==(high?l:l-1)&&p.base==(high?high:low));
        check(p.volume==(kind==3)&&p.cube==(kind==2));
        check(p.tile==(word3>>20&31));
        uint32_t packed=(word1>>20&63)|(word1>>26&15)<<8;
        for(unsigned j=0;j<4;++j)packed|=(word3>>(j*3)&7)<<(12+j*3);
        check(p.format==packed);
        check(p.fragments==(kind==2||word3>>28<14?0:word3>>16&15));
        auto expected=object;
        store(expected.data(),0,uint32_t(p.base>>8));expected[4]&=0xc0;
        mip_base(object.data(),p.base);
        check(object==expected);
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
