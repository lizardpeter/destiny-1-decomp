#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace d1 {
// Transitional engine-record association. The native list is never encoded
// in a command stream. Publish/take transfer ownership once per record use.
template<class List>
class NativeRecordRegistry {
    std::mutex mutex_;
    std::unordered_map<uint64_t, List*> published_;
    std::vector<List*> abandoned_;
public:
    // Only the legacy import boundary uses this offset; map keys are
    // actual record identities, not addresses of console command arrays.
    static constexpr uint64_t dcb_array_offset = 0x30;
    static constexpr uint64_t key(uint64_t record) {
        return record && record <= UINT64_MAX - dcb_array_offset ? record + dcb_array_offset : 0;
    }
    bool publish(uint64_t record, List* list) {
        if (!key(record) || !list) return false;
        std::lock_guard lock(mutex_);
        return published_.emplace(record, list).second;
    }
    List* take_record(uint64_t record) {
        if (!key(record)) return nullptr;
        std::lock_guard lock(mutex_);
        const auto it = published_.find(record);
        if (it == published_.end()) return nullptr;
        List* list = it->second;
        published_.erase(it);
        return list;
    }
    // Only the old retail import adapter validates segment count.
    List* take(uint64_t array_address,uint32_t segments) {
        if(array_address<=dcb_array_offset||!segments||segments>64)return nullptr;
        return take_record(array_address-dcb_array_offset);
    }
    void abandon(List* list) {
        if (!list) return;
        std::lock_guard lock(mutex_);
        abandoned_.push_back(list);
    }
    std::vector<List*> take_abandoned() {
        std::lock_guard lock(mutex_);
        std::vector<List*> result;
        result.swap(abandoned_);
        return result;
    }
};
} // namespace d1
