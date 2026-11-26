#pragma once
#include <cstdint>

namespace d1 {
// Host mapping metadata used by native GPU resources. No retail ABI types.
uint64_t guest_region_extent(uint64_t addr);
uint64_t guest_mapping_identity(uint64_t addr, uint64_t size);
bool guest_write_current_mapping(uint64_t addr, uint64_t size, uint64_t identity,
                                 void (*write)(void*, void*), void* context);
void register_native_host_region(uint64_t base, uint64_t size);
} // namespace d1
