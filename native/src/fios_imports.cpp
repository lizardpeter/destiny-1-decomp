// Transitional retail import boundary. All operations use one native provider;
// never mix its file, directory or operation handles with the retail PRX.
#include "fios_imports.h"
#include "fios_contract.h"
#include "fios_native_core.h"
#include "hle.h"
#include "host_memory.h"
#include "nid.h"
#include "runtime.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace d1 {
struct Thread;
Thread* ensure_self(const char* name);
void register_guest_thread(const char* name);

namespace {
using namespace fios_contract;
using fios_native::Core;
using pc_io::Queue;
using pc_io::Result;
using pc_io::error;

// The original game exits through the runtime's process exit path. Keep this
// provider alive for late libc calls, rather than racing static destruction
// against guest threads. Termination drains outstanding I/O and callbacks.
Core* core = nullptr;
thread_local bool in_callback = false;
thread_local bool callback_thread_ready = false;
constexpr uint32_t kUnsupported = 0x80820006;
constexpr uint32_t kInvalid = 0x80820008;
constexpr uint32_t kFailure = 0x80820001;

template<class R, class F> R boundary(R failure, F&& fn) noexcept {
    if (!core) return failure;
    try { return fn(); }
    catch (const std::exception& e) { log("native FIOS: %s", e.what()); }
    catch (...) { log("native FIOS: unexpected host exception"); }
    return failure;
}
bool supported_attributes(const OperationAttributes* a, bool allow_callback) {
    if (!a) return true;
    if (a->unknown00 || (!allow_callback && (a->callback || a->user_context))) return false;
    for (auto v : a->unknown18) if (v) return false;
    return true;
}
bool executable_callback(uint64_t addr) {
    for (const auto& m : rt().modules)
        if (addr >= m.text_lo && addr < m.text_hi) return true;
    return false;
}
bool callback_for(const OperationAttributes* attr, uint64_t requested, bool is_read, Queue::Callback& out) {
    if (!supported_attributes(attr, true)) {
        log("native FIOS: unverified operation attributes");
        return false;
    }
    if (!attr || !attr->callback) return true;
    const auto callback = attr->callback;
    const auto context = attr->user_context;
    if (!executable_callback(callback)) {
        log("native FIOS: callback is outside a loaded executable segment");
        return false;
    }
    // Copy stack-owned attributes before returning to the original caller.
    out = [callback, context, requested, is_read](Queue::Id op, Result r) {
        if (!callback_thread_ready) {
            tls_set_current(tls_create_thread_block());
            ensure_self("NativeFiosCallback");
            register_guest_thread("NativeFiosCallback");
            os::set_thread_name("NativeFiosCallback");
            callback_thread_ready = true;
        }
        uint32_t result = fios_native::status(r.error);
        // 0x5690 treats EOF as a successful short synchronous count; the
        // engine's asynchronous caller recognizes the same status.
        if (!result && is_read && r.bytes < requested) result = kObservedReadStatus;
        in_callback = true;
        d1_call_guest4(reinterpret_cast<const void*>(callback), context, op, kTerminalCallbackEvent, result);
        in_callback = false;
    };
    return true;
}

uint32_t initialize(const void* params) {
    if (!core || !params) return kInvalid;
    uint32_t version;
    std::memcpy(&version, params, sizeof version);
    // Retail 0xFA0 extracts bits 1..15 and checks structure size 0x98.
    if (((version >> 1) & 0x7fff) != 0x98) return kInvalid;
    log("native FIOS: initialized (experimental native import cohort)");
    return 0;
}
void terminate() {
    if (!core) return;
    if (in_callback) {
        log("native FIOS: termination from its own callback unsupported");
        return;
    }
    boundary<uint32_t>(kFailure, [] { core->drain(); return uint32_t(0); });
}
uint32_t open_mode(const OperationAttributes* attr, uint32_t* out, const char* name,
                   const OpenAttributes* options, int32_t mode) {
    if (!out) return kInvalid;
    *out = 0;
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] {
        return core->open(name, options ? options->flags : uint16_t(1), mode, *out);
    });
}
uint32_t open(const OperationAttributes* attr, uint32_t* out, const char* name, const OpenAttributes* options) {
    return open_mode(attr, out, name, options, -1);
}
uint32_t close(const OperationAttributes* attr, uint32_t handle) {
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->close(handle); });
}
int64_t read_sync(const OperationAttributes* attr, uint32_t handle, void* data, int64_t count) {
    if (!supported_attributes(attr, false)) return int32_t(kUnsupported);
    if (count < 0 || (count && !data)) return int32_t(kInvalid);
    return boundary<int64_t>(int32_t(kFailure), [&] { return core->read(handle, data, uint64_t(count)); });
}
int64_t write_sync(const OperationAttributes* attr, uint32_t handle, const void* data, int64_t count) {
    if (!supported_attributes(attr, false)) return int32_t(kUnsupported);
    if (count < 0 || (count && !data)) return int32_t(kInvalid);
    return boundary<int64_t>(int32_t(kFailure), [&] { return core->write(handle, data, uint64_t(count)); });
}
uint32_t read_at(const OperationAttributes* attr, uint32_t handle, void* dst, int64_t count, int64_t offset) {
    if (count < 0 || offset < 0 || (count && !dst)) return 0;
    return boundary<uint32_t>(0, [&] {
        Queue::Callback callback;
        if (!callback_for(attr, uint64_t(count), true, callback)) return uint32_t(0);
        const uint64_t addr = reinterpret_cast<uint64_t>(dst);
        const uint64_t identity = count ? guest_mapping_identity(addr, uint64_t(count)) : 0;
        if (count && !identity) {
            // A queued raw pointer cannot be safely delivered after recycling.
            log("native FIOS: async read destination has no tracked mapping");
            return uint32_t(0);
        }
        auto deliver = [addr, identity](std::span<const unsigned char> bytes, Result r) {
            if (bytes.empty()) return r;
            struct Copy { const unsigned char* src; size_t n; } copy{bytes.data(), bytes.size()};
            if (!guest_write_current_mapping(addr, bytes.size(), identity,
                [](void* dst, void* opaque) {
                    auto& c = *static_cast<Copy*>(opaque);
                    std::memcpy(dst, c.src, c.n);
                }, &copy))
                return Result{0, error(std::errc::operation_canceled)};
            return r;
        };
        std::error_code ec;
        return core->read_at(handle, uint64_t(offset), uint64_t(count), std::move(deliver), std::move(callback), ec);
    });
}
uint32_t write_at(const OperationAttributes* attr, uint32_t handle, const void* src, int64_t count, int64_t offset) {
    if (count < 0 || offset < 0 || (count && !src)) return 0;
    return boundary<uint32_t>(0, [&] {
        Queue::Callback callback;
        if (!callback_for(attr, uint64_t(count), false, callback)) return uint32_t(0);
        std::error_code ec;
        return core->write_at(handle, uint64_t(offset), src, uint64_t(count), std::move(callback), ec);
    });
}
uint64_t actual_count(uint32_t op) {
    return boundary<uint64_t>(0, [&] { Result r; return core->poll(op, r) ? r.bytes : uint64_t(0); });
}
void delete_op(uint32_t op) {
    boundary<uint32_t>(kFailure, [&] {
        if (!core->delete_operation(op)) log("native FIOS: delete of unknown operation %#x", op);
        return uint32_t(0);
    });
}
int64_t size(uint32_t handle) { return boundary<int64_t>(-1, [&] { return core->size(handle); }); }
int64_t tell(uint32_t handle) { return boundary<int64_t>(-1, [&] { return core->tell(handle); }); }
int64_t seek(uint32_t handle, int64_t offset, uint32_t origin) {
    return boundary<int64_t>(-1, [&] { return core->seek(handle, offset, origin); });
}
uint32_t sync(const OperationAttributes* attr, uint32_t handle) {
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->sync(handle); });
}
bool valid(uint32_t handle) { return boundary<bool>(false, [&] { return core->valid(handle); }); }
uint32_t stat(const OperationAttributes* attr, const char* name, Stat* out) {
    if (!out) return kInvalid;
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->stat(name, *out); });
}
uint32_t directory_open(const OperationAttributes* attr, uint32_t* out, const char* name,
                        uint64_t filter, uint64_t filter_context) {
    if (!out) return kInvalid;
    *out = 0;
    if (!supported_attributes(attr, false) || filter || filter_context) {
        log("native FIOS: directory filter/attribute contract not verified");
        return kUnsupported;
    }
    return boundary<uint32_t>(kFailure, [&] { return core->open_directory(name, *out); });
}
uint32_t directory_read(const OperationAttributes* attr, uint32_t handle, DirectoryEntry* out) {
    if (!out) return kInvalid;
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->read_directory(handle, *out); });
}
uint32_t directory_close(const OperationAttributes* attr, uint32_t handle) {
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->close_directory(handle); });
}
uint32_t directory_create(const OperationAttributes* attr, const char* name) {
    return boundary<uint32_t>(0, [&] {
        Queue::Callback callback;
        if (!callback_for(attr, 0, false, callback)) return uint32_t(0);
        std::error_code ec;
        return core->create_directory(name, std::move(callback), ec);
    });
}
template<Core::DeleteKind kind> uint32_t remove(const OperationAttributes* attr, const char* name) {
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->remove(name, kind); });
}
uint32_t rename(const OperationAttributes* attr, const char* from, const char* to) {
    if (!supported_attributes(attr, false)) return kUnsupported;
    return boundary<uint32_t>(kFailure, [&] { return core->rename(from, to); });
}
} // namespace

