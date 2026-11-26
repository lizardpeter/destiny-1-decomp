#include "pc_libc_wide.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
using d1::pc_libc::wide_length16;
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    std::mt19937 rng(0xD10133);
    std::array<unsigned char, 2050> bytes{};
    for (unsigned i=0; i<100000; ++i) {
        size_t n = rng()%1024;
        auto* p = bytes.data() + (i&1);
        for (size_t j=0;j<n;++j) {
            uint16_t unit=uint16_t(rng()%65535+1);
            std::memcpy(p+2*j,&unit,2);
        }
        p[2*n]=p[2*n+1]=0;
        check(wide_length16(p)==n);
    }
    const uint16_t surrogate_pair[]={0xd83d,0xde00,0};
    check(wide_length16(surrogate_pair)==2);
    std::puts("100000 retail 16-bit wide-string cases passed");
}
