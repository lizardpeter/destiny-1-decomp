#pragma once
#include <cstddef>
#include <cstdint>

namespace d1::fios_contract {
// Retail 01.33 callsite evidence only (0x80670 and 0x80740).
// The callback and user context offsets are observed. Unused/unknown fields
// stay opaque until the original FIOS implementation is analyzed.
struct OperationAttributes {
    uint64_t unknown00;
    uint64_t callback;
    uint64_t user_context;
    uint8_t unknown18[24];
};
static_assert(sizeof(OperationAttributes) == 0x30);
static_assert(offsetof(OperationAttributes, callback) == 8);
static_assert(offsetof(OperationAttributes, user_context) == 0x10);
struct OpenAttributes {
    uint16_t flags;
    uint8_t unknown02[22];
};
static_assert(sizeof(OpenAttributes) == 0x18);
// Exact flag translation from engine wrapper 0x804E0. This does not assign
// undocumented semantic names to the FIOS bit values.
constexpr uint32_t translate_open_flags(uint32_t flags) {
    uint32_t v = (flags & 0xC) == 0xC ? 3 : (flags & 8) ? 2 : (flags >> 2) & 1;
    if (flags & 1) v |= 8;
    else if (flags & 2) v |= 0x18;
    return v | ((v >> 2) & 2);
}
constexpr uint32_t kQueueFailure = 0x80820001;
constexpr uint32_t kSizeFailure = 0x8082000b;
constexpr uint32_t kObservedReadStatus = 0x80820010; // recognized by 0x80570; exact source not yet verified
constexpr uint8_t kTerminalCallbackEvent = 1;
} // namespace d1::fios_contract
