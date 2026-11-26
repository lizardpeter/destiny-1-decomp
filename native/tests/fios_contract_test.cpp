#include "fios_contract.h"
#include <cstdio>
#include <cstdlib>
using namespace d1::fios_contract;
int main() {
    constexpr uint32_t expected[16] = {0,10,26,10,1,11,27,11,2,10,26,10,3,11,27,11};
    for (uint32_t flags = 0; flags < 65536; ++flags)
        if (translate_open_flags(flags) != expected[flags & 15]) std::abort();
    std::puts("65536 retail open flag translation cases passed");
}
