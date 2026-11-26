#include "native_epoch_cohort.h"
#include <cstdlib>
#include <cstdio>
using d1::ng::EpochCohort;
static void check(bool b){if(!b)std::abort();}
static uint64_t res(uint32_t id,uint32_t epoch){return uint64_t(id)<<32|epoch;}
int main(){
    EpochCohort cohort;
    check(cohort.observe(0));
    for(uint32_t i=1;i<=10000;++i)check(cohort.observe(res(i,1)));
    for(uint32_t i=1;i<=10000;++i){
        check(cohort.observe(res(i,1)));
        check(!cohort.observe(res(i,2)));
        check(cohort.observe(res(i,1)));
    }
    cohort.clear();
    check(cohort.observe(res(1,2)));
    check(!cohort.observe(res(1,1)));
    check(cohort.observe(res(257,8)));
    check(cohort.observe(res(1,2)));
    std::puts("40000 ordered texture epoch and direct-cache collision checks passed");
}
