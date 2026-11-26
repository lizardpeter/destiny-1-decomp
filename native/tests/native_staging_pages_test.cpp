#include "native_staging_pages.h"
#include <cstdlib>
#include <cstdio>
#include <random>
using namespace d1::ng;
static void check(bool b){if(!b)std::abort();}
int main(){
    check(!staging_span(4,0,0).valid);
    check(!staging_span(4,3,1).valid);
    check(staging_span(4,0,3).size==4);
    check(!staging_span(UINT64_MAX,UINT64_MAX,1).valid);
    check(!staging_page_size(UINT64_MAX,0));
    check(staging_page_size(1,0)==kGeometryStagingPage);
    check(staging_page_size(4,kGeometryStagingBudget-4)==4);
    check(!staging_page_size(5,kGeometryStagingBudget-4));
    std::mt19937 rng(0xD10133);
    unsigned pages=0,allocations=0;
    for(unsigned run=0;run<1000;++run){
        uint64_t committed=0,capacity=0,used=0;
        for(unsigned j=0;j<1000;++j){
            uint64_t n=1+rng()%65536;
            auto span=staging_span(capacity,used,n);
            if(!span.valid){
                capacity=staging_page_size(n,committed);
                if(!capacity)break;
                ++pages;committed+=capacity;used=0;
                span=staging_span(capacity,used,n);
            }
            check(span.valid&&span.offset>=used&&!(span.offset&3)&&!(span.size&3)&&span.size>=n);
            used=span.offset+span.size;
            check(used<=capacity&&committed<=kGeometryStagingBudget);
            ++allocations;
        }
    }
    check(allocations==1000000&&pages<allocations/20);
    std::printf("%u staging spans across %u pages passed\n",allocations,pages);
}
