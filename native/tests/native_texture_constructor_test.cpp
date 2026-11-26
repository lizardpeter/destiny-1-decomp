#include "native_texture_constructor.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace d1::ng::texture_ctor;
static void check(bool x){if(!x)std::abort();}
int main(){
    uint64_t cases=0;
    for(uint32_t w:{1u,4u,64u,1920u})
    for(uint32_t h:{1u,4u,64u,1080u})
    for(uint32_t dep:{1u,3u,8u})
    for(uint32_t levels=0;levels<=16;++levels)
    for(uint32_t fragments=0;fragments<=3;++fragments)
    for(uint32_t kind=0;kind<3;++kind){
        Request q{w,h,dep,levels,0xabc123u,14,fragments,uint8_t(kind==1),kind==2};
        if(q.volume)q.fragments=0;
        std::array<uint32_t,12> words;words.fill(0xdeadbeefu);
        unsigned called=0;
        auto r=construct(words.data(),q,[&](LayoutOutput& out,const LayoutInput& in){
            ++called;
            check(in.width==w&&in.height==h&&in.format==q.format&&in.tile==14);
            check(in.flags==((levels>1?0x1000u:0)|(q.volume?32u:uint32_t(q.cube)*16)));
            check(in.depth==(q.volume?dep:1u));
            check(in.samples==(q.volume?1u:1u<<q.fragments));
            check(words[3]==initial_word(q));
            out.words[0]=128;out.words[12]=14;return 0;
        },[&](uint64_t& size,uint32_t& align,void*){
            ++called;size=4096;align=256;return 0;
        });
        if(accepted(q)){
            check(called==2&&r==packed_result(4096,256));
            check((words[2]&0x3fff)==w-1&&((words[2]>>14)&0x3fff)==h-1);
            check(((words[4]>>13)&0x3fff)==127);
            check((words[4]&0x1fff)==((q.volume||levels==1?dep:next_pow2(dep))-1));
            check((words[3]>>28)==type(q)&&((words[3]>>20)&31)==14);
            check((words[3]&0xfff)==((q.format>>12)&0xfff));
            check(words[0]==0&&words[6]==0&&words[7]==0);
        }else{
            check(!called&&!r);
            for(auto x:words)check(x==0xdeadbeefu);
        }
        for(unsigned i=8;i<12;++i)check(words[i]==0xdeadbeefu);
        ++cases;
    }
    Request q{64,64,1,4,0,14,0,0,false};
    uint32_t words[8]{};
    check(!construct(words,q,[](auto&,const auto&){return 1;},[](auto&,auto&,void*){std::abort();return 0;}));
    check(words[3]==initial_word(q));
    check(!construct(words,q,[](auto& o,const auto&){o.words[0]=64;return 0;},[](auto&,auto&,void*){return 1;}));
    check(words[2]==(63u|(63u<<14)));
    check(packed_result(0x100000000ull,256)==(256ull<<32));
    Request cube{64,64,3,7,0,14,0,1,false};
    uint32_t cw[8]{};
    encode(cw,cube,128,14);
    auto seed=input(cube);
    uint64_t size=0;uint32_t align=0,count=0;
    check(!footprint(size,align,cw,seed,[&](LayoutOutput& o,const LayoutInput& i){
        check(i.mip==count&&i.width==(64u>>count)&&i.height==i.width&&i.depth==1);
        check(i.base_pitch==(count?128u:0u));
        ++count;o.words[4]=100;o.words[7]=count*256;return 0u;
    }));
    // Four encoded cube groups, six faces each, rounded to 32.
    check(count==7&&size==7*100*32&&align==7*256);
    count=0;
    check(footprint(size,align,cw,seed,[&](LayoutOutput& o,const LayoutInput&){
        if(count++==2)return 42u;
        o.words[4]=100;o.words[7]=256;return 0u;
    })==42&&size==2*100*32&&align==256);
    seed.depth=2;
    check(footprint(size,align,cw,seed,[](auto&,const auto&){std::abort();return 0u;})==invalid);
    std::printf("native texture constructor: %llu cases passed\n",(unsigned long long)cases);
}
