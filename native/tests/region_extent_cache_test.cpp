#include "region_extent_cache.h"
#include <map>
#include <random>
#include <cstdio>
#include <cstdlib>

struct Region { uint64_t size; };
int main() {
    std::map<uint64_t, Region> regions;
    std::mutex mutex;
    std::atomic<uint64_t> generation{1};
    d1::RegionExtentCache cache;
    std::mt19937_64 rng(0xD1);
    uint64_t checks = 0;
    auto check = [&](uint64_t addr) {
        uint64_t expected = 0;
        auto it = regions.upper_bound(addr);
        if (it != regions.begin()) {
            --it;
            const uint64_t end = it->first + it->second.size;
            if (addr < end) expected = end - addr;
        }
        const uint64_t actual = cache.lookup(addr, generation, mutex, regions);
        if (actual != expected) {
            std::fprintf(stderr, "extent mismatch at %llu: %llu != %llu\n",
                (unsigned long long)addr, (unsigned long long)actual, (unsigned long long)expected);
            std::exit(1);
        }
        ++checks;
    };
    auto change = [&](uint64_t base, uint64_t size) {
        std::lock_guard lk(mutex);
        if (size) regions[base] = {size};
        else regions.erase(base);
        generation.fetch_add(1, std::memory_order_release);
    };
    change(100, 100); change(200, 50); change(150, 10);
    for (uint64_t a = 0; a < 300; ++a) check(a);
    change(150, 0);
    for (uint64_t a = 0; a < 300; ++a) check(a);
    change(100, 7); change(200, 0);
    for (uint64_t a = 0; a < 300; ++a) check(a);
    for (unsigned round = 0; round < 4000; ++round) {
        const uint64_t base = 16 * (rng() % 256);
        change(base, rng() % 3 ? 1 + rng() % 192 : 0);
        for (unsigned k = 0; k < 256; ++k) check(rng() % 4352);
        for (unsigned k = 0; k < 16; ++k) check(base + k);
    }
    std::printf("%llu extent checks passed\n", (unsigned long long)checks);
}
