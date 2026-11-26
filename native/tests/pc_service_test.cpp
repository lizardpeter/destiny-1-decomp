#include "pc_file_service.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
using namespace d1::pc_io;
static void require(bool v) { if (!v) { std::fputs("native file service integration failed\n", stderr); std::abort(); } }
int main(int argc, char** argv) {
    require(argc == 2);
    namespace fs = std::filesystem;
    const fs::path root = argv[1];
    require(fs::create_directory(root));
    require(fs::create_directory(root / "assets") && fs::create_directory(root / "data"));
    std::error_code ec;
    {
        Service service(root / "assets", root / "data", 1, 1);
        require(!service.open(Root::assets, "forbidden", {Access::write, true}, ec) && ec == std::errc::read_only_file_system);
        auto id = service.open(Root::user_data, "file", {Access::read_write, true, false, true}, ec);
        require(id && !ec);
        const char value[] = "native";
        auto op = service.write_at(id, 0, value, sizeof value, {}, ec);
        require(op && !ec);
        require(service.wait(op).bytes == sizeof value);
        require(!service.write_at(id, 0, value, sizeof value, {}, ec) && ec == std::errc::resource_unavailable_try_again);
        require(service.forget(op));
        require(!service.write_at(id, UINT64_MAX, value, sizeof value, {}, ec) && ec == std::errc::invalid_argument);
        char result[sizeof value]{};
        std::atomic<bool> finished{false};
        auto read = service.read_at(id, 0, result, sizeof result, [&](Queue::Id callback_id, Result r) {
            Result polled;
            require(r && r.bytes == sizeof result && service.poll(callback_id, polled) && polled.bytes == r.bytes);
            require(service.forget(callback_id));
            finished.store(true, std::memory_order_release);
        }, ec);
        require(read && !ec && service.close(id));
        while (!finished.load(std::memory_order_acquire)) std::this_thread::yield();
        require(std::string(result) == value && !service.file(id));
        require(service.paths().remove_file("file", ec) && !ec);
    }
    require(fs::remove(root / "assets") && fs::remove(root / "data") && fs::remove(root));
    std::puts("native file service: immutable assets, queue limits, callback deletion, close during I/O passed");
}
