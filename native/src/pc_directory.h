#pragma once
#include "pc_paths.h"
#include <mutex>
#include <unordered_map>

namespace d1::pc_io {
// Native directory enumeration returns explicit end-of-stream, independent
// of the original FIOS directory-entry struct and return-code convention.
struct DirectoryEntry {
    std::filesystem::path name;
    std::filesystem::file_type type = std::filesystem::file_type::none;
};
class Directory {
    std::mutex mutex_;
    std::filesystem::directory_iterator cursor_;
    std::error_code pending_;
public:
    explicit Directory(std::filesystem::directory_iterator cursor) : cursor_(std::move(cursor)) {}
    std::optional<DirectoryEntry> next(std::error_code& ec) {
        std::lock_guard lock(mutex_);
        ec = pending_;
        if (ec || cursor_ == std::filesystem::directory_iterator()) return {};
        // Report the entry itself, never follow a link outside the root.
        const auto status = cursor_->symlink_status(ec);
        if (ec) return {};
        DirectoryEntry entry{cursor_->path().filename(), status.type()};
        cursor_.increment(pending_);
        // If increment fails, deliver this valid entry and expose the error
        // at the next call. End-of-stream itself leaves the error clear.
        return entry;
    }
};
class DirectoryRegistry {
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<Directory>> dirs_;
    uint32_t next_ = 1;
public:
    uint32_t open(const Paths& paths, Root root, const std::filesystem::path& name, std::error_code& ec) {
        auto cursor = paths.list(root, name, ec);
        if (ec) return 0;
        auto dir = std::make_shared<Directory>(std::move(cursor));
        std::lock_guard lock(mutex_);
        if (next_ > 0x0fffffffu) { ec = error(std::errc::too_many_files_open); return 0; }
        const auto id = next_++;
        dirs_.emplace(id, std::move(dir));
        return id;
    }
    std::shared_ptr<Directory> get(uint32_t id) {
        std::lock_guard lock(mutex_);
        const auto it = dirs_.find(id);
        return it != dirs_.end() ? it->second : nullptr;
    }
    std::optional<DirectoryEntry> next(uint32_t id, std::error_code& ec) {
        auto dir = get(id);
        if (!dir) { ec = error(std::errc::bad_file_descriptor); return {}; }
        return dir->next(ec);
    }
    bool close(uint32_t id) {
        std::lock_guard lock(mutex_);
        return dirs_.erase(id) != 0;
    }
};
} // namespace d1::pc_io
