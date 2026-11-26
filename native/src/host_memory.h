#pragma once
#include <cstdint>

namespace d1 {
// Host mapping metadata used by native GPU resources. No retail ABI types.
uint64_t guest_region_extent(uint64_t addr);
uint64_t guest_mapping_identity(uint64_t addr, uint64_t size);
// Copy a stable mapping while the host's tracked mapping table is locked.
bool guest_copy_current_mapping(uint64_t addr, uint64_t size, uint64_t identity, void* destination);
bool guest_matches_current_mapping(uint64_t addr, uint64_t size, uint64_t identity, const void* snapshot);
// Native ordered CPU copy: validate both captured mapping lifetimes under
// the same lock, including overlapping source and destination.
bool guest_move_current_mapping(uint64_t dst,uint64_t src,uint64_t size,uint64_t dst_id,uint64_t src_id);
bool guest_write_current_mapping(uint64_t addr, uint64_t size, uint64_t identity,
                                 void (*write)(void*, void*), void* context);
void register_native_host_region(uint64_t base, uint64_t size);
} // namespace d1
