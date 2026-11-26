#include "pc_mspace.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>
using d1::pc_libc::Mspace;
static void check(bool ok) { if (!ok) std::abort(); }
struct Allocation { void* p; size_t size; unsigned char tag; };
int main() {
    constexpr size_t capacity = 1 << 20;
    alignas(4096) static std::array<std::byte, capacity> backing{};
    Mspace heap(backing.data(), backing.size());
    check(!heap.allocate(SIZE_MAX));
    check(!heap.calloc(SIZE_MAX, 2));
    check(!heap.allocate(1, 3));
    check(!heap.release(backing.data() + 1));
    auto* a = static_cast<unsigned char*>(heap.allocate(33, 256));
    check(a && uintptr_t(a) % 256 == 0 && heap.usable_size(a) >= 33);
    std::memset(a, 0xA7, 33);
    auto* b = static_cast<unsigned char*>(heap.reallocate(a, 128, 256));
    check(b == a && std::all_of(b, b + 33, [](auto x){return x == 0xA7;}));
    check(heap.reallocate(b, 16) == b && heap.usable_size(b) == 16);
    check(!heap.reallocate(b, capacity + 1) && b[0] == 0xA7);
    check(heap.release(b) && !heap.release(b));
    auto* zero = static_cast<unsigned char*>(heap.calloc(32, 17));
    check(zero && std::all_of(zero, zero + 544, [](auto x){return x == 0;}));
    check(heap.release(zero));
    auto full = heap.allocate(capacity);
    check(full && !heap.allocate(1) && heap.release(full));
    auto worker = [&](unsigned seed) {
        std::mt19937 rng(seed);
        std::vector<Allocation> live;
        for (unsigned i = 0; i < 25000; ++i) {
            if (live.empty() || (live.size() < 64 && rng() % 3 == 0)) {
                size_t n = rng() % 2048 + 1, align = size_t(16) << (rng() % 7);
                if (auto p = heap.allocate(n, align)) {
                    check(uintptr_t(p) % align == 0 && heap.usable_size(p) >= n);
                    const auto tag = static_cast<unsigned char>(rng());
                    std::memset(p, tag, n); live.push_back({p,n,tag});
                }
            } else {
                const size_t j = rng() % live.size(); auto& x = live[j];
                auto* bytes = static_cast<unsigned char*>(x.p);
                check(std::all_of(bytes, bytes+x.size, [&](auto v){return v==x.tag;}));
                if (rng() % 2) {
                    size_t n = rng() % 4096 + 1, align = size_t(16) << (rng() % 7);
                    if (auto p = heap.reallocate(x.p, n, align)) {
                        auto* q = static_cast<unsigned char*>(p);
                        check(uintptr_t(q)%align==0 && std::all_of(q,q+std::min(n,x.size),[&](auto v){return v==x.tag;}));
                        std::memset(q,x.tag,n); x={q,n,x.tag};
                    }
                } else { check(heap.release(x.p)); x=live.back(); live.pop_back(); }
            }
            auto s = heap.stats(); check(s.allocated+s.free==capacity && s.largest_free<=s.free);
        }
        for (auto x : live) check(heap.release(x.p));
    };
    std::vector<std::thread> workers;
    for (unsigned i=0;i<4;++i) workers.emplace_back(worker, 0xD10133+i);
    for (auto& t:workers) t.join();
    const auto s=heap.stats(); check(s.live==0 && s.allocated==0 && s.largest_free==capacity);
    std::puts("100000 concurrent mspace model operations and boundary checks passed");
}
