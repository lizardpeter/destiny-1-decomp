#pragma once
#include "pc_paths.h"
#include <string_view>

namespace d1::pc_io {
// Only the unreconstructed retail path callers need this name translation.
// Native systems pass Root and a relative path directly to Paths.
struct RetailPath {
    Root root = Root::user_data;
    std::string relative;
    std::string mount;
};
inline std::optional<RetailPath> decode_retail_path(const char* path, std::error_code& ec) {
    ec.clear();
    if (!path || !*path) { ec = error(std::errc::invalid_argument); return {}; }
    const std::string_view s(path);
    if (s.find('\\') != s.npos || s.find(':') != s.npos) {
        ec = error(std::errc::invalid_argument); return {};
    }
    const auto matches = [&](std::string_view mount) {
        return s == mount || (s.size() > mount.size() && s.starts_with(mount) && s[mount.size()] == '/');
    };
    auto tail = [&](size_t n) {
        while (n < s.size() && s[n] == '/') ++n;
        return std::string(s.substr(n));
    };
    if (matches("/app0")) return RetailPath{Root::assets, tail(5), {}};
    for (const std::string_view m : {"/temp0", "/temp", "/download0", "/data", "/savedata0", "/hostapp"}) {
        if (!matches(m)) continue;
        auto mount = std::string(m.substr(1));
        auto relative = tail(m.size());
        return RetailPath{Root::user_data, mount + (relative.empty() ? "" : "/" + relative), mount};
    }
    const auto rest = tail(0);
    return RetailPath{Root::user_data, "unmapped" + (rest.empty() ? "" : "/" + rest), {}};
}
} // namespace d1::pc_io
