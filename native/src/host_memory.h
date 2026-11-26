#pragma once
#include <cstdint>
#include "native_mapping_contract.h"

namespace d1 {
// Host mapping metadata used by native GPU resources. No retail ABI types.
uint64_t guest_region_extent(uint64_t addr);
// CPU-readable contiguous mapped coverage, not a resource allocation size.
bool guest_readable_range(uint64_t addr,uint64_t size);
uint64_t guest_mapping_identity(uint64_t addr, uint64_t size);
// Copy a stable mapping while the host's tracked mapping table is locked.
bool guest_copy_current_mapping(uint64_t addr, uint64_t size, uint64_t identity, void* destination);
bool guest_matches_current_mapping(uint64_t addr, uint64_t size, uint64_t identity, const void* snapshot);
// Native ordered CPU copy: validate both captured mapping lifetimes under
// the same lock, including overlapping source and destination.
bool guest_move_current_mapping(uint64_t dst,uint64_t src,uint64_t size,uint64_t dst_id,uint64_t src_id);
bool guest_write_current_mapping(uint64_t addr, uint64_t size, uint64_t identity,
                                 void (*write)(void*, void*), void* context);
bool guest_capture_move_spans(uint64_t dst,uint64_t src,uint64_t size,MappingSpans& dst_spans,MappingSpans& src_spans);
bool guest_move_current_spans(uint64_t dst,uint64_t src,uint64_t size,const MappingSpans& dst_spans,const MappingSpans& src_spans);
bool guest_capture_write_spans(uint64_t addr,uint64_t size,MappingSpans& spans);
bool guest_write_current_spans(uint64_t addr,uint64_t size,const MappingSpans& spans,const void* source);
void register_native_host_region(uint64_t base, uint64_t size);
// Private host backing for engine GPU-class regions. Reserve/commit are
// separate so the original two-ended allocator can retain stable addresses.
uint64_t reserve_native_asset_region(uint64_t size);
bool commit_native_asset_region(uint64_t base,uint64_t address,uint64_t size);
} // namespace d1
