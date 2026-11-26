#pragma once
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>

namespace d1 {
// Mapping metadata only: callers still need allocation lifetime ownership.
// Map entries expose .second.size; mutations publish generation under mutex.
class RegionExtentCache {
    struct Span { uint64_t begin = 0, limit = 0, end = 0; };
    uint64_t gen_ = 0;
    Span spans_[8]{};
    unsigned next_ = 0;
public:
    template<class Map>
    uint64_t lookup(uint64_t addr, const std::atomic<uint64_t>& generation,
                    std::mutex& mutex, const Map& regions) {
        const uint64_t gen = generation.load(std::memory_order_acquire);
        if (gen_ == gen) {
            for (const Span& span : spans_)
                if (addr >= span.begin && addr < span.limit) return span.end - addr;
        }
        std::lock_guard lk(mutex);
        const uint64_t current = generation.load(std::memory_order_relaxed);
        if (gen_ != current) {
            *this = {};
            gen_ = current;
        }
        auto next = regions.upper_bound(addr);
        if (next == regions.begin()) return 0;
        const auto it = std::prev(next);
        const uint64_t end = it->first + it->second.size;
        if (addr >= end) return 0;
        const uint64_t limit = next != regions.end() && next->first < end ? next->first : end;
        spans_[next_++ & 7] = {it->first, limit, end};
        return end - addr;
    }
};
} // namespace d1
