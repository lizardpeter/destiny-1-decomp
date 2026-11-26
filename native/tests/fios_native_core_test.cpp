#include "fios_native_core.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <set>

using namespace d1::fios_native;
namespace fs = std::filesystem;
static void check(bool b) { if (!b) { std::fputs("native FIOS core regression failed\n", stderr); std::abort(); } }
int main(int argc, char** argv) {
    check(argc == 2);
    const fs::path root = argv[1];
    check(fs::create_directory(root));
    check(fs::create_directory(root / "assets") && fs::create_directory(root / "data"));
    {
        std::error_code ec;
        auto f = d1::pc_io::File::open(root / "assets" / "sample", {d1::pc_io::Access::write, true, false, true}, ec);
        check(f && f->write("asset", 5).bytes == 5);
    }
    {
        Core core(root / "assets", root / "data", 2, 256, 4096);
        uint32_t file = 0, dir = 0, asset = 0;
        check(core.open("/app0/sample", 3 | 8, -1, file) != 0 && !file);
        check(core.open("/app0/sample", 1, -1, asset) == 0 && core.valid(asset));
        char a[8]{};
        check(core.read(asset, a, sizeof a) == 5 && std::string(a) == "asset");
        check(core.close(asset) == 0 && !core.valid(asset));
        std::error_code ec;
        const auto mkdir = core.create_directory("/data/test", {}, ec);
        check(mkdir && !ec && core.wait(mkdir) && core.delete_operation(mkdir));
        check(!core.valid(mkdir));
        check(core.open("/data/test/file", 3 | 8 | 16, 0600, file) == 0 && core.valid(file));
        check(core.write(file, "hello", 5) == 5 && core.size(file) == 5 && core.tell(file) == 5);
        check(core.seek(file, -2, 2) == 3 && core.read(file, a, 3) == 2);
        check(a[0] == 'l' && a[1] == 'o' && core.sync(file) == 0);
        check(core.seek(file, -100, 0) == -1);
        d1::fios_contract::Stat st{};
        check(core.stat("/data/test/file", st) == 0 && st.size == 5);
        check(core.open_directory("/data/test", dir) == 0 && core.valid(dir));
        check(dir != file && core.size(dir) == -1 && core.close(dir) != 0);
        d1::fios_contract::DirectoryEntry entry{};
        check(core.read_directory(dir, entry) == 0 && std::string(entry.path) == "/data/test/file");
        check(entry.name_length == 4 && entry.size == 5 && core.read_directory(dir, entry) == 0x80820010);
        check(core.close_directory(dir) == 0 && !core.valid(dir));
        const auto w = core.write_at(file, 8, "bytes", 5, {}, ec);
        check(w && !ec && core.wait(w).bytes == 5 && core.delete_operation(w));
        check(core.size(file) == 13 && core.tell(file) == 5);
        for (unsigned i = 0; i < 1000; ++i) {
            std::latch entered(1), release(1);
            std::atomic<bool> seen{false};
            std::array<unsigned char, 8> target{};
            auto op = core.read_at(file, 8, target.size(), [&](auto bytes, Result r) {
                check(r && bytes.size() == 5);
                std::copy(bytes.begin(), bytes.end(), target.begin());
                return r;
            }, [&](auto id, Result r) {
                entered.count_down(); release.wait();
                Result observed;
                check(r.bytes == 5 && core.poll(id, observed) && observed.bytes == 5);
                check(core.delete_operation(id));
                seen.store(true);
            }, ec);
            check(op && !ec);
            entered.wait();
            check(core.delete_operation(op) && core.valid(op));
            release.count_down();
            core.drain();
            check(seen.load() && !core.valid(op) && std::string(reinterpret_cast<char*>(target.data())) == "bytes");
        }
        check(core.close(file) == 0 && !core.valid(file));
        check(core.open("/data/test/file", 2 | 4, -1, file) == 0);
        check(core.write(file, "!", 1) == 1 && core.size(file) == 14 && core.tell(file) == 14);
        const auto rejected = core.write_at(file, 0, "x", 1, {}, ec);
        check(rejected && core.wait(rejected).error == std::errc::operation_not_supported && core.delete_operation(rejected));
        check(core.close(file) == 0);
        check(core.rename("/data/test/file", "/data/test/renamed") == 0);
        check(core.remove("/data/test/renamed", Core::DeleteKind::file) == 0);
        check(core.remove("/data/test", Core::DeleteKind::directory) == 0);
        check(core.remove("/app0/sample", Core::DeleteKind::any) != 0);
        check(core.stat("/app0/sample", st) == 0 && st.size == 5);
    }
    check(fs::remove(root / "assets" / "sample") && fs::remove(root / "assets"));
    check(fs::remove(root / "data" / "data") && fs::remove(root / "data") && fs::remove(root));
    std::puts("native FIOS core: sync/async I/O, directories, stat, append, handle classes, 1000 early deletions passed");
}
