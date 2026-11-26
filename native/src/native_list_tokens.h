#pragma once
#include <cstdint>
namespace d1 {
constexpr uint32_t kNativeListMagic = 0x4E44544Bu;
// Transitional retail submit ABI: decode the list token once, on the producer.
// Nothing in the native renderer or GPU queue consumes these packet words.
template<class Accept>
bool visit_native_list_tokens(const uint32_t* words, uint32_t count, Accept&& accept) {
    if (!words) return count == 0;
    for (uint32_t i = 0; i < count;) {
        const uint32_t h = words[i], type = h >> 30;
        const uint32_t n = type == 2 ? 1 : ((h >> 16) & 0x3FFF) + 2;
        if (n > count - i) return false;
        if (type == 3 && n == 4 && ((h >> 8) & 0xFF) == 0x10 && words[i + 1] == kNativeListMagic)
            accept(uint64_t(words[i + 2]) | (uint64_t(words[i + 3]) << 32));
        i += n;
    }
    return true;
}
} // namespace d1
