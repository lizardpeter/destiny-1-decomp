#include "pc_libc_leaf.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

using namespace d1::pc_libc;
static void require(bool v) {
    if (!v) { std::fputs("PC libc leaf regression failed\n", stderr); std::exit(1); }
}
int main() {
    std::mt19937_64 rng(0xD10133);
    for (unsigned t = 0; t < 100000; ++t) {
        std::array<unsigned char, 128> a{}, b{}, expected{};
        for (auto& c : a) c = static_cast<unsigned char>(rng());
        b = expected = a;
        const size_t src = rng() % 64, dst = rng() % 64, n = rng() % 65;
        require(move(b.data() + dst, b.data() + src, n) == b.data() + dst);
        for (size_t i = 0; i < n; ++i) expected[dst + i] = a[src + i];
        require(b == expected);
        require(copy(b.data(), a.data(), a.size()) == b.data() && b == a);
        const int byte = rng() % 256;
        require(fill(b.data() + dst, byte, n) == b.data() + dst);
        for (size_t i = 0; i < n; ++i) require(b[dst+i] == byte);
        const void* found = nullptr;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] == byte) { found = &a[i]; break; }
        require(find_byte(a.data(), byte, a.size()) == found);
        std::string str;
        for (size_t i = rng() % 64; i; --i) str += char('a' + rng() % 26);
        std::array<char, 80> buf;
        buf.fill('#');
        const size_t cap = rng() % buf.size();
        require(copy_bounded(buf.data(), str.c_str(), cap) == str.size());
        if (cap) {
            const size_t used = std::min(str.size(), cap - 1);
            require(std::string(buf.data()) == str.substr(0, used));
            for (size_t i = used + 1; i < buf.size(); ++i) require(buf[i] == '#');
        } else for (char c : buf) require(c == '#');
        require(length(str.c_str()) == str.size());
        require(compare_string(str.c_str(), str.c_str()) == 0);
        require(compare_prefix(str.c_str(), "other", 0) == 0);
    }
    char data[32]{};
    require(copy_string(data, "abc") == data);
    require(append(data, "de") == data);
    require(append_prefix(data, "fgzz", 2) == data && compare_string(data, "abcdefg") == 0);
    require(find_string(data, "cd") == data + 2);
    require(find_char(data, 'b') == data + 1);
    require(find_last_char(data, 'g') == data + 6);
    require(find_char(data, 0) == data + 7);
    require(find_any(data, "zde") == data + 3);
    require(span(data, "abc") == 3);
    require(reject_span(data, "de") == 3);
    require(compare("a", "b", 1) < 0);
    require(copy_prefix(data, "a", 4) == data && data[1] == 0 && data[3] == 0);
    require(copy_bounded(nullptr, "abc", 0) == 3);
    std::puts("100000 randomized PC libc leaf cases and boundary checks passed");
}
