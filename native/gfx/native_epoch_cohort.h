#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
namespace d1::ng {
// A preparation cohort can reuse one persistent image per resource ID,
// but it cannot upload two different versions ahead of all its draws.
// The small direct cache avoids a hash-table lookup on repeated binds.
class EpochCohort {
    std::array<uint64_t,256> recent_{};
    std::unordered_map<uint32_t,uint32_t> epochs_;
public:
    bool observe(uint64_t res){
        if(!res)return true;
        const uint32_t id=uint32_t(res>>32),epoch=uint32_t(res);
        auto& recent=recent_[id&255];
        if(recent==res)return true;
        const auto [it,inserted]=epochs_.try_emplace(id,epoch);
        if(!inserted&&it->second!=epoch)return false;
        recent=res;
        return true;
    }
    void clear(){recent_.fill(0);epochs_.clear();}
};
} // namespace d1::ng
