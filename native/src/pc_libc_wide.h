#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d1::pc_libc {
// Retail wchar_t is 16 bits (confirmed in libc.prx); host wchar_t need not be.
// Use byte loads through memcpy so even unaligned guest addresses are supported.
inline size_t wide_length16(const void* str) {
    const auto* bytes = static_cast<const unsigned char*>(str);
    size_t n = 0;
    uint16_t unit;
    do {
        std::memcpy(&unit, bytes, sizeof(unit));
        if (!unit) return n;
        bytes += sizeof(unit);
        ++n;
    } while (true);
}
} // namespace d1::pc_libc
