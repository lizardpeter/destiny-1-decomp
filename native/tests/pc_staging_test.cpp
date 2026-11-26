#include "pc_file_queue.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <stdexcept>
using namespace d1::pc_io;
static void require(bool ok) { if (!ok) { std::fputs("native staging regression failed\n", stderr); std::abort(); } }
int main(int argc, char** argv) {
    require(argc == 2);
    ByteBudget budget(64);
    require(!budget.reserve(65));
    {
        auto a = budget.reserve(64);
        require(bool(a) && budget.used() == 64 && !budget.reserve(1));
        auto b = budget.reserve(0);
        require(bool(b));
        b = std::move(a);
        require(!a && budget.used() == 64);
    }
    require(budget.used() == 0);
    std::atomic<unsigned> cycles{0};
    std::array<std::thread, 4> threads;
    for (auto& t : threads) t = std::thread([&] {
        for (unsigned i = 0; i < 25000; ++i) {
            auto lease = budget.reserve(16);
            require(bool(lease) && budget.used() <= 64);
            ++cycles;
        }
    });
    for (auto& t : threads) t.join();
    require(cycles == 100000 && budget.used() == 0);
    std::error_code ec;
    const std::filesystem::path path = argv[1];
    {
        Queue queue(2, 8, 64);
        auto file = queue.open(path, {Access::read_write, true, false, true}, ec);
        require(file && !ec);
        std::array<unsigned char, 64> payload{};
        for (unsigned i = 0; i < payload.size(); ++i) payload[i] = static_cast<unsigned char>(i);
        require(queue.get(file)->write_at(0, payload.data(), payload.size()).bytes == payload.size());
        std::latch entered(1), release(1), callback_done(1);
        auto op = queue.read_staged(file, 0, 64, [&](auto bytes, Result r) {
            require(r && r.bytes == 64 && std::equal(bytes.begin(), bytes.end(), payload.begin()));
            entered.count_down(); release.wait();
            return Result{0, error(std::errc::operation_canceled)};
        }, [&](Queue::Id id, Result r) {
            Result polled;
            require(!r && r.error == std::errc::operation_canceled && r.bytes == 0);
            require(queue.poll(id, polled) && polled.error == r.error && queue.forget(id));
            callback_done.count_down();
        }, ec);
        require(op && !ec);
        entered.wait();
        require(queue.staging_bytes() == 64);
        require(!queue.write_at(file, 0, payload.data(), 1, {}, ec) && ec == std::errc::resource_unavailable_try_again);
        require(!queue.read_staged(file, 0, 1, [](auto, Result r){ return r; }, {}, ec) && ec == std::errc::resource_unavailable_try_again);
        Result pending;
        require(!queue.poll(op, pending) && !queue.forget(op));
        release.count_down(); callback_done.wait();
        require(queue.staging_bytes() == 0);
        require(!queue.write_at(0, 0, payload.data(), 64, {}, ec) && queue.staging_bytes() == 0);
        auto w = queue.write_at(file, 0, payload.data(), 64, {}, ec);
        require(w && queue.wait(w).bytes == 64 && queue.forget(w));
        for (unsigned i = 0; i < 1000; ++i) {
            auto read = queue.read_staged(file, i % 65, 64, [&](auto bytes, Result r) {
                require(r && r.bytes == 64 - i % 65 && bytes.size() == r.bytes);
                require(std::equal(bytes.begin(), bytes.end(), payload.begin() + i % 65));
                return r;
            }, {}, ec);
            require(read && queue.wait(read).bytes == 64 - i % 65 && queue.forget(read));
        }
        auto fail = queue.read_staged(file, 0, 64, [](auto, Result) -> Result { throw std::runtime_error("delivery"); }, {}, ec);
        require(fail && queue.wait(fail).error == std::errc::io_error && queue.forget(fail));
        require(queue.staging_bytes() == 0 && queue.close(file));
    }
    require(std::filesystem::remove(path));
    std::puts("100000 budget cycles, 1000 staged reads, backpressure and rejected delivery checks passed");
}
