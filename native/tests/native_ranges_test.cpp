#include "native_ranges.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace d1::native_ranges;
static void require(bool v) {
    if (!v) { std::fputs("native range regression failed\n", stderr); std::exit(1); }
}
int main() {
    require(overlaps(merged_writes({{100, 0}}), 90, 11));
    require(!overlaps(merged_writes({{100, 0}}), 90, 10));
    require(!overlaps(merged_writes({{100, 10}}), 110, 1));
    require(overlaps(merged_writes({{UINT64_MAX - 8, 32}}), UINT64_MAX - 4, 2));
    std::mt19937_64 rng(0xD1);
    for (int batch = 0; batch < 10000; ++batch) {
        std::vector<Range> writes;
        for (unsigned n = rng() % 64; n; --n)
            writes.emplace_back(rng() % 4096, rng() % 128);
        const auto merged = merged_writes(writes);
        for (size_t i = 1; i < merged.size(); ++i)
            require(merged[i - 1].second < merged[i].first);
        for (int query = 0; query < 100; ++query) {
            const uint64_t a = rng() % 4096, size = rng() % 512;
            bool expected = false;
            for (auto [b, n] : writes)
                expected |= size != 0 && a < b + std::max<uint64_t>(n, 1) && b < a + size;
            require(overlaps(merged, a, size) == expected);
        }
    }
    std::puts("1000004 native range comparisons passed");
}
