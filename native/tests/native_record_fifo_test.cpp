#include "native_record_fifo.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <thread>
using d1::ng::NativeRecordFifo;
static void check(bool b){if(!b)std::abort();}
int main(){
    auto m=[](uint32_t){}; auto p=[]{};
    NativeRecordFifo q(UINT32_MAX-31);
    for(unsigned pass=0;pass<2048;++pass){
        for(uint64_t i=1;i<=64;++i)check(q.reserve(i,false,m));
        check(!q.reserve(65,true,m)&&!q.reserve(1,true,m));
        for(uint64_t i=64;i>1;--i)check(q.publish(i,p));
        check(!q.take_ready(m)&&q.publish(1,p)&&!q.publish(1,p));
        for(uint64_t i=1;i<=64;++i)check(q.take_ready(m)==i);
        check(!q.take_ready(m));
    }
    NativeRecordFifo concurrent;
    std::atomic<uint64_t> reserved{0};
    std::thread producer([&]{
        for(uint64_t i=1;i<=100000;++i){
            while(!concurrent.reserve(i,false,m))std::this_thread::yield();
            reserved.store(i,std::memory_order_release);
        }
    });
    std::thread worker([&]{
        for(uint64_t i=1;i<=100000;++i){
            while(reserved.load(std::memory_order_acquire)<i)std::this_thread::yield();
            check(concurrent.publish(i,p));
        }
    });
    for(uint64_t i=1;i<=100000;++i){
        uint64_t record=0;
        while(!(record=concurrent.take_ready(m)))std::this_thread::yield();
        check(record==i);
    }
    producer.join();worker.join();
    std::puts("131072 wrapped reservations and 100000 concurrent ordered records passed");
}
