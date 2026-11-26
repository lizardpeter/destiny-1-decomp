#include "native_mapping_contract.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1;
static void check(bool b){if(!b)std::abort();}
int main(){
    check(mapping_contains(100,20,100,20));
    check(mapping_contains(100,20,119,1));
    check(!mapping_contains(100,20,120,1)&&!mapping_contains(100,20,99,1));
    check(!mapping_contains(UINT64_MAX-5,6,UINT64_MAX-1,1));
    check(!mapping_contains(100,20,100,0));
    check(mapping_allows(100,20,3,105,5,3,3,1));
    check(!mapping_allows(100,20,4,105,5,3,3,1));
    check(!mapping_allows(100,20,0,105,5,0,3,1));
    check(!mapping_allows(100,20,3,105,5,3,1,2));
    std::mt19937_64 rng(0xD10133);
    for(unsigned i=0;i<200000;++i){
        const uint64_t base=rng(),extent=rng()%8192;
        const uint64_t offset=rng()%16384,addr=base+offset,size=rng()%16384;
        const bool expected=extent<=UINT64_MAX-base&&addr>=base&&size!=0&&offset<extent&&size<=extent-offset;
        check(mapping_contains(base,extent,addr,size)==expected);
        const uint64_t stamp=rng()%4,wanted=rng()%4;
        const unsigned prot=rng()%8,required=rng()%8;
        check(mapping_allows(base,extent,stamp,addr,size,wanted,prot,required)==
              (expected&&wanted!=0&&stamp==wanted&&(prot&required)==required));
    }
    MappingSpans spans;
    uint64_t second_identity=12;
    bool second_writable=true;
    auto lookup=[&](uint64_t a,MappingSpan& r){
        if(a>=100&&a<120){r={100,20,11};return true;}
        if(a>=120&&a<150&&second_writable){r={120,30,second_identity};return true;}
        return false;
    };
    check(capture_mapping_spans(110,30,spans,lookup));
    check(spans.size()==2&&spans[0].size==10&&spans[1].size==20);
    check(validate_mapping_spans(110,30,spans,lookup));
    check(validate_mapping_spans(110,5,spans,lookup));
    check(!validate_mapping_spans(110,31,spans,lookup));
    second_identity++;
    check(!validate_mapping_spans(110,30,spans,lookup));
    check(validate_mapping_spans(110,10,spans,lookup));
    second_writable=false;
    check(!capture_mapping_spans(110,30,spans,lookup)&&spans.empty());
    check(!capture_mapping_spans(UINT64_MAX-2,4,spans,lookup));
    std::puts("200000 mapping lifetime, extent and protection cases plus multi-region cases passed");
}
