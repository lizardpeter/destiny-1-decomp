#include "frame_time_contract.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::frame_time;
static void require(bool v) {
    if (!v) { std::fputs("frame time contract regression failed\n", stderr); std::exit(1); }
}
int main() {
    require(split_primary(0, 0, 33366).first_us == 0);
    require(split_primary(8333, 1, 33366).first_us == 8333);
    require(split_primary(66732, 2, 33366).first_us == 33366);
    require(split_primary(67000, 3, 33366).first_us == 268);
    // Preserve an inconsistent schedule for diagnostics; never silently clamp.
    require(split_primary(16000, 2, 33366).first_us == -17366);
    std::mt19937_64 rng(0x591d40);
    for (unsigned i = 0; i < 100000; ++i) {
        const int64_t period = 1 + rng() % 100000;
        const int32_t count = rng() % 11;
        const int64_t total = rng() % 1000000;
        const auto split = split_primary(total, count, period);
        require(split.remaining == (count > 0 ? count - 1 : 0));
        int64_t scheduled = split.first_us;
        for (int32_t j = 0; j < split.remaining; ++j) scheduled += period;
        require(scheduled == total);
    }
    std::puts("100005 frame timing contract cases passed");
}
