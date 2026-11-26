#include "pc_thread_keys.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <vector>
using Keys=d1::pc_threads::Keys<void(*)(void*),64>;
static void check(bool v){if(!v)std::abort();}
static void unused(void*){}
int main(){
    Keys keys; Keys::Values a,b; uint32_t key=999; int first=1,second=2;
    check(keys.create(key,unused)&&key==0);
    check(keys.set(a,key,&first)&&keys.get(a,key)==&first&&keys.get(b,key)==nullptr);
    check(keys.erase(key)&&!keys.erase(key)&&keys.get(a,key)==nullptr);
    uint32_t reused=999; check(keys.create(reused,unused)&&reused==key);
    check(keys.get(a,key)==nullptr&&keys.set(b,key,&second));
    check(keys.drain(a,[&](auto,void*){std::abort();})==0);
    unsigned n=0;
    check(keys.drain(b,[&](auto d,void* p){check(d==unused&&p==&second&&keys.get(b,key)==nullptr);++n;check(keys.set(b,key,&second));})==4&&n==4);
    check(keys.get(b,key)==nullptr&&keys.erase(key));
    uint32_t all[64]; for(auto& k:all)check(keys.create(k,nullptr));
    check(!keys.create(key,nullptr)); for(auto k:all)check(keys.erase(k));
    std::atomic<unsigned> done{0}; std::vector<std::thread> threads;
    for(unsigned t=0;t<8;++t)threads.emplace_back([&]{
        Keys::Values values; int local=42;
        for(unsigned i=0;i<12500;++i){
            uint32_t k; check(keys.create(k,unused));
            check(keys.set(values,k,&local)&&keys.get(values,k)==&local);
            check(keys.drain(values,[&](auto d,void* p){check(d==unused&&p==&local);})==1);
            check(keys.get(values,k)==nullptr&&keys.erase(k)); ++done;
        }
    });
    for(auto& t:threads)t.join();
    check(done==100000);
    std::puts("100000 concurrent key lifecycle cycles, reuse and destructor passes passed");
}
