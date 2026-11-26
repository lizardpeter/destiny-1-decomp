#include "native_geometry_sources.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_set>
using namespace d1::ng;
static void check(bool v){if(!v)std::abort();}
int main(){
    GeometrySources store(128);
    GeometryKey old{0x100000001ull,0x8000,64,0,0};
    GeometryKey next{0x100000002ull,0x8000,64,0,0};
    GeometryKey other{0x200000001ull,0x9000,64,0,0};
    unsigned reads=0;
    auto a=store.capture(old,[&](uint8_t* p,uint64_t n){++reads;std::memset(p,0x31,n);return true;});
    check(a&&a->size()==64&&a->at(0)==0x31&&store.bytes()==64);
    auto alias=store.capture(old,[&](uint8_t*,uint64_t){++reads;return false;});
    check(alias==a&&reads==1);
    auto b=store.capture(next,[&](uint8_t* p,uint64_t n){++reads;std::memset(p,0x72,n);return true;});
    check(b&&b!=a&&b->at(0)==0x72&&a->at(0)==0x31&&store.bytes()==128);
    check(!store.capture(other,[&](uint8_t*,uint64_t){++reads;return true;})&&reads==2);
    a.reset();alias.reset();b.reset();
    store.prune([&](uint64_t id){return id==old.resource;});
    check(store.bytes()==64&&store.get(old)&&!store.get(next));
    auto failure=store.capture(other,[](uint8_t* p,uint64_t n){std::memset(p,0x99,n);return false;});
    check(!failure&&store.bytes()==64&&!store.get(other));
    auto held=store.get(old);
    store.prune([](uint64_t){return false;});
    check(held&&held->at(1)==0x31&&store.bytes()==64);
    held.reset();
    store.prune([](uint64_t){return false;});
    check(store.bytes()==0&&!store.get(old));
    GeometrySources shared(1024);
    const GeometryKey key{0x300000001ull,0xa000,1024,0,0};
    std::atomic<unsigned> calls{0};
    std::thread workers[8];
    for(auto& worker:workers)worker=std::thread([&]{
        for(unsigned i=0;i<1000;++i){
            auto source=shared.capture(key,[&](uint8_t* p,uint64_t n){++calls;std::memset(p,0x5a,n);return true;});
            check(source&&source->size()==1024&&source->at(i%1024)==0x5a);
        }
    });
    for(auto& worker:workers)worker.join();
    check(calls==1&&shared.bytes()==1024);
    shared.prune([](uint64_t){return false;});
    check(shared.bytes()==0);
    std::puts("native geometry: exact epoch, failed capture, pressure, pin/consumer ownership and 8000 concurrent acquisitions passed");
}
