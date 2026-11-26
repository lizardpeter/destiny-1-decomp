#include "native_texture_identity.h"
#include "native_texture_leases.h"
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
    // Distinct swizzles and minimum LOD still reference the same bytes.
    uint32_t view[8];for(unsigned i=0;i<8;++i)view[i]=high[i];
    view[3]^=0xfffu;view[1]^=0xabc00u;
    check(texture_identity_words(high)==texture_identity_words(view));
    check(texture_source_layout_equal(high,view));
    view[1]^=1u<<20;
    check(texture_identity_words(high)!=texture_identity_words(view));
    check(!texture_source_layout_equal(high,view));
    // Array and volume resources also use view-level component selection.
    for(uint32_t type:{10u,12u,13u}){
        high[3]=(high[3]&0x0fffffffu)|(type<<28);
        for(unsigned i=0;i<8;++i)view[i]=high[i];
        view[3]^=0xfffu;
        check(texture_identity_words(high)==texture_identity_words(view));
        check(texture_source_layout_equal(high,view));
    }
    TextureLeaseRegistry registry;
    {
        TextureLeaseSet a(registry);
        {auto guard=registry.capture();for(unsigned i=0;i<1000;++i)a.pin(7);a.pin(8);}
        {
            TextureLeaseSet b(registry);
            {auto guard=registry.capture();b.pin(7);b.pin(9);}
            auto guard=registry.retire();
            check(registry.pinned(7)&&registry.pinned(8)&&registry.pinned(9));
        }
        check(registry.pinned(7)&&registry.pinned(8)&&!registry.pinned(9));
    }
    check(!registry.pinned(7)&&!registry.pinned(8));
    std::puts("split descriptor identity and independent source layout checks passed");
}
