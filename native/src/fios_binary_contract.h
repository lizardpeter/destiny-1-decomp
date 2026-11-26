#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace d1::fios_contract {
// Exact scalar transformations from the retail libSceFios2.prx (01.33).
// 0x2AD70: low access bits and four independent flag bits.
constexpr uint32_t to_kernel_open_flags(uint32_t flags) {
    const uint32_t access = (flags & 3) == 2 ? 1 : (flags & 3) == 3 ? 2 : 0;
    return access | ((flags & 4) << 1) | ((flags & 8) << 6) |
           ((flags & 16) << 6) | ((flags & 0x1000) << 4);
}
// 0x2AB90: already-encoded kernel status input (not raw -1/errno).
// Unrecognized negative encoded statuses pass through unchanged.
constexpr uint32_t from_encoded_kernel_status(uint32_t status) {
    switch (status) {
    case 0x80020001: case 0x8002000d: case 0x80020042: return 0x80820013;
    case 0x80020002: case 0x80020016: case 0x8002002d: case 0x8002003f:
    case 0x80020059: return 0x80820005;
    case 0x80020009: return 0x8082000b;
    case 0x80020010: return 0x8082001e;
    case 0x80020011: return 0x80820023;
    case 0x80020013: case 0x8002004e: return 0x80820017;
    case 0x80020014: return 0x8082000f;
    case 0x80020015: return 0x8082000e;
    case 0x8002001b: return 0x80820008;
    case 0x8002001e: return 0x80820015;
    case 0x80020022: return 0x80820010;
    case 0x8002003c: return 0x80820011;
    default: return status;
    }
}
// Shared attribute-bit calculation in StatSync (0x9DE0) and directory
// worker (0x18BC0): directory, owner-readable, owner-writable.
constexpr uint32_t attribute_bits(uint32_t mode) {
    return ((mode & 0xf000) == 0x4000 ? 1u : 0u) |
           ((mode & 0400) ? 2u : 0u) | ((mode & 0200) ? 4u : 0u);
}
struct Stat {
    uint64_t size;
    uint64_t access_ns;
    uint64_t modification_ns;
    uint64_t unknown18;
    uint32_t attributes;
    uint8_t unknown24[36];
    uint64_t mode;
};
static_assert(sizeof(Stat) == 0x50);
static_assert(offsetof(Stat, attributes) == 0x20);
static_assert(offsetof(Stat, mode) == 0x48);
// 0x9CE0 direct path zeros the entire record then sets exactly these fields.
// Seconds are multiplied by 1e9 as in the original; no invented birth time.
constexpr Stat make_stat(uint64_t size, int64_t access_seconds, int64_t modification_seconds, uint16_t mode) {
    Stat result{};
    result.size = size;
    result.access_ns = uint64_t(access_seconds) * 1000000000ull;
    result.modification_ns = uint64_t(modification_seconds) * 1000000000ull;
    result.attributes = attribute_bits(mode);
    result.mode = mode;
    return result;
}
struct DirectoryEntry {
    uint64_t size;
    uint32_t attributes;
    uint16_t name_length;
    uint16_t path_length;
    uint16_t name_offset;
    uint8_t unknown12[6];
    char path[1024];
};
static_assert(sizeof(DirectoryEntry) == 0x418);
static_assert(offsetof(DirectoryEntry, path) == 0x18);
static_assert(offsetof(DirectoryEntry, name_offset) == 0x10);
// 0x18AC0 directory worker clears 0x418 bytes and records full path,
// basename length and offset. The caller supplies a path already assembled
// under the original namespace; this helper never accesses the filesystem.
inline bool make_directory_entry(DirectoryEntry& result, std::string_view path,
                                 std::string_view name, uint64_t size, uint16_t mode) {
    result = {};
    if (path.size() >= sizeof(result.path) || name.empty() || name.size() > path.size() ||
        !path.ends_with(name) || path.find('\0') != path.npos || name.find('\0') != name.npos ||
        (name.size() != path.size() && path[path.size() - name.size() - 1] != '/')) return false;
    result.size = size;
    result.attributes = attribute_bits(mode);
    result.name_length = uint16_t(name.size());
    result.path_length = uint16_t(path.size());
    result.name_offset = uint16_t(path.size() - name.size());
    std::memcpy(result.path, path.data(), path.size());
    return true;
}
} // namespace d1::fios_contract
