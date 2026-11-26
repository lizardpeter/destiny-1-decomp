#pragma once
#include <cstdint>
namespace d1 {
// Native GPU queue API, shared with the transitional import adapter.
struct NativeSubmission;
struct NativeFlipCompletion {
    int64_t arg;
    uint64_t tsc;
    int32_t queue_count,pending_count;
};
NativeFlipCompletion native_flip_completion();
void native_flip_rate_hint(uint32_t rate);
NativeSubmission* native_device_capture_record(uint64_t record);
void native_queue_submit(NativeSubmission* batch,bool flip,int32_t buffer,int64_t arg);
} // namespace d1
