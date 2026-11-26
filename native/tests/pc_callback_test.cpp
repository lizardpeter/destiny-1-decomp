#include "pc_file_queue.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <latch>
using namespace d1::pc_io;
static void require(bool v) { if (!v) { std::fputs("native callback regression failed\n", stderr); std::abort(); } }
int main(int argc, char** argv) {
    require(argc == 2);
    std::error_code ec;
    const std::filesystem::path path = argv[1];
    std::atomic<unsigned> drained{0};
    {
        Queue q(1, 2048, 4096);
        auto id = q.open(path, {Access::read_write, true, false, true}, ec);
        require(id && !ec);
        const unsigned char value = 42;
        require(q.get(id)->write_at(0, &value, 1).bytes == 1);
        std::thread::id io_thread, callback_thread;
        std::latch done(1);
        auto first = q.read_staged(id, 0, 1, [&](auto bytes, Result r) {
            require(r && bytes.size() == 1 && bytes[0] == value);
            io_thread = std::this_thread::get_id();
            return r;
        }, [&](Queue::Id op, Result r) {
            require(r && r.bytes == 1 && q.forget(op));
            callback_thread = std::this_thread::get_id();
            require(callback_thread != io_thread);
            unsigned char data = 0;
            std::error_code local;
            auto nested = q.read_at(id, 0, &data, 1, {}, local);
            require(nested && !local && q.wait(nested).bytes == 1 && data == value && q.forget(nested));
            done.count_down();
        }, ec);
        require(first && !ec);
        done.wait();
        for (unsigned i = 0; i < 1000; ++i) {
            auto op = q.read_staged(id, 0, 1, [](auto bytes, Result r) {
                require(r && bytes.size() == 1 && bytes[0] == 42); return r;
            }, [&, expected = callback_thread](Queue::Id op_id, Result r) {
                require(std::this_thread::get_id() == expected && r.bytes == 1 && q.forget(op_id));
                ++drained;
            }, ec);
            require(op && !ec);
        }
        // Close may race queued reads; accepted operations retain the file.
        require(q.close(id));
        // Destruction must drain both workers and callback dispatcher.
    }
    require(drained == 1000);
    require(std::filesystem::remove(path));
    std::puts("dedicated callback thread, nested I/O, and 1000 shutdown-drained callbacks passed");
}
