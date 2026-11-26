#include "native_epoch_cohort.h"
#include "native_draw_batch.h"
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
    using namespace d1::ng;
    SegmentTable<uint32_t,uint32_t> lazy;
    unsigned constructed=0;
    for(unsigned i=0;i<1000;++i){const auto [value,added]=lazy.get_or_insert(17,[&]{++constructed;return 123u;});check(value==123&&added==(i==0));}
    check(constructed==1);lazy.clear();check(lazy.get_or_insert(17,[&]{++constructed;return 456u;}).first==456&&constructed==2);
    struct CollisionHash {size_t operator()(uint32_t)const{return 0;}};
    SegmentTable<uint32_t,uint32_t,CollisionHash> flat;
    for(unsigned pass=0;pass<8;++pass){
        flat.clear();
        for(uint32_t i=0;i<512;++i)check(flat.insert(i,i+pass).second);
        for(uint32_t i=0;i<512;++i){const auto [value,added]=flat.insert(i,9999);check(!added&&value==i+pass);}
    }
    Draw a{},b{};
    a.kind=b.kind=Draw::Indexed;
    a.ires=b.ires={res(1,2),0x10000,256};
    a.index_addr=0x10000;b.index_addr=0x10020;
    a.count=12;b.count=24;
    uint32_t first=0;
    check(same_native_draw_state(a,b)&&native_index_range(b,first)&&first==16);
    b.index_addr=0x100ff;check(!native_index_range(b,first));b.index_addr=0x10020;
    b.cb[0][1].addr=42;check(!same_native_draw_state(a,b));b.cb[0][1].addr=0;
    b.stencil_ref=1;check(!same_native_draw_state(a,b));b.stencil_ref=0;
    b.vres[1].id=99;check(!same_native_draw_state(a,b));b.vres[1].id=0;
    a.active_tex_mask[1]=b.active_tex_mask[1]=1;
    check(!same_native_draw_state(a,b));
    a.native_tex_ready[1]=b.native_tex_ready[1]=1;
    a.tex_res[1][0]=b.tex_res[1][0]=res(2,1);
    check(same_native_draw_state(a,b));
    b.tex_res[1][0]=res(2,2);check(!same_native_draw_state(a,b));
    std::puts("40000 ordered texture epoch and direct-cache collision checks passed");
}
