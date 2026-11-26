#include "native_list_tokens.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace d1;
static void require(bool value) {
    if (!value) { std::fputs("native list token regression failed\n", stderr); std::exit(1); }
}
int main() {
    std::mt19937_64 rng(0xD10133);
    std::vector<uint32_t> words;
    std::vector<uint64_t> expected, actual;
    uint64_t checked = 0;
    auto decode = [&](const uint32_t* p, uint32_t n) {
        actual.clear();
        return visit_native_list_tokens(p, n, [&](uint64_t id) { actual.push_back(id); });
    };
    require(decode(nullptr, 0) && actual.empty());
    require(!decode(nullptr, 1));
    for (unsigned run = 0; run < 10000; ++run) {
        words.clear(); expected.clear();
        for (unsigned k = 0; k < 64; ++k) {
            switch (rng() % 4) {
            case 0: {
                const uint64_t id = rng();
                words.insert(words.end(), {0xC0021000u, kNativeListMagic, uint32_t(id), uint32_t(id >> 32)});
                expected.push_back(id);
                break;
            }
            case 1: words.push_back(0x80000000u); break;
            case 2: words.insert(words.end(), {0xC0021000u, 0u, 0xC0021000u, kNativeListMagic}); break;
            case 3: words.insert(words.end(), {0xC0032000u, kNativeListMagic, 0xC0021000u, kNativeListMagic, 0u}); break;
            }
        }
        require(decode(words.data(), uint32_t(words.size())) && actual == expected);
        ++checked;
        // A truncated token may not expose either its ID or an out-of-bounds word.
        for (uint32_t tail = 1; tail < 4; ++tail) {
            std::vector<uint32_t> truncated = words;
            const uint32_t token[] = {0xC0021000u, kNativeListMagic, 123u, 456u};
            truncated.insert(truncated.end(), token, token + tail);
            require(!decode(truncated.data(), uint32_t(truncated.size())) && actual == expected);
            ++checked;
        }
    }
    std::printf("%llu native token cases passed\n", (unsigned long long)checked);
}
