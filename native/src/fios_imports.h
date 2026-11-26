#pragma once
#include <string>

namespace d1 {
// Temporary retail import boundary. Native code itself uses pc_io::Service.
// Default off until verified against the original game and Windows builds.
bool native_fios_enabled();
void native_fios_prepare();
void* native_fios_import(const std::string& nid);
} // namespace d1
