#include "pc_paths.h"
#include <cstdio>
#include <cstdlib>
#include <set>
using namespace d1::pc_io;
static void require(bool v) { if (!v) { std::fputs("native filesystem namespace regression failed\n", stderr); std::abort(); } }
int main(int argc, char** argv) {
    require(argc == 2);
    namespace fs = std::filesystem;
    const fs::path root = argv[1];
    std::error_code ec;
    // The caller supplies a fresh private test directory. Never recursively remove
    // any existing directory supplied to the test executable.
    require(fs::create_directory(root, ec) && !ec);
    require(fs::create_directory(root / "assets"));
    require(fs::create_directory(root / "data"));
    {
        auto asset = File::open(root / "assets" / "original", {Access::write, true, true}, ec);
        require(asset && asset->write("test", 4).bytes == 4);
    }
    Paths paths(root / "assets", root / "data");
    require(paths.stat(Root::assets, "original", ec)->size == 4 && !ec);
    require(!paths.open(Root::assets, "original", {Access::write}, ec) && ec == std::errc::read_only_file_system);
    require(!paths.open(Root::user_data, "../assets/original", {Access::write}, ec) && ec);
    require(paths.resolve(Root::user_data, root / "assets" / "original", false, ec).empty() && ec);
    require(paths.resolve(Root::user_data, "", true, ec).empty() && ec);
    require(paths.create_directory("saves", ec) && !ec);
    for (unsigned i = 0; i < 128; ++i) {
        const auto name = fs::path("saves") / std::to_string(i);
        auto f = paths.open(Root::user_data, name, {Access::read_write, true, false, true}, ec);
        require(f && !ec && f->write(&i, sizeof i).bytes == sizeof i);
        require(paths.stat(Root::user_data, name, ec)->size == sizeof i && !ec);
    }
    std::set<std::string> entries;
    for (auto it = paths.list(Root::user_data, "saves", ec); it != fs::directory_iterator(); it.increment(ec)) {
        require(!ec); entries.insert(it->path().filename().string());
    }
    require(!ec && entries.size() == 128);
    require(!paths.remove_file("saves", ec) && ec == std::errc::is_a_directory);
    require(!paths.remove_directory("saves", ec) && ec);
    require(!paths.remove_directory("saves/0", ec) && ec == std::errc::not_a_directory);
#ifndef _WIN32
    fs::create_directory_symlink(fs::canonical(root / "assets"), root / "data" / "escape", ec);
    require(!ec);
    require(paths.resolve(Root::user_data, "escape/original", true, ec).empty() && ec);
    require(paths.resolve(Root::user_data, "escape/original", false, ec).empty() && ec);
    require(fs::remove(root / "data" / "escape"));
#endif
    for (unsigned i = 0; i < 128; ++i) require(paths.remove_file(fs::path("saves") / std::to_string(i), ec) && !ec);
    require(paths.remove_directory("saves", ec) && !ec);
    require(paths.stat(Root::assets, "original", ec)->size == 4 && !ec);
    require(fs::remove(root / "assets" / "original"));
    require(fs::remove(root / "assets") && fs::remove(root / "data") && fs::remove(root));
    std::puts("native filesystem namespace: 128 files, listing, deletion and containment checks passed");
}
