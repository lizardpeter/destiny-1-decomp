#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <algorithm>
namespace d1::ng {
// Reusable flat table for ordered preparation segments. A segment reset
// changes a generation, rather than freeing thousands of hash-map nodes.
// Grow before the load factor exceeds 50%; exact key equality remains
// authoritative. No pointer into the table is retained across insertion.
template<class Key,class Value,class Hash=std::hash<Key>>
class SegmentTable {
    struct Entry { Key key{}; Value value{}; uint32_t stamp=0; };
    std::vector<Entry> entries_;
    uint32_t stamp_=1;
    size_t used_=0;
    static size_t hash(const Key& key){
        uint64_t h=uint64_t(Hash{}(key));
        h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;
        return size_t(h);
    }
    void grow(){
        auto old=std::move(entries_);
        entries_.resize(old.empty()?256:old.size()*2);
        used_=0;
        for(const auto& e:old)if(e.stamp==stamp_)insert(e.key,e.value);
    }
public:
    template<class Create>
    std::pair<Value,bool> get_or_insert(const Key& key,Create create){
        if(entries_.empty()||used_*2>=entries_.size())grow();
        const size_t mask=entries_.size()-1;
        size_t i=hash(key)&mask;
        for(;;i=(i+1)&mask){
            auto& e=entries_[i];
            if(e.stamp!=stamp_){
                Value value=create();
                e={key,value,stamp_};++used_;return {value,true};
            }
            if(e.key==key)return {e.value,false};
        }
    }
    std::pair<Value,bool> insert(const Key& key,const Value& value){
        return get_or_insert(key,[&]{return value;});
    }
    void clear(){
        used_=0;
        if(++stamp_==0){
            for(auto& e:entries_)e.stamp=0;
            stamp_=1;
        }
    }
};
class EpochCohort {
    std::array<uint64_t,256> recent_{};
    SegmentTable<uint32_t,uint32_t> epochs_;
public:
    bool observe(uint64_t res){
        if(!res)return true;
        const uint32_t id=uint32_t(res>>32),epoch=uint32_t(res);
        auto& recent=recent_[id&255];
        if(recent==res)return true;
        const auto [prior,inserted]=epochs_.insert(id,epoch);
        if(!inserted&&prior!=epoch)return false;
        recent=res;
        return true;
    }
    void clear(){recent_.fill(0);epochs_.clear();}
};
} // namespace d1::ng
