// Link the production FIOS import adapter and the exact callback entry against
// isolated host-side runtime/mapping doubles. No retail executable is loaded.
#include "fios_imports.h"
#include "fios_contract.h"
#include "fios_binary_contract.h"
#include "runtime.h"
#include "host_memory.h"
#include "nid.h"
#include "guest_call4_asm.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>

asm(R"(
    .data
    .balign 16
    .globl d1_root_frame
d1_root_frame:
    .quad d1_root_frame
    .quad 0
)");

namespace {
std::array<unsigned char, 32> target{};
std::atomic<bool> reject_delivery{false};
std::atomic<unsigned> callback_count{0}, tls_count{0};
std::atomic<uint32_t> expected_status{0};
std::atomic<uint64_t> expected_count{0};
static void check(bool ok) { if (!ok) { std::fputs("native FIOS import regression failed\n", stderr); std::abort(); } }
template<class Fn> Fn entry(const char* name) {
    auto p = d1::native_fios_import(d1::nid_of(name));
    check(p != nullptr);
    return reinterpret_cast<Fn>(p);
}
extern "C" D1_SYSV uint64_t terminal_callback(uint64_t context, uint64_t op, uint64_t event, uint64_t status) {
    check(context == 0x12345678abcdef09ull && event == 1 && status == expected_status.load());
    const auto count = entry<uint64_t(D1_SYSV *)(uint32_t)>("sceFiosOpGetActualCount");
    const auto erase = entry<void(D1_SYSV *)(uint32_t)>("sceFiosOpDelete");
    check(count(uint32_t(op)) == expected_count.load());
    erase(uint32_t(op));
    ++callback_count;
    return 0;
}
}

namespace d1 {
Runtime& rt() { static Runtime r; return r; }
struct Thread {};
Thread* ensure_self(const char*) { static Thread t; return &t; }
void register_guest_thread(const char*) {}
void* tls_create_thread_block() { ++tls_count; return nullptr; }
void tls_set_current(void*) {}
void log(const char*, ...) {}
namespace os { void set_thread_name(const char*) {} }
uint64_t guest_mapping_identity(uint64_t addr, uint64_t n) {
    const auto base = reinterpret_cast<uint64_t>(target.data());
    return addr >= base && addr - base <= target.size() && n <= target.size() - (addr - base) ? 7 : 0;
}
bool guest_write_current_mapping(uint64_t addr, uint64_t n, uint64_t id, void(*fn)(void*, void*), void* context) {
    if (reject_delivery || id != 7 || guest_mapping_identity(addr, n) != id) return false;
    fn(reinterpret_cast<void*>(addr), context);
    return true;
}
}

