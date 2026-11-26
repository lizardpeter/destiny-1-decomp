#include "native_cbm_contract.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::ng::cbm;
static void check(bool b){if(!b)std::abort();}
int main(){
    check(!layout(0,4096).bytes&&!layout(1,4096).bytes&&!layout(chunk_bytes,3).bytes);
    check(!layout(chunk_bytes,0).bytes);
    for(uint64_t gran:{4096ull,16384ull,65536ull}){
        for(uint64_t n=1;n<=512;++n){
            auto l=layout(n*chunk_bytes,gran);
            check(l.data==n*chunk_bytes&&l.label==l.data&&l.bytes==l.data+gran);
        }
    }
    std::mt19937 rng(0xd10133);
    for(unsigned i=0;i<200000;++i){
        uint32_t seq=rng(),sample=rng(),index=rng()%slot_count;
        check(closed_value(open_value(seq))==seq);
        check(next(index)==(index+1)%slot_count);
        check(completed(sample,seq)==((uint64_t(sample)+0x100000000ull-seq)%0x100000000ull<0x80000000ull));
        check(!completed(seq,open_value(seq)));
    }
    std::puts("200000 CBM sequence cases and 1536 backing layouts passed");
}
