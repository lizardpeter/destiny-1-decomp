#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace d1::pc_io {
// Bounds allocated snapshots and staging buffers, including concurrent
// producers that have not yet published their operation to the worker queue.
class ByteBudget {
    struct State {
        const uint64_t limit;
        std::atomic<uint64_t> used{0};
        explicit State(uint64_t n) : limit(n) {}
    };
    std::shared_ptr<State> state_;
public:
    class Lease {
        friend class ByteBudget;
        std::shared_ptr<State> state_;
        uint64_t bytes_ = 0;
        Lease(std::shared_ptr<State> state, uint64_t n) : state_(std::move(state)), bytes_(n) {}
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : state_(std::move(other.state_)), bytes_(other.bytes_) { other.bytes_ = 0; }
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                reset();
                state_ = std::move(other.state_);
                bytes_ = other.bytes_;
                other.bytes_ = 0;
            }
            return *this;
        }
        ~Lease() { reset(); }
        explicit operator bool() const { return bool(state_); }
        void reset() {
            if (state_) state_->used.fetch_sub(bytes_, std::memory_order_relaxed);
            state_.reset();
            bytes_ = 0;
        }
    };
    explicit ByteBudget(uint64_t limit) : state_(std::make_shared<State>(limit)) {}
    Lease reserve(uint64_t bytes) {
        uint64_t used = state_->used.load(std::memory_order_relaxed);
        do {
            if (bytes > state_->limit - used) return {};
        } while (!state_->used.compare_exchange_weak(used, used + bytes, std::memory_order_relaxed));
        return Lease(state_, bytes);
    }
    uint64_t used() const { return state_->used.load(std::memory_order_relaxed); }
};
} // namespace d1::pc_io
