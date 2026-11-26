#pragma once
#include <cstddef>
#include <cstring>

namespace d1::pc_libc {
// Stateless, ABI-compatible leaf routines. Do not allocate, free, throw,
// access retail FILE objects, or consult the retail locale/errno.
inline void* copy(void* d, const void* s, size_t n) { return std::memcpy(d, s, n); }
inline void* move(void* d, const void* s, size_t n) { return std::memmove(d, s, n); }
inline void* fill(void* d, int c, size_t n) { return std::memset(d, c, n); }
inline int compare(const void* a, const void* b, size_t n) { return std::memcmp(a, b, n); }
inline const void* find_byte(const void* p, int c, size_t n) { return std::memchr(p, c, n); }
inline size_t length(const char* s) { return std::strlen(s); }
inline int compare_string(const char* a, const char* b) { return std::strcmp(a, b); }
inline int compare_prefix(const char* a, const char* b, size_t n) { return std::strncmp(a, b, n); }
inline const char* find_string(const char* a, const char* b) { return std::strstr(a, b); }
inline const char* find_char(const char* a, int c) { return std::strchr(a, c); }
inline const char* find_last_char(const char* a, int c) { return std::strrchr(a, c); }
inline char* copy_string(char* d, const char* s) { return std::strcpy(d, s); }
inline char* copy_prefix(char* d, const char* s, size_t n) { return std::strncpy(d, s, n); }
inline char* append(char* d, const char* s) { return std::strcat(d, s); }
inline char* append_prefix(char* d, const char* s, size_t n) { return std::strncat(d, s, n); }
inline size_t span(const char* a, const char* b) { return std::strspn(a, b); }
inline size_t reject_span(const char* a, const char* b) { return std::strcspn(a, b); }
inline const char* find_any(const char* a, const char* b) { return std::strpbrk(a, b); }
inline size_t copy_bounded(char* d, const char* s, size_t n) {
    const size_t size = std::strlen(s);
    if (n) {
        const size_t count = size < n ? size : n - 1;
        std::memcpy(d, s, count);
        d[count] = 0;
    }
    return size;
}
} // namespace d1::pc_libc
