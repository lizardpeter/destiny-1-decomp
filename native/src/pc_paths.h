#pragma once
#include "pc_file.h"
#include <optional>

namespace d1::pc_io {
// Native filesystem namespace: assets are immutable, user data writable.
// Paths are relative to an explicit root, not console mount strings.
// Canonical containment prevents ordinary traversal and existing symlink escapes;
// this is not a race-proof sandbox against a concurrent hostile local process.
enum class Root { assets, user_data };
struct Info {
    std::filesystem::file_type type = std::filesystem::file_type::none;
    uint64_t size = 0;
    std::filesystem::file_time_type modified{};
};
class Paths {
    std::filesystem::path assets_, data_;
    static bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
        auto p = path.begin();
        for (auto r = root.begin(); r != root.end(); ++r, ++p)
            if (p == path.end() || *p != *r) return false;
        return true;
    }
public:
    Paths(const std::filesystem::path& assets, const std::filesystem::path& data)
        : assets_(std::filesystem::canonical(assets)), data_(std::filesystem::canonical(data)) {
        if (!std::filesystem::is_directory(assets_) || !std::filesystem::is_directory(data_) ||
            within(assets_, data_) || within(data_, assets_))
            throw std::invalid_argument("native asset and data roots must be separate directories");
    }
    std::filesystem::path resolve(Root root, const std::filesystem::path& name, bool write, std::error_code& ec) const {
        ec.clear();
        if (write && root == Root::assets) { ec = error(std::errc::read_only_file_system); return {}; }
        if (name.has_root_path()) { ec = error(std::errc::invalid_argument); return {}; }
        // Reject parent components before resolution: no escape-and-reentry.
        for (const auto& part : name)
            if (part == "..") { ec = error(std::errc::permission_denied); return {}; }
        const auto& base = root == Root::assets ? assets_ : data_;
        auto full = std::filesystem::weakly_canonical(base / name, ec);
        if (ec) return {};
        if (!within(base, full) || (write && (full == base || within(assets_, full)))) {
            ec = error(std::errc::permission_denied); return {};
        }
        return full;
    }
    std::shared_ptr<File> open(Root root, const std::filesystem::path& name, OpenOptions options, std::error_code& ec) const {
        const bool write = options.access != Access::read || options.create || options.truncate || options.exclusive;
        auto path = resolve(root, name, write, ec);
        if (ec) return {};
        if (std::filesystem::is_directory(path, ec)) { ec = error(std::errc::is_a_directory); return {}; }
        // is_directory can set ENOENT for a prospective newly created file.
        if (ec && ec != std::errc::no_such_file_or_directory) return {};
        return File::open(path, options, ec);
    }
    std::optional<Info> stat(Root root, const std::filesystem::path& name, std::error_code& ec) const {
        const auto path = resolve(root, name, false, ec);
        if (ec) return {};
        auto st = std::filesystem::status(path, ec);
        if (ec) return {};
        Info info; info.type = st.type();
        if (std::filesystem::is_regular_file(st)) { info.size = std::filesystem::file_size(path, ec); if (ec) return {}; }
        info.modified = std::filesystem::last_write_time(path, ec);
        if (ec) return {};
        return info;
    }
    bool create_directory(const std::filesystem::path& name, std::error_code& ec) const {
        auto path = resolve(Root::user_data, name, true, ec);
        if (ec) return false;
        return std::filesystem::create_directory(path, ec);
    }
    bool remove_file(const std::filesystem::path& name, std::error_code& ec) const {
        auto path = resolve(Root::user_data, name, true, ec);
        if (ec) return false;
        if (std::filesystem::is_directory(path, ec)) { ec = error(std::errc::is_a_directory); return false; }
        if (ec) return false;
        return std::filesystem::remove(path, ec);
    }
    bool remove_directory(const std::filesystem::path& name, std::error_code& ec) const {
        auto path = resolve(Root::user_data, name, true, ec);
        if (ec) return false;
        if (!std::filesystem::is_directory(path, ec)) {
            if (!ec) ec = error(std::errc::not_a_directory);
            return false;
        }
        return std::filesystem::remove(path, ec); // only an empty directory
    }
    std::filesystem::directory_iterator list(Root root, const std::filesystem::path& name, std::error_code& ec) const {
        auto path = resolve(root, name, false, ec);
        if (ec) return {};
        return std::filesystem::directory_iterator(path, ec);
    }
};
} // namespace d1::pc_io
