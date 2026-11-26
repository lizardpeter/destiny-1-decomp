#include "native_resource_move.h"
#include "native_pool_contract.h"
#include <vector>
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace d1::ng::move;
static void check(bool x){if(!x)std::abort();}
int main(){
    uint64_t cases=0;
    for(uint32_t p=0;p<0x80000;++p){
        const uint32_t h=(p<<13)|0x123;
        const auto signed_part=int32_t(h)>>13;
        const uint32_t expected=(uint32_t(signed_part)>>18|0x3ff)&uint32_t(signed_part);
        check(pool_index(h)==expected);
        check(datum_offset(h,0x140)==0x123*0x140);
        ++cases;
    }
    check(datum_offset(0x1fff,0xffffffff)==uint32_t(0u-0x1fff));
    check(datum_address(0x500000000ull,0x100,0xff,0x1234)==0x5000000ccull);
    check(datum_address(0x500000000ull,0x100,-1,0x34)==0x5000000ccull);
    check(relocation_flags(0x100000001ull)==0xc0000001u);
    check(container_index(0xabcdef12)==0xef12&&container_generation(0xabcdef12)==0xcd);
    for(uint32_t f=0;f<65536;++f){
        const uint32_t id=0x12345678,other=0x23456789;
        check(secondary_id(uint16_t(f),id,other)==((f&4)?(other|0xc0000000u):((f&0x21)?id:UINT32_MAX)));
        if(f&4)check(secondary_id(uint16_t(f),UINT32_MAX,other)==UINT32_MAX);
        check(unpin(uint8_t(f))==uint8_t(f&0xfe));
    }
    check(owner_handle(2,10,20)==10&&owner_handle(1,10,20)==20);
    auto p=copy(0x1000,0x2000,0x80,0x100000040ull);
    check(p.src==0x1000&&p.dst==0x2080&&p.bytes==0x40);
    namespace pool=d1::ng::pool;
    // Synthetic pool table with a three-node list; exercise head, middle,
    // tail, singleton and unoccupied release, plus callback ordering.
    for(unsigned mode=0;mode<5;++mode){
        std::vector<uint8_t> root(0x40300),bitmap(2048);
        std::array<uint8_t,0x50> datum{};
        const uint64_t base=uint64_t(root.data()),bits=uint64_t(bitmap.data()),d=uint64_t(datum.data());
        pool::write(base,0x40010,bits);pool::write(base,0x402a0,base);
        pool::write(base,0x402b0,uint32_t(0x40));
        const unsigned i=2,prev=mode==0||mode==3?0xffff:1,next=mode==2||mode==3?0xffff:3;
        const auto node=base+i*0x40;
        pool::write(node,0x18,d);pool::write(node,0x38,uint16_t(19));
        pool::write(node,0x3a,uint16_t(7));pool::write(node,0x3c,uint16_t(next));pool::write(node,0x3e,uint16_t(prev));
        pool::write(d,0,uint64_t(123));pool::write(d,8,uint64_t(456));pool::write(d,0x34,uint32_t(42));pool::write(d,0x40,uint8_t(1));
        pool::write(bits,0,mode==4?0u:(1u<<i)|(1u<<1)|(1u<<3));
        pool::write(base,0x402c2,uint16_t(i));
        unsigned phase=0;
        check(pool::lookup(base,i)==(mode==4?0:d));
        bool done=pool::destroy(i|0x4000,[&]{return base;},
            [&](uint32_t id,uint16_t kind){check(phase++==0&&id==i&&kind==7);},
            [&](uint64_t p){check(phase++==1&&p==123);},
            [&](uint16_t tag){check(phase++==2&&tag==19&&pool::read<uint64_t>(d,8)==0);});
        check(done==(mode!=4));
        if(mode==4){check(!phase);continue;}
        check(phase==3&&!pool::occupied(base,i)&&pool::read<uint32_t>(d,0x34)==UINT32_MAX);
        for(unsigned j=0;j<0x40;++j)check(root[i*0x40+j]==0);
        if(next!=0xffff)check(pool::read<uint16_t>(base+next*0x40,0x3e)==prev);
        if(prev!=0xffff)check(pool::read<uint16_t>(base+prev*0x40,0x3c)==next);
        else check(pool::read<uint16_t>(base,0x402c2)==next);
    }
    std::array<uint8_t,0x40> table{},entry{},datum{};
    const auto tb=uint64_t(table.data()),en=uint64_t(entry.data()),da=uint64_t(datum.data());
    pool::write(tb,0,en);pool::write(tb,0x10,uint32_t(0x40));
    pool::write(en,0x18,tb+0x20);pool::write(tb+0x20,8,da);pool::write(tb+0x20,0x10,uint32_t(16));
    pool::set_datum(tb,1,da+0x1000,0xc0000040);
    check(pool::read<uint32_t>(da+16)==0xc0000040&&pool::read<uint64_t>(da+16,8)==uint64_t(16-0x1000));
    // Active list uses relative link offsets; verify normal head/tail and
    // special recycling, including the release-failed no-mutation path.
    for(uint32_t kind:{1u,3u,4u,5u,10u}){
        std::array<uint8_t,0x300> tab{},regions{};
        std::array<uint8_t,8> special{};
        const auto tb=uint64_t(tab.data()),rg=uint64_t(regions.data()),sp=uint64_t(special.data());
        const auto links=tb+0x80,entries=tb+0x100;
        pool::write(tb,0,uint64_t(0x100));pool::write(tb,8,uint64_t(0x78));pool::write(tb,0x10,uint64_t(0x80));
        const uint16_t id=1;
        const auto item=entries+uint64_t(id)*0x80;
        pool::write(item,0x30,uint64_t(0x4000));pool::write(item,0x50,uint64_t(0x100));
        pool::write(item,0x70,uint32_t(5));pool::write(item,0x74,kind);
        const bool top=((0x270u>>(kind-1))&1)!=0;
        const auto active=rg+0x70+(top?0x64:0x5c);
        pool::write(active,0,id);pool::write(active,2,id);pool::write(active,4,uint32_t(1));
        pool::write(links+id*4,0,uint16_t(0xffff));pool::write(links+id*4,2,uint16_t(0xffff));
        pool::write(tb,0x24,uint16_t(2));pool::write(tb,0x26,uint16_t(2));pool::write(tb,0x28,uint32_t(1));
        pool::write(sp,0,uint16_t(2));pool::write(sp,2,uint16_t(2));pool::write(sp,4,uint32_t(1));
        pool::write(links+8,0,uint16_t(0xffff));pool::write(links+8,2,uint16_t(0xffff));
        const auto before=tab;
        check(!pool::release_container(tb,rg,sp,id,[](uint32_t){return 1;},
            [](uint64_t,uint64_t,uint32_t,uint8_t,int32_t){return false;}));
        check(tab==before&&pool::read<uint32_t>(active,4)==1);
        check(pool::release_container(tb,rg,sp,id,[](uint32_t){return 1;},
            [&](uint64_t a,uint64_t n,uint32_t tag,uint8_t side,int32_t region){
                check(a==0x4000&&n==0x100&&tag==5&&side==top&&region==1);return true;
            }));
        check(pool::read<uint16_t>(active)==0xffff&&pool::read<uint32_t>(active,4)==0);
        const bool separate=kind==3||kind==4||kind==5;
        const auto free_list=separate?sp:tb+0x24;
        check(pool::read<uint32_t>(free_list,4)==2);
        if(kind==10)check(pool::read<uint16_t>(free_list,2)==id&&pool::read<uint16_t>(links+8)==id);
        else check(pool::read<uint16_t>(free_list)==id&&pool::read<uint16_t>(links+8,2)==id);
    }
    std::printf("native resource move: %llu handle cases passed\n",(unsigned long long)cases);
}
