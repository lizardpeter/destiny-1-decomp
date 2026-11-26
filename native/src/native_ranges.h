#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace d1::native_ranges {
// Half-open address intervals. Zero-size notifications mean a single
// address (the original engine's unlock hook only supplies a pointer).
using Range = std::pair<uint64_t, uint64_t>;
inline uint64_t end_of(uint64_t base, uint64_t size) {
    return size > UINT64_MAX - base ? UINT64_MAX : base + size;
}
inline std::vector<Range> merged_writes(const std::vector<Range>& events) {
    std::vector<Range> ranges;
    ranges.reserve(events.size());
    for (auto [base, size] : events) {
        const uint64_t end = end_of(base, std::max<uint64_t>(1, size));
        if (end > base) ranges.emplace_back(base, end);
    }
    std::sort(ranges.begin(), ranges.end());
    size_t used = 0;
    for (const auto& range : ranges) {
        if (used && range.first <= ranges[used - 1].second)
            ranges[used - 1].second = std::max(ranges[used - 1].second, range.second);
        else ranges[used++] = range;
    }
    ranges.resize(used);
    return ranges;
}
inline bool overlaps(const std::vector<Range>& merged, uint64_t base, uint64_t size) {
    if (!size) return false;
    const uint64_t end = end_of(base, size);
    const auto it = std::upper_bound(merged.begin(), merged.end(), Range{base, UINT64_MAX});
    return (it != merged.end() && it->first < end) ||
           (it != merged.begin() && (it - 1)->second > base);
}
} // namespace d1::native_ranges
