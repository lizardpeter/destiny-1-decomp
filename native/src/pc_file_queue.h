#pragma once
#include "pc_file.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace d1::pc_io {
// Accepted work retains its file independently of the public handle. The
// caller must keep read buffers alive until completion. Writes own a snapshot.
// A completion callback executes outside service locks, after publishing result.
// Callbacks must not destroy this service or wait for work on this same pool.
class Queue {
public:
    using Id = uint32_t;
    using Callback = std::function<void(Id, Result)>;
private:
    struct Op {
        std::shared_ptr<File> file;
        uint64_t offset = 0, size = 0;
        void* dst = nullptr;
        bool write = false;
        std::vector<unsigned char> source;
        Callback callback;
        Result result;
        bool done = false;
    };
    std::mutex m_;
    std::condition_variable work_, complete_;
    std::unordered_map<Id, std::shared_ptr<File>> files_;
    std::unordered_map<Id, std::shared_ptr<Op>> ops_;
    std::deque<std::pair<Id, std::shared_ptr<Op>>> jobs_;
    std::vector<std::thread> workers_;
    Id next_file_ = 1, next_op_ = 1;
    size_t limit_;
    bool stopping_ = false;
    void worker() {
        for (;;) {
            std::unique_lock lock(m_);
            work_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) return;
            auto [id, op] = std::move(jobs_.front()); jobs_.pop_front();
            lock.unlock();
            Result r = op->write ? op->file->write_at(op->offset, op->source.data(), op->size)
                                 : op->file->read_at(op->offset, op->dst, op->size);
            lock.lock();
            op->result = r; op->done = true;
            lock.unlock();
            complete_.notify_all();
            if (op->callback) op->callback(id, r);
        }
    }
    Id submit(Id file, std::shared_ptr<Op> op, std::error_code& ec) {
        std::lock_guard lock(m_);
        ec.clear();
        auto it = files_.find(file);
        if (it == files_.end()) { ec = error(std::errc::bad_file_descriptor); return 0; }
        if (stopping_ || !next_op_ || ops_.size() >= limit_) { ec = error(std::errc::resource_unavailable_try_again); return 0; }
        op->file = it->second;
        const Id id = next_op_++;
        ops_.emplace(id, op);
        try { jobs_.emplace_back(id, std::move(op)); }
        catch (...) { ops_.erase(id); throw; }
        work_.notify_one();
        return id;
    }
public:
    explicit Queue(unsigned workers = 2, size_t max_operations = 1024) : limit_(max_operations) {
        try {
            for (unsigned i = 0; i < std::max(1u, workers); ++i) workers_.emplace_back([this] { worker(); });
        } catch (...) {
            { std::lock_guard lock(m_); stopping_ = true; }
            work_.notify_all();
            for (auto& t : workers_) t.join();
            throw;
        }
    }
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    ~Queue() {
        { std::lock_guard lock(m_); stopping_ = true; }
        work_.notify_all();
        for (auto& t : workers_) t.join();
    }
    Id open(const std::filesystem::path& path, OpenOptions options, std::error_code& ec) {
        auto file = File::open(path, options, ec);
        if (!file) return 0;
        std::lock_guard lock(m_);
        if (stopping_ || !next_file_) { ec = error(std::errc::too_many_files_open); return 0; }
        const Id id = next_file_++;
        files_.emplace(id, std::move(file));
        return id;
    }
    std::shared_ptr<File> get(Id id) {
        std::lock_guard lock(m_);
        const auto it = files_.find(id);
        return it == files_.end() ? nullptr : it->second;
    }
    bool close(Id id) { std::lock_guard lock(m_); return files_.erase(id) != 0; }
    Id read_at(Id file, uint64_t offset, void* dst, uint64_t n, Callback cb, std::error_code& ec) {
        if (n && !dst) { ec = error(std::errc::invalid_argument); return 0; }
        auto op = std::make_shared<Op>();
        op->offset = offset; op->dst = dst; op->size = n; op->callback = std::move(cb);
        return submit(file, std::move(op), ec);
    }
    Id write_at(Id file, uint64_t offset, const void* src, uint64_t n, Callback cb, std::error_code& ec) {
        if ((n && !src) || n > SIZE_MAX) { ec = error(std::errc::invalid_argument); return 0; }
        auto op = std::make_shared<Op>();
        op->offset = offset; op->size = n; op->write = true; op->callback = std::move(cb);
        if (n) { const auto* p = static_cast<const unsigned char*>(src); op->source.assign(p, p + size_t(n)); }
        return submit(file, std::move(op), ec);
    }
    // Wait keeps a reference even if a concurrent callback deletes the public ID.
    Result wait(Id id) {
        std::unique_lock lock(m_);
        const auto it = ops_.find(id);
        if (it == ops_.end()) return {0, error(std::errc::invalid_argument)};
        auto op = it->second;
        complete_.wait(lock, [&] { return op->done; });
        return op->result;
    }
    bool poll(Id id, Result& result) {
        std::lock_guard lock(m_);
        auto it = ops_.find(id);
        if (it == ops_.end() || !it->second->done) return false;
        result = it->second->result; return true;
    }
    bool forget(Id id) {
        std::lock_guard lock(m_);
        auto it = ops_.find(id);
        if (it == ops_.end() || !it->second->done) return false;
        ops_.erase(it); return true;
    }
};
} // namespace d1::pc_io
