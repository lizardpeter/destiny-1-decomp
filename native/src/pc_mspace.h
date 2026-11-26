#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <iterator>
#include <new>
#include <map>
#include <mutex>
#include <stdexcept>

namespace d1::pc_libc {
// Independent bounded heap model over caller-owned memory. Metadata is host-owned.
// Not a retail allocator ABI adapter: no original chunk layout or stats ABI assumed.
class Mspace {
    struct Block { size_t size; bool free; };
    using Map = std::map<size_t, Block>;
    std::byte* base_;
    size_t capacity_;
    mutable std::mutex mutex_;
    Map blocks_;
    static constexpr size_t minimum = 16;
    static bool alignment_valid(size_t a) { return a && !(a & (a - 1)); }
    static bool rounded(size_t n, size_t& result) {
        n = std::max(n, minimum);
        if (n > SIZE_MAX - (minimum - 1)) return false;
        result = (n + minimum - 1) & ~(minimum - 1);
        return true;
    }
    Map::iterator find(void* p) {
        auto addr = reinterpret_cast<uintptr_t>(p), base = reinterpret_cast<uintptr_t>(base_);
        if (addr < base || addr - base >= capacity_) return blocks_.end();
        auto it = blocks_.find(size_t(addr - base));
        return it != blocks_.end() && !it->second.free ? it : blocks_.end();
    }
    void* allocate_locked(size_t n, size_t align) {
        size_t size;
        if (!alignment_valid(align) || !rounded(n, size)) return nullptr;
        align = std::max(align, minimum);
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (!it->second.free) continue;
            const size_t start = it->first, available = it->second.size;
            const uintptr_t address = reinterpret_cast<uintptr_t>(base_) + start;
            const size_t padding = size_t((-address) & (align - 1));
            if (padding > available || size > available - padding) continue;
            const size_t tail = available - padding - size;
            auto allocated = it;
            // Insert all needed metadata before changing the original free block.
            if (padding) allocated = blocks_.emplace(start + padding, Block{size, false}).first;
            try {
                if (tail) blocks_.emplace(start + padding + size, Block{tail, true});
            } catch (...) {
                if (padding) blocks_.erase(allocated);
                throw;
            }
            if (padding) it->second.size = padding;
            else it->second = {size, false};
            return base_ + allocated->first;
        }
        return nullptr;
    }
    void release_locked(Map::iterator it) {
        it->second.free = true;
        auto next = std::next(it);
        if (next != blocks_.end() && next->second.free) {
            it->second.size += next->second.size;
            blocks_.erase(next);
        }
        if (it != blocks_.begin()) {
            auto prev = std::prev(it);
            if (prev->second.free) {
                prev->second.size += it->second.size;
                blocks_.erase(it);
            }
        }
    }
public:
    struct Stats { size_t capacity, allocated, free, largest_free, live; };
    Mspace(void* memory, size_t capacity) : base_(static_cast<std::byte*>(memory)), capacity_(capacity & ~(minimum - 1)) {
        const auto addr = reinterpret_cast<uintptr_t>(base_);
        if (!base_ || addr % minimum || !capacity_ || capacity_ > UINTPTR_MAX - addr)
            throw std::invalid_argument("invalid mspace range");
        blocks_.emplace(0, Block{capacity_, true});
    }
    void* allocate(size_t n, size_t align = minimum) {
        std::lock_guard lock(mutex_);
        try { return allocate_locked(n, align); } catch (const std::bad_alloc&) { return nullptr; }
    }
    void* calloc(size_t count, size_t size) {
        if (count && size > SIZE_MAX / count) return nullptr;
        const size_t n = count * size;
        std::lock_guard lock(mutex_);
        try {
            void* p = allocate_locked(n, minimum);
            if (p) std::memset(p, 0, n);
            return p;
        } catch (const std::bad_alloc&) { return nullptr; }
    }
    bool release(void* p) {
        if (!p) return true;
        std::lock_guard lock(mutex_);
        auto it = find(p);
        if (it == blocks_.end()) return false;
        release_locked(it);
        return true;
    }
    size_t usable_size(void* p) {
        std::lock_guard lock(mutex_);
        auto it = find(p);
        return it == blocks_.end() ? 0 : it->second.size;
    }
    // Zero-size realloc releases the old block; this is a model policy, not retail evidence.
    void* reallocate(void* p, size_t n, size_t align = minimum) {
        if (!alignment_valid(align)) return nullptr;
        if (!p) return allocate(n, align);
        std::lock_guard lock(mutex_);
        auto it = find(p);
        if (it == blocks_.end()) return nullptr;
        if (!n) { release_locked(it); return nullptr; }
        size_t size;
        if (!rounded(n, size)) return nullptr;
        align = std::max(align, minimum);
        try {
            if (!(reinterpret_cast<uintptr_t>(p) & (align - 1))) {
                const size_t old = it->second.size;
                auto next = std::next(it);
                if (size <= old) {
                    if (size != old) {
                        auto tail = blocks_.emplace(it->first + size, Block{old - size, false}).first;
                        it->second.size = size;
                        release_locked(tail);
                    }
                    return p;
                }
                if (next != blocks_.end() && next->second.free && size - old <= next->second.size) {
                    const size_t remain = next->second.size - (size - old);
                    if (remain) blocks_.emplace(it->first + size, Block{remain, true});
                    blocks_.erase(next);
                    it->second.size = size;
                    return p;
                }
            }
            void* fresh = allocate_locked(n, align);
            if (!fresh) return nullptr;
            std::memcpy(fresh, p, std::min(it->second.size, n));
            release_locked(it);
            return fresh;
        } catch (const std::bad_alloc&) { return nullptr; }
    }
    Stats stats() const {
        std::lock_guard lock(mutex_);
        Stats s{capacity_, 0, 0, 0, 0};
        for (const auto& [offset, b] : blocks_) {
            (void)offset;
            if (b.free) { s.free += b.size; s.largest_free = std::max(s.largest_free, b.size); }
            else { s.allocated += b.size; ++s.live; }
        }
        return s;
    }
};
} // namespace d1::pc_libc
