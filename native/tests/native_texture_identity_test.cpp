#include "native_texture_identity.h"
#include <cstdlib>
#include <cstdio>
using namespace d1::ng;
static void check(bool x){if(!x)std::abort();}
int main(){
    uint32_t high[8]={0x200,0x12300000,127|(63u<<14),(9u<<28)|(14u<<20)|(1u<<25)|(7u<<16)|0x688,127u<<13,0,0,0};
    uint32_t low[8]={0x100,0x12300000,63|(31u<<14),(9u<<28)|(9u<<20)|(6u<<16)|0x688,63u<<13,0,0,0};
    check(texture_identity_words(high)==texture_identity_words(low));
    check(!texture_source_layout_equal(high,low));
    low[3]=(low[3]&0x0fffffff)|(8u<<28);
    check(texture_identity_words(high)==texture_identity_words(low));
    low[1]^=1u<<20;check(texture_identity_words(high)!=texture_identity_words(low));
    low[1]^=1u<<20;
    uint32_t moved[8];for(unsigned i=0;i<8;++i)moved[i]=high[i];
    moved[0]=0xffff;moved[1]^=0x12300;moved[3]^=0x1000;
    check(texture_source_layout_equal(high,moved));
    moved[3]^=1u<<20;check(!texture_source_layout_equal(high,moved));
    std::puts("split descriptor identity and independent source layout checks passed");
}
