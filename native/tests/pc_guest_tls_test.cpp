#include "pc_guest_tls.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <vector>
using namespace d1::pc_tls;
static void check(bool v) { if (!v) std::abort(); }
int main() {
    const std::array<unsigned char,7> init{1,2,3,4,5,6,7};
    for (size_t align=1;align<=4096;align*=2) {
        StaticBlock b({init.data(),init.size(),77,align},256);
        check(uintptr_t(b.data())%align==0);
        check(uintptr_t(b.tcb())%std::max(align,size_t(16))==0);
        check(b.extent()==round_up(77,std::max(align,size_t(16))));
        check(std::memcmp(b.data(),init.data(),init.size())==0);
        for(size_t i=init.size();i<b.extent()+256;++i) check(b.data()[i]==std::byte{});
    }
    try { StaticBlock b({init.data(),8,7,16},256); std::abort(); } catch(const std::invalid_argument&) {}
    try { StaticBlock b({nullptr,0,SIZE_MAX,64},256); std::abort(); } catch(const std::invalid_argument&) {}
    try { StaticBlock b({nullptr,0,32,3},256); std::abort(); } catch(const std::invalid_argument&) {}
    std::atomic<unsigned> completed{0};
    std::vector<std::thread> workers;
    for(unsigned thread=0;thread<8;++thread) workers.emplace_back([&,thread]{
        for(unsigned i=0;i<12500;++i) {
            StaticBlock b({init.data(),init.size(),77,16},256);
            DynamicBlocks d;
            const Image image{init.data(),init.size(),123,128};
            auto* p=static_cast<unsigned char*>(d.get(2,0,image));
            auto* q=static_cast<unsigned char*>(d.get(3,0,image));
            check(p&&q&&p!=q&&uintptr_t(p)%128==0&&p[6]==7&&p[122]==0);
            p[0]=static_cast<unsigned char>(thread+10);
            check(q[0]==1&&d.get(2,6,image)==p+6&&d.get(2,123,image)==nullptr);
            check(d.get(0,0,image)==nullptr&&p[0]==thread+10);
            check(b.data()[0]==std::byte{1});
            ++completed;
        }
    });
    for(auto& t:workers)t.join();
    check(completed==100000);
    std::puts("100000 independent TLS lifecycle cycles and layout checks passed");
}
