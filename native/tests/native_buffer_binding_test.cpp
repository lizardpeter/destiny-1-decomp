#include "native_buffer_binding.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::ng;
static void check(bool b){if(!b)std::abort();}
int main(){
    std::mt19937_64 rng(0xD10133);
    for(uint32_t f=0;f<65536;++f){
        const uint64_t addr=rng();
        const uint32_t stride=uint32_t(rng()),count=uint32_t(rng());
        uint32_t w[4]{};
        vertex_words(w,addr,f,stride,count);
        check(w[0]==uint32_t(addr)&&w[2]==count);
        check((w[1]&4095)==((addr>>32)&4095));
        check((w[1]>>16)==(stride%16384));
        const uint32_t expected=(f/4096)%4096+((f/256)%8)*4096+0x20000000u+
            (((f%256)>0&&(f%256)<15&&((f/2048)%2)==0)?(f%16)*32768:0);
        check(w[3]==expected);
        for(uint32_t t=0;t<128;++t){
            uint32_t a[4]{w[0],w[1],w[2],w[3]};
            buffer_memory_type(a,t);
            check(a[0]==w[0]&&a[2]==w[2]);
            check((a[1]&0xfff)==(w[1]&0xfff));
            check((a[1]&0xffff0fff)==(w[1]&0xffff0fff));
            check((a[1]>>14&3)==(t&3));
            check((a[1]>>12&3)==(t>>5&3));
            check((a[3]&0xc7ffffff)==(w[3]&0xc7ffffff));
            check((a[3]>>27&7)==(t>>2&7));
        }
    }
    for(unsigned i=0;i<100000;++i){
        const uint32_t full=uint32_t(rng()),offset=uint32_t(rng()),stride=1+uint32_t(rng()%16383);
        const int32_t explicit_count=int32_t(rng());
        check(vertex_records(full,0,stride,explicit_count)==full);
        check(vertex_records(full,offset,stride,0)==(offset?full-offset/stride:full));
        check(vertex_records(full,offset,stride,explicit_count)==(!offset?full:explicit_count?uint32_t(explicit_count):full-offset/stride));
        check(!vertex_bind_changed(7,8,7,8)&&vertex_bind_changed(7,8,7,9));
        check(!vertex_bind_changed(7,8,UINT32_MAX,9));
        uint32_t w[4];const auto addr=rng();const uint32_t bytes=uint32_t(rng());
        constant_words(w,addr,bytes);
        check(w[0]==uint32_t(addr)&&w[1]==(0x100000u|((addr>>32)&4095)));
        check(w[2]==uint32_t(bytes+15u)/16&&w[3]==0x20077facu);
    }
    std::puts("65536 vertex layouts, 8388608 memory-type combinations and 100000 constant/binding cases passed");
}
