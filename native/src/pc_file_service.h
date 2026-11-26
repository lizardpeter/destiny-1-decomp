#pragma once
#include "pc_file_queue.h"
#include "pc_paths.h"
#include "pc_directory.h"

namespace d1::pc_io {
// Native game-facing file service. No retail mount names, layouts, error codes,
// or callback ABI. All public opens must go through the asset/data namespace.
class Service {
    Paths paths_;
    DirectoryRegistry directories_;
    Queue queue_; // drain before destroying paths or directory handles
public:
    Service(const std::filesystem::path& assets, const std::filesystem::path& data,
            unsigned workers = 2, size_t max_operations = 1024, uint64_t max_bytes = 256ull << 20)
        : paths_(assets, data), queue_(workers, max_operations, max_bytes) {}
    Queue::Id open(Root root, const std::filesystem::path& name, OpenOptions options, std::error_code& ec) {
        auto f = paths_.open(root, name, options, ec);
        return f ? queue_.attach(std::move(f), ec) : 0;
    }
    bool close(Queue::Id id) { return queue_.close(id); }
    std::shared_ptr<File> file(Queue::Id id) { return queue_.get(id); }
    Queue::Id read_at(Queue::Id id, uint64_t off, void* data, uint64_t n, Queue::Callback cb, std::error_code& ec) {
        return queue_.read_at(id, off, data, n, std::move(cb), ec);
    }
    Queue::Id write_at(Queue::Id id, uint64_t off, const void* data, uint64_t n, Queue::Callback cb, std::error_code& ec) {
        return queue_.write_at(id, off, data, n, std::move(cb), ec);
    }
    Queue::Id read_staged(Queue::Id id, uint64_t off, uint64_t n, Queue::Delivery delivery, Queue::Callback cb, std::error_code& ec) {
        return queue_.read_staged(id, off, n, std::move(delivery), std::move(cb), ec);
    }
    Queue::Id submit_task(Queue::Task task, Queue::Callback cb, std::error_code& ec) {
        return queue_.submit_task(std::move(task), std::move(cb), ec);
    }
    uint64_t staging_bytes() const { return queue_.staging_bytes(); }
    Result wait(Queue::Id id) { return queue_.wait(id); }
    bool poll(Queue::Id id, Result& result) { return queue_.poll(id, result); }
    bool forget(Queue::Id id) { return queue_.forget(id); }
    uint32_t open_directory(Root root, const std::filesystem::path& name, std::error_code& ec) {
        return directories_.open(paths_, root, name, ec);
    }
    std::optional<DirectoryEntry> next_directory(uint32_t id, std::error_code& ec) {
        return directories_.next(id, ec);
    }
    bool close_directory(uint32_t id) { return directories_.close(id); }
    const Paths& paths() const { return paths_; }
};
} // namespace d1::pc_io
