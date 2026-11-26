#pragma once
#include <cstddef>
#include <cstdint>
namespace d1::frame_time {
// Recovered from 0x591D40, 0x593970 and 0x74BB00 (retail 01.33).
// Names describe observed usage; unknown flags are not assigned invented meaning.
struct FiberFrameTimeInfo {
    int64_t start_us;                // +00: scheduler timestamp
    int64_t real_total_us;           // +08
    int64_t primary_total_us;        // +10
    int64_t measured_elapsed_us;     // +18
    int64_t elapsed_us;              // +20
    int64_t current_step_us;         // +28
    int64_t primary_aggregate_us;    // +30
    int64_t extra_step_us;           // +38: copied from time config +40
    int32_t remaining_steps;        // +40
    uint32_t unknown44;
};
static_assert(sizeof(FiberFrameTimeInfo) == 0x48);
static_assert(offsetof(FiberFrameTimeInfo, remaining_steps) == 0x40);

struct PublishedFrameTime {
    float elapsed;
    float measured_elapsed;
    float current_step;
    float primary_aggregate;
    float secondary_aggregate;
    int32_t primary_count;
    int32_t secondary_count;
    uint32_t unknown1c;
    uint64_t simulation_tsc;
    uint8_t has_primary;
    uint8_t first_step;
    uint8_t phase_flag;
    uint8_t last_step;
    uint8_t last_step_copy;
    uint8_t unknown2d[3];
    uint32_t frame_info_handle;
    uint32_t unknown34;
};
static_assert(sizeof(PublishedFrameTime) == 0x38);
static_assert(offsetof(PublishedFrameTime, current_step) == 8);
static_assert(offsetof(PublishedFrameTime, simulation_tsc) == 0x20);
static_assert(offsetof(PublishedFrameTime, frame_info_handle) == 0x30);

// The first call publishes the remainder of an aggregate interval; follow-up
// scheduler calls consume one extra_step_us each. This is the integer portion
// of 0x591D40, not a substitute for its gate/timescale/dependency logic.
struct PrimarySplit { int64_t first_us; int32_t remaining; };
constexpr PrimarySplit split_primary(int64_t aggregate_us, int32_t count, int64_t extra_us) {
    const int32_t remaining = count < 1 ? 0 : count - 1;
    return {aggregate_us - int64_t(remaining) * extra_us, remaining};
}
} // namespace d1::frame_time
