#pragma once
#include <atomic>
#include <cstdint>
namespace d1::render {
// Native frame timing telemetry, independent of graphics command submission.
struct Perf {
    std::atomic<uint64_t> guest_frames{0}, guest_ns{0};
    std::atomic<uint64_t> guest_wait_ns{0};
};
Perf& perf();
} // namespace d1::render
