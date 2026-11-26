#include "query_reduce.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <vector>

using namespace d1::spv;
namespace {
void require(bool ok) { if (!ok) throw std::runtime_error("query reducer regression failed"); }

// Deliberately small, test-only evaluator of the integer/control-flow subset
// emitted by query_reduce_shader. Not a general SPIR-V or Vulkan validator.
struct Program {
    struct Inst { uint16_t op; std::vector<uint32_t> a; };
    std::vector<Inst> code;
    std::vector<size_t> label;
    std::vector<uint32_t> constants, storage, binding;
    uint32_t ids;
    size_t entry = 0;
    explicit Program(const std::vector<uint32_t>& words) : ids(words.at(3)) {
        require(words.size() >= 5 && words[0] == 0x07230203 && words[1] == 0x00010600 && ids < 1024);
        label.resize(ids, SIZE_MAX); constants.resize(ids); storage.resize(ids, UINT32_MAX); binding.resize(ids, UINT32_MAX);
        bool compute = false, size_one = false;
        for (size_t p = 5; p < words.size();) {
            const uint32_t n = words[p] >> 16;
            require(n && n <= words.size() - p);
            Inst ins{uint16_t(words[p]), {words.begin() + p + 1, words.begin() + p + n}};
            const auto& a = ins.a;
            switch (ins.op) {
            case OpEntryPoint: require(a.size() >= 3); compute = a[0] == ExecGLCompute; break;
            case OpExecutionMode: size_one = a.size() == 5 && a[1] == ModeLocalSize && a[2] == 1 && a[3] == 1 && a[4] == 1; break;
            case OpDecorate: if (a.size() == 3 && a[1] == DecBinding) binding.at(a[0]) = a[2]; break;
            case OpConstant: constants.at(a.at(1)) = a.at(2); break;
            case OpVariable: storage.at(a.at(1)) = a.at(2); break;
            case OpFunction: entry = code.size() + 1; break;
            case OpLabel: label.at(a.at(0)) = code.size(); break;
            default: break;
            }
            code.push_back(std::move(ins));
            p += n;
        }
        require(compute && size_one && entry);
    }
    void run(const std::vector<uint32_t>& source, std::vector<uint32_t>& dest,
             const std::array<uint32_t, 3>& push) const {
        struct Ptr { uint32_t root = 0, index = 0; };
        auto v = constants;
        std::vector<Ptr> ptr(ids);
        std::vector<uint32_t> local(ids);
        for (uint32_t i = 1; i < ids; ++i) if (storage[i] != UINT32_MAX) ptr[i] = {i, 0};
        auto read = [&](Ptr p) -> uint32_t {
            switch (storage.at(p.root)) {
            case SCFunction: return local.at(p.root);
            case SCPushConstant: return push.at(p.index);
            case SCStorageBuffer: require(binding.at(p.root) == 0); return source.at(p.index);
            default: throw std::runtime_error("unsupported pointer read");
            }
        };
        auto write = [&](Ptr p, uint32_t value) {
            switch (storage.at(p.root)) {
            case SCFunction: local.at(p.root) = value; return;
            case SCStorageBuffer: require(binding.at(p.root) == 1); dest.at(p.index) = value; return;
            default: throw std::runtime_error("unsupported pointer write");
            }
        };
        size_t pc = entry;
        for (size_t step = 0; step < 100000; ++step) {
            const auto& in = code.at(pc++);
            const auto& a = in.a;
            auto get = [&](size_t n) { return v.at(a.at(n)); };
            auto result = [&](uint32_t r) { v.at(a.at(1)) = r; };
            switch (in.op) {
            case OpLabel: case OpVariable: case OpLoopMerge: break;
            case OpAccessChain: {
                Ptr p = ptr.at(a.at(2));
                require(p.root && a.size() >= 4);
                if (storage.at(p.root) == SCStorageBuffer) require(a.size() == 5 && get(3) == 0);
                else require(a.size() == 4);
                p.index = get(a.size() - 1);
                ptr.at(a.at(1)) = p;
                break;
            }
            case OpLoad: result(read(ptr.at(a.at(2)))); break;
            case OpStore: write(ptr.at(a.at(0)), get(1)); break;
            case OpIAdd: result(get(2) + get(3)); break;
            case OpIMul: result(get(2) * get(3)); break;
            case OpBitwiseOr: result(get(2) | get(3)); break;
            case OpULessThan: result(get(2) < get(3)); break;
            case OpINotEqual: result(get(2) != get(3)); break;
            case OpSelect: result(get(2) ? get(3) : get(4)); break;
            case OpBranch: pc = label.at(a.at(0)); break;
            case OpBranchConditional: pc = label.at(a.at(get(0) ? 1 : 2)); break;
            case OpReturn: return;
            default: throw std::runtime_error("unsupported executable opcode");
            }
        }
        throw std::runtime_error("shader failed to terminate");
    }
};
}
int main(int argc, char** argv) {
    try {
        const auto words = d1::ng::query_reduce_shader();
        if (argc == 2) {
            FILE* f = std::fopen(argv[1], "wb"); require(f != nullptr);
            require(std::fwrite(words.data(), sizeof(uint32_t), words.size(), f) == words.size());
            require(std::fclose(f) == 0);
        }
        const Program p(words);
        std::mt19937_64 rng(0xD10133);
        uint32_t cases = 0;
        auto check = [&](const std::vector<uint32_t>& input, uint32_t first, uint32_t count, uint32_t slot) {
            std::vector<uint32_t> dest(32, 0xDEADBEEF);
            bool expected = false;
            for (uint32_t i = first; i < first + count; ++i) expected |= input.at(2*i) != 0 || input.at(2*i+1) != 0;
            p.run(input, dest, {count, slot, first});
            for (uint32_t i = 0; i < dest.size(); ++i) require(dest[i] == (i == slot ? uint32_t(expected) : 0xDEADBEEF));
            ++cases;
        };
        check({}, 0, 0, 0);
        check({0,0,0,0}, 0, 2, 1);
        check({0,1}, 0, 1, 2);  // low 32 bits zero; high word must not be ignored
        check({0,0,0,1,0,0}, 2, 1, 3); // adjacent nonzero query outside the span
        check({0,0,0,1,0,0}, 1, 1, 4);
        for (uint32_t t = 0; t < 10000; ++t) {
            const uint32_t n = 1 + rng() % 64;
            std::vector<uint32_t> input(n * 2, 0);
            if (t % 4) for (auto& word : input) if (rng() % 13 == 0) word = uint32_t(rng());
            const uint32_t first = rng() % (n + 1), count = rng() % (n - first + 1);
            check(input, first, count, rng() % 32);
        }
        std::printf("%u emitted-shader interpreter cases passed; module %zu words\n", cases, words.size());
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
