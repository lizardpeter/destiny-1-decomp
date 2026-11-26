#include "native_record_registry.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

struct List { uint64_t payload = 0; };
using Registry = d1::NativeRecordRegistry<List>;
static void require(bool b) {
    if (!b) { std::fputs("native record registry regression failed\n", stderr); std::abort(); }
}
int main() {
    Registry registry;
    List a{1}, b{2};
    require(!registry.publish(0, &a));
    require(!registry.publish(UINT64_MAX, &a));
    require(!registry.publish(0x1000, nullptr));
    require(registry.publish(0x1000, &a));
    require(!registry.publish(0x1000, &b));
    require(!registry.take(0x1000, 1));
    require(!registry.take(0x1030, 0));
    require(!registry.take(0x1030, 65));
    require(registry.take(0x1030, 1) == &a);
    require(!registry.take(0x1030, 1));
    require(registry.publish(0x1000, &b));
    require(registry.take(0x1030, 64) == &b);
    require(registry.publish(0x1000,&a));
    require(!registry.take(0x1030,0));
    require(registry.take_record(0x1000)==&a);
    require(!registry.take_record(0x1000));
    registry.abandon(&a);
    registry.abandon(&b);
    registry.abandon(nullptr);
    require(registry.take_abandoned() == std::vector<List*>({&a, &b}));
    require(registry.take_abandoned().empty());
    constexpr unsigned count = 25000;
    std::atomic<unsigned> completed{0};
    std::array<std::thread, 4> workers;
    for (unsigned t = 0; t < workers.size(); ++t) workers[t] = std::thread([&, t] {
        const uint64_t rec = 0x100000 + t * 0x10000;
        List item{};
        for (unsigned i = 0; i < count; ++i) {
            item.payload = (uint64_t(t) << 32) | i;
            require(registry.publish(rec, &item));
            require(!registry.publish(rec, &a));
            require(registry.take(Registry::key(rec), 1) == &item);
            require(!registry.take(Registry::key(rec), 1));
            ++completed;
        }
    });
    for (auto& worker : workers) worker.join();
    require(completed == 100000);
    std::puts("100000 concurrent native record ownership cycles and boundary checks passed");
}
