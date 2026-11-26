#include "fios_binary_contract.h"
#include "pc_file_queue.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace d1;
static void require(bool condition) { if (!condition) std::abort(); }
int main() {
    using namespace fios_contract;
    constexpr uint32_t access[] = {0, 0, 1, 2};
    for (uint32_t flags = 0; flags < 65536; ++flags) {
        uint32_t expected = access[flags % 4];
        if (flags & 4) expected |= 8;
        if (flags & 8) expected |= 0x200;
        if (flags & 16) expected |= 0x400;
        if (flags & 4096) expected |= 0x10000;
        require(to_kernel_open_flags(flags) == expected);
    }
    auto s = make_stat(123, 17, 29, 0100644);
    require(s.size == 123 && s.access_ns == 17000000000ull && s.modification_ns == 29000000000ull);
    require(s.attributes == 6 && s.mode == 0100644 && s.unknown18 == 0);
    require(attribute_bits(0040700) == 7);
    DirectoryEntry e{};
    require(make_directory_entry(e, "/data/hello", "hello", 42, 0100400));
    require(e.name_offset == 6 && e.name_length == 5 && e.path_length == 11 && e.attributes == 2);
    require(std::string(e.path) == "/data/hello");
    require(!make_directory_entry(e, "/data/hello", "ello", 42, 0));
    require(!make_directory_entry(e, std::string(1024, 'a'), "a", 0, 0));
    require(from_encoded_kernel_status(0x80020002) == 0x80820005);
    require(from_encoded_kernel_status(0x8002000d) == 0x80820013);
    require(from_encoded_kernel_status(0x800200ff) == 0x800200ff);
    pc_io::Queue q(2, 256, 1024);
    std::atomic<unsigned> callbacks{0};
    std::error_code ec;
    for (unsigned i = 0; i < 100; ++i) {
        auto id = q.submit_task([i] { return pc_io::Result{i, {}}; },
            [&](auto id, auto result) {
                require(result && q.has_operation(id) && q.forget(id));
                ++callbacks;
            }, ec);
        require(id && !ec);
    }
    q.drain();
    require(callbacks == 100);
    auto id = q.submit_task([]() -> pc_io::Result { throw 1; }, {}, ec);
    require(id && !ec && q.wait(id).error == std::errc::io_error && q.forget(id));
    q.drain();
    std::cout << "65536 flag cases, binary layouts, and 100 asynchronous task callbacks passed\n";
}
