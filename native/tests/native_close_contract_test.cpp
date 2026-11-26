#include "native_close_contract.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <random>
using namespace d1::ng;
static void check(bool v){if(!v)std::abort();}
int main(){
    for(unsigned flag=0;flag<256;++flag)for(unsigned kind=0;kind<256;++kind){
        auto p=close_plan(kind,uint8_t(flag),false);
        check(p.label_flag==(flag==0||kind==1)&&p.segment_kind==2);
        check(p.segment_hint==(kind==1?280u:24u)&&p.flip==(kind!=0)&&p.update_progress==(kind==1)&&p.append_ring);
        p=close_plan(kind,uint8_t(flag),true);
        check(p.label_flag==(flag^1)&&p.segment_kind==3&&p.segment_hint==24&&!p.flip&&!p.update_progress&&!p.append_ring);
    }
    std::mt19937 rng(0xD10133);
    for(unsigned i=0;i<100000;++i){
        uint32_t a=rng(),b=rng(); uint8_t enabled=uint8_t(rng());
        uint64_t delta=(uint64_t(a)+0x100000000ull-b)%0x100000000ull;
        check(worker_should_submit(a,b,enabled)==(enabled!=0&&delta<0x80000000ull));
        check(record_ring_slot(a)==a%64);
    }
    check(worker_should_submit(0,UINT32_MAX,1)&&!worker_should_submit(UINT32_MAX,0,1));
    std::puts("65536 close plans and 100000 native submission wrap cases passed");
}
