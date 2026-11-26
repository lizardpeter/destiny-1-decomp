#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace d1::pc_io {
struct Result {
    uint64_t bytes = 0;
    std::error_code error;
    explicit operator bool() const { return !error; }
};
inline std::error_code error(std::errc e) { return std::make_error_code(e); }
inline std::error_code os_error() { return {errno, std::generic_category()}; }
inline bool valid_range(uint64_t offset, uint64_t size) {
    return offset <= uint64_t(INT64_MAX) && size <= uint64_t(INT64_MAX) - offset && size <= SIZE_MAX;
}
enum class Access { read, write, read_write };
struct OpenOptions {
    Access access = Access::read;
    bool create = false, truncate = false, exclusive = false;
    bool append = false;
    uint16_t permissions = 0644;
};
class File {
    int fd_;
    Access access_;
    bool append_;
    std::mutex cursor_mutex_, io_mutex_;
    uint64_t cursor_ = 0;
    File(int fd, Access access, bool append) : fd_(fd), access_(access), append_(append) {}
    Result transfer(bool write, void* dst, const void* src, uint64_t size, uint64_t offset) {
        if ((write && access_ == Access::read) || (!write && access_ == Access::write))
            return {0, error(std::errc::bad_file_descriptor)};
        if (!valid_range(offset, size) ||
            (size && !(write ? src : dst))) return {0, error(std::errc::invalid_argument)};
#ifdef _WIN32
        // CRT positional I/O is implemented as seek + transfer under a per-file lock.
        // It never changes the independent logical sequential cursor.
        std::lock_guard lock(io_mutex_);
        if (_lseeki64(fd_, static_cast<int64_t>(offset), SEEK_SET) < 0) return {0, os_error()};
#endif
        Result out;
        while (out.bytes < size) {
            const auto chunk = static_cast<unsigned>(std::min<uint64_t>(size - out.bytes, 1u << 30));
            const auto* in = static_cast<const unsigned char*>(src);
            auto* to = static_cast<unsigned char*>(dst);
#ifdef _WIN32
            const int n = write ? _write(fd_, in + out.bytes, chunk) : _read(fd_, to + out.bytes, chunk);
#else
            const ssize_t n = write ? ::pwrite(fd_, in + out.bytes, chunk, static_cast<off_t>(offset + out.bytes))
                                    : ::pread(fd_, to + out.bytes, chunk, static_cast<off_t>(offset + out.bytes));
#endif
            if (n < 0) { if (errno == EINTR) continue; out.error = os_error(); break; }
            if (!n) { if (write) out.error = error(std::errc::io_error); break; }
            out.bytes += uint64_t(n);
        }
        return out;
    }
public:
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() {
#ifdef _WIN32
        _close(fd_);
#else
        ::close(fd_);
#endif
    }
    static std::shared_ptr<File> open(const std::filesystem::path& path, OpenOptions options, std::error_code& ec) {
        ec.clear();
        if ((options.access == Access::read && (options.create || options.truncate || options.append)) ||
            (options.exclusive && !options.create)) { ec = error(std::errc::invalid_argument); return {}; }
        int flags = options.access == Access::read ? O_RDONLY : options.access == Access::write ? O_WRONLY : O_RDWR;
        if (options.create) flags |= O_CREAT;
        if (options.truncate) flags |= O_TRUNC;
        if (options.exclusive) flags |= O_EXCL;
        if (options.append) flags |= O_APPEND;
#ifdef _WIN32
        const int mode = ((options.permissions & 0400) ? _S_IREAD : 0) | ((options.permissions & 0200) ? _S_IWRITE : 0);
        const int fd = _wopen(path.c_str(), flags | _O_BINARY | _O_NOINHERIT, mode);
#else
        static_assert(sizeof(off_t) >= 8);
        const int fd = ::open(path.c_str(), flags | O_CLOEXEC, options.permissions & 0777);
#endif
        if (fd < 0) { ec = os_error(); return {}; }
        File* raw;
        try { raw = new File(fd, options.access, options.append); }
        catch (...) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
            throw;
        }
        return std::shared_ptr<File>(raw);
    }
    Result read_at(uint64_t offset, void* dst, uint64_t size) { return transfer(false, dst, nullptr, size, offset); }
    Result write_at(uint64_t offset, const void* src, uint64_t size) {
        // Linux pwrite on an O_APPEND descriptor ignores its offset. Do not
        // silently substitute append for explicit positional I/O.
        if (append_) return {0, error(std::errc::operation_not_supported)};
        return transfer(true, nullptr, src, size, offset);
    }
    Result read(void* dst, uint64_t size) {
        std::lock_guard lock(cursor_mutex_);
        auto r = read_at(cursor_, dst, size); cursor_ += r.bytes; return r;
    }
    Result write(const void* src, uint64_t size) {
        std::lock_guard lock(cursor_mutex_);
        if (!append_) { auto r = write_at(cursor_, src, size); cursor_ += r.bytes; return r; }
        if (!valid_range(0, size) || (size && !src)) return {0, error(std::errc::invalid_argument)};
        std::lock_guard io_lock(io_mutex_);
        Result out;
        while (out.bytes < size) {
            const auto chunk = static_cast<unsigned>(std::min<uint64_t>(size - out.bytes, 1u << 30));
            const auto* in = static_cast<const unsigned char*>(src) + out.bytes;
#ifdef _WIN32
            const int n = _write(fd_, in, chunk);
#else
            const ssize_t n = ::write(fd_, in, chunk);
#endif
            if (n < 0) { if (errno == EINTR) continue; out.error = os_error(); break; }
            if (!n) { out.error = error(std::errc::io_error); break; }
            out.bytes += uint64_t(n);
        }
        if (out.bytes) {
#ifdef _WIN32
            const auto pos = _lseeki64(fd_, 0, SEEK_CUR);
#else
            const auto pos = ::lseek(fd_, 0, SEEK_CUR);
#endif
            if (pos >= 0) cursor_ = uint64_t(pos);
            else if (!out.error) out.error = os_error();
        }
        return out;
    }
    Result size() const {
#ifdef _WIN32
        struct _stat64 st{};
        if (_fstat64(fd_, &st)) return {0, os_error()};
#else
        struct stat st{};
        if (fstat(fd_, &st)) return {0, os_error()};
#endif
        if (st.st_size < 0) return {0, error(std::errc::io_error)};
        return {uint64_t(st.st_size), {}};
    }
    enum class Origin { begin, current, end };
    Result seek(int64_t delta, Origin origin) {
        std::lock_guard lock(cursor_mutex_);
        uint64_t base = origin == Origin::current ? cursor_ : 0;
        if (origin == Origin::end) { auto r = size(); if (!r) return r; base = r.bytes; }
        if (delta < 0) {
            const uint64_t magnitude = uint64_t(-(delta + 1)) + 1;
            if (magnitude > base) return {0, error(std::errc::invalid_argument)};
            base -= magnitude;
        } else {
            if (base > uint64_t(INT64_MAX) - uint64_t(delta)) return {0, error(std::errc::value_too_large)};
            base += uint64_t(delta);
        }
        cursor_ = base; return {base, {}};
    }
    uint64_t tell() { std::lock_guard lock(cursor_mutex_); return cursor_; }
    std::error_code sync() {
#ifdef _WIN32
        std::lock_guard lock(io_mutex_);
        if (_commit(fd_)) return os_error();
#else
        if (fsync(fd_)) return os_error();
#endif
        return {};
    }
};
} // namespace d1::pc_io
