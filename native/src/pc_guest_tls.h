#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace d1::pc_tls {
inline size_t alignment(size_t requested) {
    if (!requested || (requested & (requested - 1))) throw std::invalid_argument("invalid TLS alignment");
    return std::max(requested, size_t(64));
}
inline size_t round_up(size_t n, size_t align) {
    if (!align || (align & (align - 1)) || n > SIZE_MAX - (align - 1))
        throw std::invalid_argument("invalid TLS extent");
    return (n + align - 1) & ~(align - 1);
}
struct Image {
    const void* init;
    size_t filesz, memsz, align;
    void validate() const {
        alignment(align);
        if (filesz > memsz || (filesz && !init)) throw std::invalid_argument("invalid TLS image");
    }
};
class Block {
    size_t align_;
    std::byte* data_;
public:
    Block(size_t size, size_t align) : align_(alignment(align)),
        data_(static_cast<std::byte*>(::operator new(std::max(size,size_t(1)), std::align_val_t(align_)))) {
        std::memset(data_, 0, size);
    }
    ~Block() { ::operator delete(data_, std::align_val_t(align_)); }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    std::byte* data() const { return data_; }
};
// Variant II layout: initial image immediately before the TCB, with the same
// rounded static extent used by the guest's negative TLS offsets.
class StaticBlock {
    size_t extent_, prefix_;
    Block block_;
    static size_t total(size_t extent, size_t prefix, size_t tcb_size) {
        if (extent > SIZE_MAX - prefix || tcb_size > SIZE_MAX - prefix - extent)
            throw std::invalid_argument("TLS allocation overflow");
        return extent + prefix + tcb_size;
    }
    static size_t image_extent(Image image) {
        image.validate();
        return round_up(image.memsz, std::max(image.align,size_t(16)));
    }
public:
    StaticBlock(Image image, size_t tcb_size) : extent_(image_extent(image)),
        prefix_(alignment(image.align)), block_(total(extent_,prefix_,tcb_size), image.align) {
        if (image.filesz) std::memcpy(data(),image.init,image.filesz);
    }
    std::byte* data() const { return block_.data()+prefix_; }
    void* tcb() const { return data()+extent_; }
    size_t extent() const { return extent_; }
};
class DynamicBlocks {
    struct Entry { size_t size; std::unique_ptr<Block> block; };
    std::unordered_map<uint64_t,Entry> blocks_;
public:
    void* get(uint64_t id, size_t offset, Image image) {
        if (!id || offset >= image.memsz) return nullptr;
        auto it=blocks_.find(id);
        if (it==blocks_.end()) {
            image.validate();
            auto block=std::make_unique<Block>(image.memsz,image.align);
            if (image.filesz) std::memcpy(block->data(),image.init,image.filesz);
            it=blocks_.emplace(id,Entry{image.memsz,std::move(block)}).first;
        }
        if (offset >= it->second.size) return nullptr;
        return it->second.block->data()+offset;
    }
};
} // namespace d1::pc_tls