bool native_fios_enabled() {
    const char* mode = std::getenv("D1_NATIVE_FIOS");
    return mode && std::atoi(mode) != 0;
}
void native_fios_prepare() {
    if (!native_fios_enabled() || core) return;
    core = new Core(rt().app0, rt().data_root);
}
void* native_fios_import(const std::string& nid) {
    // Complete observed import closure of eboot and libc (25 distinct NIDs).
    static const std::unordered_map<std::string, void*> table{
        {nid_of("sceFiosInitialize"), guest_entry<&initialize>()},
        {nid_of("sceFiosTerminate"), guest_entry<&terminate>()},
        {nid_of("sceFiosFHOpenSync"), guest_entry<&open>()},
        {nid_of("sceFiosFHOpenWithModeSync"), guest_entry<&open_mode>()},
        {nid_of("sceFiosFHCloseSync"), guest_entry<&close>()},
        {nid_of("sceFiosFHReadSync"), guest_entry<&read_sync>()},
        {nid_of("sceFiosFHWriteSync"), guest_entry<&write_sync>()},
        {nid_of("sceFiosFHPread"), guest_entry<&read_at>()},
        {nid_of("sceFiosFHPwrite"), guest_entry<&write_at>()},
        {nid_of("sceFiosOpGetActualCount"), guest_entry<&actual_count>()},
        {nid_of("sceFiosOpDelete"), guest_entry<&delete_op>()},
        {nid_of("sceFiosFHGetSize"), guest_entry<&size>()},
        {nid_of("sceFiosFHTell"), guest_entry<&tell>()},
        {nid_of("sceFiosFHSeek"), guest_entry<&seek>()},
        {nid_of("sceFiosFHSyncSync"), guest_entry<&sync>()},
        {nid_of("sceFiosIsValidHandle"), guest_entry<&valid>()},
        {nid_of("sceFiosStatSync"), guest_entry<&stat>()},
        {nid_of("sceFiosDHOpenSync"), guest_entry<&directory_open>()},
        {nid_of("sceFiosDHReadSync"), guest_entry<&directory_read>()},
        {nid_of("sceFiosDHCloseSync"), guest_entry<&directory_close>()},
        {nid_of("sceFiosDirectoryCreate"), guest_entry<&directory_create>()},
        {nid_of("sceFiosDeleteSync"), guest_entry<&remove<Core::DeleteKind::any>>()},
        {nid_of("sceFiosFileDeleteSync"), guest_entry<&remove<Core::DeleteKind::file>>()},
        {nid_of("sceFiosDirectoryDeleteSync"), guest_entry<&remove<Core::DeleteKind::directory>>()},
        {nid_of("sceFiosRenameSync"), guest_entry<&rename>()}
    };
    const auto it = table.find(nid);
    return it == table.end() ? nullptr : it->second;
}
} // namespace d1
