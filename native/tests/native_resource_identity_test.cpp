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
    check(r.touch(0x10000,true,5));
    auto next=r.get(0x10000);
    check(next.identity==first.identity+1);
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
    struct Collision { size_t operator()(const GeometryKey&) const {return 0;} };
    std::unordered_map<GeometryKey,int,Collision> m;
    m[{1,0,64,0,0}]=1;m[{1,0,64,2,32}]=2;m[{1,0,64,4,16}]=3;m[{2,0,64,0,0}]=4;
    check(m.size()==4&&m.at({1,0,64,2,32})==2&&m.at({1,0,64,4,16})==3);
    std::puts("100000 native resource generations and collision-safe geometry identity checks passed");
}
