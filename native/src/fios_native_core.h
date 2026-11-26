#pragma once
#include "fios_binary_contract.h"
#include "pc_file_service.h"
#include "pc_mount_adapter.h"

namespace d1::fios_native {
using pc_io::Result;
using pc_io::Queue;
using pc_io::Root;
using pc_io::RetailPath;
using pc_io::error;

// Convert host error conditions to the retail FreeBSD numbers, then apply
// the exact recovered module mapper. Unknown conditions remain explicit I/O
// failures rather than being mistaken for successful operations.
inline uint32_t status(const std::error_code& ec) {
    if (!ec) return 0;
    using E = std::errc;
    uint32_t n = 5;
    if (ec == E::operation_not_permitted) n = 1;
    else if (ec == E::no_such_file_or_directory) n = 2;
    else if (ec == E::bad_file_descriptor) n = 9;
    else if (ec == E::permission_denied) n = 13;
    else if (ec == E::bad_address) n = 14;
    else if (ec == E::device_or_resource_busy) n = 16;
    else if (ec == E::file_exists) n = 17;
    else if (ec == E::no_such_device) n = 19;
    else if (ec == E::not_a_directory) n = 20;
    else if (ec == E::is_a_directory) n = 21;
    else if (ec == E::invalid_argument) n = 22;
    else if (ec == E::too_many_files_open) n = 24;
    else if (ec == E::file_too_large) n = 27;
    else if (ec == E::no_space_on_device) n = 28;
    else if (ec == E::read_only_file_system) n = 30;
    else if (ec == E::result_out_of_range) n = 34;
    else if (ec == E::operation_not_supported) n = 45;
    else if (ec == E::timed_out) n = 60;
    else if (ec == E::filename_too_long) n = 63;
    else if (ec == E::directory_not_empty) n = 66;
    else if (ec == E::not_enough_memory) n = 12;
    else if (ec == E::resource_unavailable_try_again) n = 35;
    else if (ec == E::operation_canceled) n = 85;
    return fios_contract::from_encoded_kernel_status(0x80020000u | n);
}
inline int64_t sync_count(Result r) {
    if (!r.error) return int64_t(r.bytes);
    return int32_t(status(r.error));
}

// Host service for the recovered retail filesystem operations. The three
// handle classes have disjoint namespaces; IDs are not recycled. The loader
// must only redirect a complete cohort, never mix these with original handles.
class Core {
    std::mutex directory_mutex_;
    std::unordered_map<uint32_t, std::string> directory_paths_;
    pc_io::Service service_;
    static constexpr uint32_t kMask = 0x0fffffffu;
    static constexpr uint32_t kFile = 0x10000000u, kDirectory = 0x20000000u, kOperation = 0x30000000u;
    static uint32_t encode(uint32_t kind, uint32_t id) { return id && id <= kMask ? kind | id : 0; }
    static uint32_t decode(uint32_t kind, uint32_t id) { return (id & ~kMask) == kind ? id & kMask : 0; }
    std::optional<RetailPath> path(const char* name, bool writing, std::error_code& ec) {
        auto p = pc_io::decode_retail_path(name, ec);
        if (!p) return {};
        if (service_.paths().resolve(p->root, p->relative, writing, ec).empty() || ec) return {};
        if (!p->mount.empty()) {
            service_.paths().create_directory(p->mount, ec);
            if (ec) return {};
        }
        return p;
    }
    static Queue::Callback wrap(Queue::Callback callback) {
        if (!callback) return {};
        return [callback = std::move(callback)](Queue::Id id, Result r) {
            callback(encode(kOperation, id), r);
        };
    }
public:
    Core(const std::filesystem::path& assets, const std::filesystem::path& data,
         unsigned workers = 2, size_t max_ops = 1024, uint64_t bytes = 256ull << 20)
        : service_(assets, data, workers, max_ops, bytes) {}
    ~Core() { service_.drain(); }
    uint32_t open(const char* name, uint16_t flags, int32_t mode, uint32_t& handle) {
        handle = 0;
        const auto k = fios_contract::to_kernel_open_flags(flags);
        // 0x10000 is a verified kernel flag, but its native caching contract
        // is not yet recovered; do not claim it is implemented.
        if (k & 0x10000) return 0x80820006;
        pc_io::OpenOptions options;
        options.access = (k & 3) == 1 ? pc_io::Access::write : (k & 3) == 2 ? pc_io::Access::read_write : pc_io::Access::read;
        options.create = (k & 0x200) != 0;
        options.truncate = (k & 0x400) != 0;
        options.append = (k & 8) != 0;
        options.permissions = mode == -1 ? 0777 : uint16_t(mode) & 0777;
        std::error_code ec;
        auto p = path(name, options.access != pc_io::Access::read || options.create || options.truncate || options.append, ec);
        if (!p) return status(ec);
        const auto id = service_.open(p->root, p->relative, options, ec);
        if (!id) return status(ec);
        handle = encode(kFile, id);
        if (!handle) { service_.close(id); return 0x80820002; }
        return 0;
    }
    bool valid(uint32_t handle) {
        if (auto id = decode(kFile, handle)) return bool(service_.file(id));
        if (auto id = decode(kDirectory, handle)) return service_.valid_directory(id);
        if (auto id = decode(kOperation, handle)) return service_.valid_operation(id);
        return false;
    }
    uint32_t close(uint32_t handle) {
        return service_.close(decode(kFile, handle)) ? 0 : 0x8082000b;
    }
    int64_t read(uint32_t handle, void* data, uint64_t count) {
        auto f = service_.file(decode(kFile, handle));
        return f ? sync_count(f->read(data, count)) : int32_t(0x8082000b);
    }
    int64_t write(uint32_t handle, const void* data, uint64_t count) {
        auto f = service_.file(decode(kFile, handle));
        return f ? sync_count(f->write(data, count)) : int32_t(0x8082000b);
    }
    int64_t size(uint32_t handle) {
        auto f = service_.file(decode(kFile, handle));
        if (!f) return -1;
        auto r = f->size(); return r ? int64_t(r.bytes) : -1;
    }
    int64_t tell(uint32_t handle) {
        auto f = service_.file(decode(kFile, handle)); return f ? int64_t(f->tell()) : -1;
    }
    int64_t seek(uint32_t handle, int64_t offset, uint32_t origin) {
        auto f = service_.file(decode(kFile, handle));
        if (!f || origin > 2) return -1;
        auto r = f->seek(offset, static_cast<pc_io::File::Origin>(origin)); return r ? int64_t(r.bytes) : -1;
    }
    uint32_t sync(uint32_t handle) {
        auto f = service_.file(decode(kFile, handle));
        return f ? status(f->sync()) : 0x8082000b;
    }
    uint32_t stat(const char* name, fios_contract::Stat& out) {
        out = {};
        std::error_code ec;
        auto p = path(name, false, ec);
        if (!p) return status(ec);
        auto full = service_.paths().resolve(p->root, p->relative, false, ec);
        if (ec) return status(ec);
#ifdef _WIN32
        struct _stat64 st{};
        if (_wstat64(full.c_str(), &st)) return status(pc_io::os_error());
#else
        struct stat st{};
        if (::stat(full.c_str(), &st)) return status(pc_io::os_error());
#endif
        if (st.st_size < 0) return status(error(std::errc::io_error));
        out = fios_contract::make_stat(uint64_t(st.st_size), int64_t(st.st_atime), int64_t(st.st_mtime), uint16_t(st.st_mode));
        return 0;
    }
    uint32_t open_directory(const char* name, uint32_t& handle) {
        handle = 0;
        std::error_code ec;
        auto p = path(name, false, ec);
        if (!p) return status(ec);
        const auto id = service_.open_directory(p->root, p->relative, ec);
        if (!id) return status(ec);
        handle = encode(kDirectory, id);
        if (!handle) { service_.close_directory(id); return 0x80820002; }
        try {
            std::lock_guard lock(directory_mutex_);
            directory_paths_.emplace(id, name);
        } catch (...) { service_.close_directory(id); handle = 0; throw; }
        return 0;
    }
    uint32_t read_directory(uint32_t handle, fios_contract::DirectoryEntry& out) {
        out = {};
        const auto id = decode(kDirectory, handle);
        std::string base;
        {
            std::lock_guard lock(directory_mutex_);
            auto it = directory_paths_.find(id);
            if (it == directory_paths_.end()) return 0x8082000c;
            base = it->second;
        }
        std::error_code ec;
        const auto item = service_.next_directory(id, ec);
        if (!item) return ec ? status(ec) : 0x80820010;
        const auto leaf = item->name.generic_string();
        if (!base.empty() && base.back() != '/') base += '/';
        base += leaf;
        fios_contract::Stat st{};
        if (auto result = stat(base.c_str(), st)) return result;
        if (!fios_contract::make_directory_entry(out, base, leaf, st.size, uint16_t(st.mode))) return 0x80820018;
        return 0;
    }
    uint32_t close_directory(uint32_t handle) {
        const auto id = decode(kDirectory, handle);
        { std::lock_guard lock(directory_mutex_); directory_paths_.erase(id); }
        return service_.close_directory(id) ? 0 : 0x8082000c;
    }
    enum class DeleteKind { any, file, directory };
    uint32_t remove(const char* name, DeleteKind kind) {
        std::error_code ec;
        auto p = path(name, true, ec);
        if (!p) return status(ec);
        if (kind == DeleteKind::any) {
            auto info = service_.paths().stat(p->root, p->relative, ec);
            if (!info) return status(ec);
            kind = info->type == std::filesystem::file_type::directory ? DeleteKind::directory : DeleteKind::file;
        }
        const bool ok = kind == DeleteKind::directory ? service_.paths().remove_directory(p->relative, ec)
                                                       : service_.paths().remove_file(p->relative, ec);
        if (!ok && !ec) ec = error(std::errc::no_such_file_or_directory);
        return status(ec);
    }
    uint32_t rename(const char* from, const char* to) {
        std::error_code ec;
        auto a = path(from, true, ec);
        if (!a) return status(ec);
        auto b = path(to, true, ec);
        if (!b) return status(ec);
        const auto src = service_.paths().resolve(a->root, a->relative, true, ec);
        if (ec) return status(ec);
        const auto dst = service_.paths().resolve(b->root, b->relative, true, ec);
        if (ec) return status(ec);
        std::filesystem::rename(src, dst, ec);
        return status(ec);
    }
    uint32_t read_at(uint32_t handle, uint64_t offset, uint64_t size,
                     Queue::Delivery delivery, Queue::Callback callback, std::error_code& ec) {
        return encode(kOperation, service_.read_staged(decode(kFile, handle), offset, size,
            std::move(delivery), wrap(std::move(callback)), ec));
    }
    uint32_t write_at(uint32_t handle, uint64_t offset, const void* source, uint64_t size,
                      Queue::Callback callback, std::error_code& ec) {
        return encode(kOperation, service_.write_at(decode(kFile, handle), offset, source, size,
            wrap(std::move(callback)), ec));
    }
    uint32_t create_directory(const char* name, Queue::Callback callback, std::error_code& ec) {
        auto p = path(name, true, ec);
        if (!p) return 0;
        return encode(kOperation, service_.submit_task([this, relative = std::move(p->relative)] {
            std::error_code result;
            if (!service_.paths().create_directory(relative, result) && !result) result = error(std::errc::file_exists);
            return Result{0, result};
        }, wrap(std::move(callback)), ec));
    }
    bool poll(uint32_t handle, Result& result) { return service_.poll(decode(kOperation, handle), result); }
    Result wait(uint32_t handle) { return service_.wait(decode(kOperation, handle)); }
    bool forget(uint32_t handle) { return service_.forget(decode(kOperation, handle)); }
    bool delete_operation(uint32_t handle) { return service_.delete_operation(decode(kOperation, handle)); }
    void drain() { service_.drain(); }
};
} // namespace d1::fios_native
