#include "native_blit_queue.h"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <random>
using namespace d1::ng;
static void check(bool b){if(!b)std::abort();}
int main(){
    // Native host storage has the original slot/cursor layout and a separate
    // aligned label, initialized to the predecessor of the next batch.
    std::array<uint8_t,blit::storage_bytes> storage;
    for(uint32_t seq:{0u,1u,0xffffffffu,0x80000000u}){
        storage.fill(0xff);
        blit::initialize_storage(storage.data(),seq);
        for(size_t i=0;i<blit::label_offset;++i)check(!storage[i]);
        check(binding::load<uint32_t>(storage.data(),blit::label_offset)==seq-1);
    }
    for(uint32_t h=0;h<blit::capacity;++h)for(uint32_t t=0;t<blit::capacity;++t){
        const auto cursor=blit::pack(h,t);
        const auto count=(t+blit::capacity-h)%blit::capacity;
        check(blit::used(cursor)==count);
        check(blit::contains(cursor,h)==(count!=0));
        check(!blit::contains(cursor,t));
        if(count)check(blit::contains(cursor,(t+blit::capacity-1)%blit::capacity));
        uint64_t updated=0;uint32_t slot=0;
        const bool ok=blit::reserve(cursor,updated,slot);
        check(ok==(count<blit::capacity-1));
        if(ok)check(slot==t&&blit::head(updated)==h&&blit::used(updated)==count+1);
        check(blit::can_request(cursor,blit::capacity-count));
        check(!blit::can_request(cursor,blit::capacity-count+1));
    }
    std::mt19937_64 rng(0xd10133);
    for(unsigned i=0;i<100000;++i){
        std::array<uint8_t,64> obj{},expected{};
        for(auto& x:obj)x=uint8_t(rng());
        expected=obj;
        blit::Request r{rng(),rng(),uint32_t(rng()),rng(),rng(),rng()};
        binding::store(expected.data(),8,r.destination);binding::store(expected.data(),16,r.source);
        binding::store(expected.data(),24,r.bytes);binding::store(expected.data(),32,r.callback);
        binding::store(expected.data(),40,r.arg0);binding::store(expected.data(),48,r.arg1);
        expected[60]=0;
        blit::initialize(obj.data(),r);
        check(obj==expected);
        const uint32_t done=rng(),seq=rng();
        check(blit::completed(done,seq)==((uint64_t(done)+0x100000000ull-seq)%0x100000000ull<0x80000000ull));
    }
    check(!blit::valid(blit::pack(blit::capacity,0)));
    std::puts("6553600 blit cursor pairs and 100000 record layouts passed");
}