int main(int argc, char** argv) {
    check(argc == 2);
    namespace fs = std::filesystem;
    using namespace d1::fios_contract;
    const fs::path root = argv[1];
    check(fs::create_directory(root) && fs::create_directory(root / "assets") && fs::create_directory(root / "data"));
    check(::setenv("D1_NATIVE_FIOS", "1", 1) == 0);
    d1::rt().app0 = (root / "assets").string();
    d1::rt().data_root = (root / "data").string();
    d1::Module m;
    m.text_lo = reinterpret_cast<uint64_t>(&terminal_callback);
    m.text_hi = m.text_lo + 1;
    d1::rt().modules.push_back(m);
    d1::native_fios_prepare();
    const std::pair<const char*, const char*> symbols[] = {
        {"sceFiosInitialize","wAKZ-det+yo"},{"sceFiosTerminate","3HAgZPl1v+4"},
        {"sceFiosFHOpenSync","b44anV2D7K0"},{"sceFiosFHOpenWithModeSync","w13Ojm7ON9o"},
        {"sceFiosFHCloseSync","AOujSGqU+ms"},{"sceFiosFHReadSync","Bn2ZF4ZjeuQ"},
        {"sceFiosFHWriteSync","Kl-TbrDU9YM"},{"sceFiosFHPread","rR8wq7YFRZs"},
        {"sceFiosFHPwrite","PbxGVfOvUQY"},{"sceFiosOpGetActualCount","+FRvKknUj1I"},
        {"sceFiosOpDelete","5cyEcilO-J0"},{"sceFiosFHGetSize","FdjoqFQOlt0"},
        {"sceFiosFHTell","MrRFrdgpsx8"},{"sceFiosFHSeek","xReSebwKApA"},
        {"sceFiosFHSyncSync","EzzSJz6yuMc"},{"sceFiosIsValidHandle","8IGjwtnvYwI"},
        {"sceFiosStatSync","jayvY07C5dk"},{"sceFiosDHOpenSync","GGqucL9F+YI"},
        {"sceFiosDHReadSync","odjOGg8harg"},{"sceFiosDHCloseSync","0-p4O8FINmU"},
        {"sceFiosDirectoryCreate","-ULUBK21QgE"},{"sceFiosDeleteSync","KsVTc04kN9k"},
        {"sceFiosFileDeleteSync","bDupEgbQ6Fk"},{"sceFiosDirectoryDeleteSync","n5TUlwdhGSA"},
        {"sceFiosRenameSync","G-39lsdSgXo"}
    };
    static_assert(std::size(symbols) == 25);
    for (auto [name,nid] : symbols) check(d1::nid_of(name) == nid && d1::native_fios_import(nid));
    check(!d1::native_fios_import("missing-nid"));
    auto init = entry<uint32_t(D1_SYSV *)(const void*)>("sceFiosInitialize");
    uint32_t config = 0x1000130;
    check(init(&config) == 0);
    auto open = entry<uint32_t(D1_SYSV *)(const OperationAttributes*, uint32_t*, const char*, const OpenAttributes*)>("sceFiosFHOpenSync");
    auto close = entry<uint32_t(D1_SYSV *)(const OperationAttributes*, uint32_t)>("sceFiosFHCloseSync");
    auto write = entry<int64_t(D1_SYSV *)(const OperationAttributes*, uint32_t, const void*, int64_t)>("sceFiosFHWriteSync");
    auto pread = entry<uint32_t(D1_SYSV *)(const OperationAttributes*, uint32_t, void*, int64_t, int64_t)>("sceFiosFHPread");
    auto pwrite = entry<uint32_t(D1_SYSV *)(const OperationAttributes*, uint32_t, const void*, int64_t, int64_t)>("sceFiosFHPwrite");
    auto terminate = entry<void(D1_SYSV *)()>("sceFiosTerminate");
    auto erase_file = entry<uint32_t(D1_SYSV *)(const OperationAttributes*, const char*)>("sceFiosFileDeleteSync");
    OpenAttributes options{};
    options.flags = 3 | 8 | 16;
    uint32_t file = 0;
    check(open(nullptr, &file, "/data/file", &options) == 0 && file);
    check(write(nullptr, file, "hello", 5) == 5);
    OperationAttributes attr{};
    attr.callback = reinterpret_cast<uint64_t>(&terminal_callback);
    attr.user_context = 0x12345678abcdef09ull;
    expected_count = 5;
    expected_status = kObservedReadStatus;
    check(pread(&attr, file, target.data(), 8, 0) != 0);
    terminate();
    check(callback_count == 1 && tls_count == 1 && std::string(reinterpret_cast<char*>(target.data())) == "hello");
    // Delivery rejection exercises the actual adapter's mapping identity boundary.
    target.fill(0);
    reject_delivery = true;
    expected_count = 0;
    expected_status = 0x80020055;
    check(pread(&attr, file, target.data(), 5, 0) != 0);
    terminate();
    check(callback_count == 2 && target[0] == 0);
    reject_delivery = false;
    expected_status = 0;
    expected_count = 3;
    check(pwrite(&attr, file, "xyz", 3, 0) != 0);
    terminate();
    check(callback_count == 3 && tls_count == 1);
    char untracked[8]{};
    check(pread(&attr, file, untracked, 5, 0) == 0);
    check(close(nullptr, file) == 0 && erase_file(nullptr, "/data/file") == 0);
    check(fs::remove(root / "assets") && fs::remove(root / "data" / "data") && fs::remove(root / "data") && fs::remove(root));
    std::puts("25 exact FIOS NIDs, real import adapter, four-argument ABI, short read, rejected mapping and write callbacks passed");
}
