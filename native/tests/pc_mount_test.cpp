#include "pc_mount_adapter.h"
#include <cstdio>
#include <cstdlib>
using namespace d1::pc_io;
static void require(bool v) { if (!v) { std::fputs("native mount adapter regression failed\n", stderr); std::abort(); } }
int main() {
    std::error_code ec;
    auto check = [&](const char* path, Root root, const char* relative, const char* mount) {
        auto p = decode_retail_path(path, ec);
        require(p && !ec && p->root == root && p->relative == relative && p->mount == mount);
    };
    check("/app0", Root::assets, "", "");
    check("/app0//packages/file", Root::assets, "packages/file", "");
    check("/app00/a", Root::user_data, "unmapped/app00/a", "");
    check("/temp0/save", Root::user_data, "temp0/save", "temp0");
    check("/temp", Root::user_data, "temp", "temp");
    check("/temp123", Root::user_data, "unmapped/temp123", "");
    check("/download0/x", Root::user_data, "download0/x", "download0");
    check("/data/x", Root::user_data, "data/x", "data");
    check("/savedata0/x", Root::user_data, "savedata0/x", "savedata0");
    check("/hostapp/x", Root::user_data, "hostapp/x", "hostapp");
    check("foo/bar", Root::user_data, "unmapped/foo/bar", "");
    require(!decode_retail_path(nullptr, ec) && ec);
    require(!decode_retail_path("", ec) && ec);
    require(!decode_retail_path("/data/C:/escape", ec) && ec);
    require(!decode_retail_path("/data/..\\escape", ec) && ec);
    std::puts("native retail path boundary and mount translation checks passed");
}
