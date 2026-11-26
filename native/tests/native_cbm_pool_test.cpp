#include "native_cbm_pool.h"
#include <atomic>
#include <array>
#include <thread>
#include <cstdio>
#include <cstdlib>
using namespace d1::ng::cbm;
static void check(bool b){if(!b)std::abort();}
int main(){
    auto reserved=[](Pool::Reservation,uint32_t,uint32_t){};
    auto allocated=[](uint32_t,uint32_t){};
    auto closed=[](uint32_t,uint32_t){};
    auto retired=[](uint32_t,uint32_t){};
    Pool pool(198,UINT32_MAX-255);
    for(unsigned pass=0;pass<2000;++pass){
        std::array<Pool::Reservation,99> rec;
        std::array<bool,198> seen{};
        for(auto& r:rec){
            r=pool.reserve(reserved);check(r.slot!=UINT32_MAX);
            for(int i=0;i<2;++i){
                auto id=pool.allocate(r.slot,allocated);
                check(id<seen.size()&&!seen[id]);seen[id]=true;
            }
        }
        check(!pool.available()&&pool.allocate(rec[0].slot,allocated)==UINT32_MAX);
        for(unsigned i=1;i<rec.size();++i)check(pool.close(rec[i].slot,closed));
        check(!pool.retire(rec.back().seq,retired));
        check(pool.close(rec[0].slot,closed)&&!pool.close(rec[0].slot,closed));
        check(!pool.retire(rec[0].seq-1,retired));
        for(auto r:rec)check(pool.retire(r.seq,retired));
        check(pool.empty()&&pool.available()==198);
    }
    Pool labels(1);
    std::array<Pool::Reservation,511> slots;
    for(auto& r:slots){r=labels.reserve(reserved);check(r.slot!=UINT32_MAX);}
    check(labels.reserve(reserved).slot==UINT32_MAX);
    for(auto r:slots){check(labels.close(r.slot,closed));check(labels.retire(r.seq,retired));}
    Pool workers(198);
    std::atomic<unsigned> issued{0};
    std::thread producer([&]{
        for(unsigned i=0;i<100000;++i){
            Pool::Reservation r;
            do{r=workers.reserve(reserved);if(r.slot==UINT32_MAX)std::this_thread::yield();}while(r.slot==UINT32_MAX);
            while(workers.allocate(r.slot,allocated)==UINT32_MAX)std::this_thread::yield();
            check(workers.close(r.slot,closed));
            issued.store(r.seq,std::memory_order_release);
        }
    });
    for(unsigned i=0;i<100000;++i){
        while(!workers.retire(issued.load(std::memory_order_acquire),retired))std::this_thread::yield();
    }
    producer.join();check(workers.empty()&&workers.available()==198);
    std::puts("396000 chunk ownership checks, 198000 slot lifetimes, 100000 concurrent retirements passed");
}
