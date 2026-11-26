#include "pc_file_queue.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
using namespace d1::pc_io;
static void require(bool b) { if (!b) { std::fputs("native file service regression failed\n", stderr); std::abort(); } }
int main(int argc, char** argv) {
    require(argc == 2);
    const std::filesystem::path path = argv[1];
    // Exclusive creation below refuses to overwrite an existing test path.
    std::error_code ec;
    {
        Queue q(4, 128);
        auto id = q.open(path, {Access::read_write, true, false, true}, ec);
        require(id && !ec);
        require(!q.open(path, {Access::read_write, true, false, true}, ec) && ec);
        auto f = q.get(id);
        require(bool(f));
        std::mt19937_64 rng(0xD10133);
        std::vector<unsigned char> model(65536, 0), actual(65536);
        require(f->write_at(0, model.data(), model.size()).bytes == model.size());
        for (unsigned i = 0; i < 10000; ++i) {
            std::array<unsigned char, 128> bytes{};
            for (auto& b : bytes) b = static_cast<unsigned char>(rng());
            const size_t n = rng() % (bytes.size() + 1), offset = rng() % (model.size() - bytes.size());
            const auto w = f->write_at(offset, bytes.data(), n);
            require(w && w.bytes == n);
            std::copy_n(bytes.begin(), n, model.begin() + offset);
            const auto r = f->read_at(offset, actual.data(), n);
            require(r && r.bytes == n && std::equal(actual.begin(), actual.begin() + n, model.begin() + offset));
        }
        require(f->tell() == 0);
        require(f->seek(-16, File::Origin::end).bytes == model.size() - 16);
        require(f->read(actual.data(), 128).bytes == 16);
        require(f->tell() == model.size());
        require(f->read(actual.data(), 1).bytes == 0);
        require(!f->seek(INT64_MIN, File::Origin::current));
        require(!f->read_at(UINT64_MAX, actual.data(), 1));
        require(!f->write_at(0, nullptr, 1));
        require(f->seek(0, File::Origin::begin).bytes == 0);
        require(f->read(actual.data(), actual.size()).bytes == actual.size() && actual == model);
        // Verify 64-bit positions without allocating a huge non-sparse Windows file.
        constexpr uint64_t large = (1ull << 32) + 17;
        require(f->seek(large, File::Origin::begin).bytes == large);
        require(f->tell() == large);
#ifndef _WIN32
        unsigned char v = 73, out = 0;
        require(f->write_at(large, &v, 1).bytes == 1);
        require(f->size().bytes == large + 1);
        require(f->read_at(large, &out, 1).bytes == 1 && out == v);
#endif
        require(f->seek(0, File::Origin::begin).bytes == 0);
        require(!f->sync());
        // Each async write owns its own snapshot; each job uses disjoint offsets.
        std::atomic<unsigned> callbacks{0};
        for (unsigned batch = 0; batch < 100; ++batch) {
            std::array<Queue::Id, 64> ops{};
            std::array<std::array<unsigned char, 32>, 64> outputs{};
            for (unsigned i = 0; i < ops.size(); ++i) {
                std::array<unsigned char, 32> input{};
                input.fill(static_cast<unsigned char>(batch + i));
                ops[i] = q.write_at(id, i * input.size(), input.data(), input.size(), {}, ec);
                require(ops[i] && !ec);
                input.fill(0xff);
            }
            for (auto op : ops) { const auto r = q.wait(op); require(r && r.bytes == 32 && q.forget(op)); }
            for (unsigned i = 0; i < ops.size(); ++i) {
                ops[i] = q.read_at(id, i * outputs[i].size(), outputs[i].data(), outputs[i].size(),
                    [&](Queue::Id, Result r) { require(r && r.bytes == 32); ++callbacks; }, ec);
                require(ops[i] && !ec);
            }
            for (unsigned i = 0; i < ops.size(); ++i) {
                auto r = q.wait(ops[i]); require(r && r.bytes == 32 && q.forget(ops[i]));
                require(std::all_of(outputs[i].begin(), outputs[i].end(), [&](auto b) { return b == static_cast<unsigned char>(batch+i); }));
            }
        }
        while (callbacks != 6400) std::this_thread::yield();
        std::array<unsigned char, 32> buffer{};
        const auto op = q.read_at(id, 0, buffer.data(), buffer.size(), {}, ec);
        require(op && q.close(id) && !q.get(id));
        require(q.wait(op).bytes == buffer.size() && q.forget(op));
        require(!q.read_at(id, 0, buffer.data(), buffer.size(), {}, ec) && ec);
        auto ro = q.open(path, {Access::read}, ec);
        require(ro && ro != id && !ec);
        require(!q.get(ro)->write_at(0, buffer.data(), 1));
        require(q.close(ro));
    }
    std::filesystem::remove(path);
    std::puts("10000 positional I/O pairs, 12800 asynchronous operations, sparse-file and lifetime checks passed");
}
