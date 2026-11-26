#pragma once
#include "pc_file_queue.h"
#include "pc_paths.h"

namespace d1::pc_io {
// Native game-facing file service. No retail mount names, layouts, error codes,
// or callback ABI. All public opens must go through the asset/data namespace.
class Service {
    Paths paths_;
    Queue queue_;
public:
    Service(const std::filesystem::path& assets, const std::filesystem::path& data,
            unsigned workers = 2, size_t max_operations = 1024)
        : paths_(assets, data), queue_(workers, max_operations) {}
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
    Result wait(Queue::Id id) { return queue_.wait(id); }
    bool poll(Queue::Id id, Result& result) { return queue_.poll(id, result); }
    bool forget(Queue::Id id) { return queue_.forget(id); }
    const Paths& paths() const { return paths_; }
};
} // namespace d1::pc_io
