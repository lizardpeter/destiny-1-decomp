#include "native_resource_identity.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_map>
#include <vector>
using namespace d1::ng;
static void check(bool v){if(!v)std::abort();}
int main(){
    ResourceRegistry<10> r;
    check(!r.get(0x10000).identity);
    check(r.touch(0x10000,true,5));
    auto first=r.get(0x10000);
    check(first.handle==5&&uint32_t(first.identity)==1);
    uint64_t published=0;
    check(r.touch(0x10000,true,5,&published));
    auto next=r.get(0x10000);
    check(next.identity==first.identity+1&&published==next.identity);
    check(!r.touch(0x10000,false,6,&published)&&!published);
    check(!r.touch(0x10000,false,6));
    check(r.touch(0x10000,true,6));
    auto changed=r.get(0x10000);
    check(changed.handle==6&&changed.identity>>32!=first.identity>>32&&uint32_t(changed.identity)==1);
    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    // Each new handle has an identity with a predictable resource id, because
    // only this one slot is written and each update changes its handle.
    for(unsigned i=0;i<4;++i)readers.emplace_back([&]{
        while(!stop.load()){
            auto v=r.get(0x20000);
            if(v.identity)check((v.identity>>32)==uint64_t(v.handle)+2&&uint32_t(v.identity)==1);
        }
    });
    for(uint32_t i=1;i<=100000;++i)check(r.touch(0x20000,true,i));
    stop=true;for(auto& t:readers)t.join();
    // A reused package may have exactly the same object address and handle.
    ResourceRegistry<8> pools;
    const uint32_t handle=(42u<<13)|7;
    check(handle_pool(handle)==42);
    check(handle_pool(0x80000000u|(0x2345u<<13)|7)==0x2345);
    check(pools.touch(0x40000,true,handle));
    const auto old=pools.get(0x40000);
    check(pools.begin_pool_retire(42));
    check(!pools.current(old)&&!pools.get(0x40000).identity);
    check(!pools.touch(0x40000,true,handle));
    pools.end_pool_retire(42);
    check(!pools.touch(0x40000,false,handle));
    check(pools.touch(0x40000,true,handle));
    check(pools.get(0x40000).identity>>32!=old.identity>>32);
    for(unsigned generation=0;generation<10000;++generation){
        for(unsigned i=0;i<32;++i)
            check(pools.touch(0x100000+uint64_t(generation)*0x1000+i*16,true,(42u<<13)|i));
        check(pools.begin_pool_retire(42));
        pools.end_pool_retire(42);
        for(unsigned i=0;i<32;++i)
            check(!pools.get(0x100000+uint64_t(generation)*0x1000+i*16).identity);
    }
    struct Collision { size_t operator()(const GeometryKey&) const {return 0;} };
    std::unordered_map<GeometryKey,int,Collision> m;
    m[{1,0,64,0,0}]=1;m[{1,0,64,2,32}]=2;m[{1,0,64,4,16}]=3;m[{2,0,64,0,0}]=4;
    check(m.size()==4&&m.at({1,0,64,2,32})==2&&m.at({1,0,64,4,16})==3);
    std::puts("100000 native resource generations and collision-safe geometry identity checks passed");
}
