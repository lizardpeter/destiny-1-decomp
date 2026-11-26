#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace d1::pc_threads {
// Guest key names can be reused; per-thread values retain the generation of
// the key they belong to, so deletion never resurrects another thread's value.
template<class Destructor, size_t Capacity=512>
class Keys {
    struct Slot { uint64_t generation=0; Destructor destructor=nullptr; bool used=false; };
    mutable std::mutex mutex_;
    std::array<Slot,Capacity> slots_{};
public:
    struct Values {
        struct Value { uint64_t generation=0; void* data=nullptr; };
        std::array<Value,Capacity> entries{};
    };
    bool create(uint32_t& out, Destructor dtor) {
        std::lock_guard lock(mutex_);
        for (size_t i=0;i<Capacity;++i) {
            auto& s=slots_[i];
            if (s.used || s.generation==UINT64_MAX) continue;
            ++s.generation; s.destructor=dtor; s.used=true;
            out=uint32_t(i); return true;
        }
        return false;
    }
    bool erase(uint32_t key) {
        std::lock_guard lock(mutex_);
        if (key>=Capacity || !slots_[key].used) return false;
        slots_[key].used=false; slots_[key].destructor=nullptr;
        return true;
    }
    bool set(Values& values,uint32_t key,void* data) const {
        std::lock_guard lock(mutex_);
        if (key>=Capacity || !slots_[key].used) return false;
        values.entries[key]={slots_[key].generation,data}; return true;
    }
    void* get(const Values& values,uint32_t key) const {
        std::lock_guard lock(mutex_);
        if (key>=Capacity || !slots_[key].used) return nullptr;
        const auto& v=values.entries[key];
        return v.generation==slots_[key].generation ? v.data : nullptr;
    }
    // Four passes is the POSIX minimum, not yet independently measured on
    // retail libkernel. Clear values before calling, never hold a registry lock
    // across guest code, and allow a destructor to set a new value for next pass.
    template<class Invoke>
    size_t drain(Values& values,Invoke invoke,unsigned passes=4) const {
        size_t calls=0;
        for (unsigned pass=0;pass<passes;++pass) {
            bool did_work=false;
            for (size_t i=0;i<Capacity;++i) {
                Destructor dtor=nullptr; void* data=nullptr;
                {
                    std::lock_guard lock(mutex_);
                    auto& v=values.entries[i]; const auto& s=slots_[i];
                    if (s.used && v.generation==s.generation && v.data && s.destructor) {
                        data=v.data; dtor=s.destructor; v.data=nullptr;
                    }
                }
                if (dtor) { did_work=true; ++calls; invoke(dtor,data); }
            }
            if (!did_work) break;
        }
        values=Values{};
        return calls;
    }
};
} // namespace d1::pc_threads
