#pragma once
#include "native_resource_identity.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace d1::ng {
// Own immutable CPU source bytes under the exact resource epoch, address
// and allocation size, independently of the original package pool. A
// caller must capture the bytes under its resource lifetime gate.
class GeometrySources {
public:
    using Bytes=std::vector<uint8_t>;
    using Source=std::shared_ptr<const Bytes>;
private:
    mutable std::mutex mutex_;
    std::unordered_map<GeometryKey,Source,GeometryKeyHash> entries_;
    uint64_t bytes_=0;
    const uint64_t budget_;
public:
    explicit GeometrySources(uint64_t budget=256ull<<20):budget_(budget){}
    Source get(const GeometryKey& key) const{
        std::lock_guard lock(mutex_);
        const auto i=entries_.find(key);
        return i==entries_.end()?Source{}:i->second;
    }
    // The caller's mapping/epoch check is performed by read(). Never
    // publish partially copied or rejected sources.
    template<class Read> Source capture(const GeometryKey& key,Read read){
        if(!key.resource||!key.size||key.size>budget_)return {};
        std::lock_guard lock(mutex_);
        if(auto i=entries_.find(key);i!=entries_.end())return i->second;
        if(key.size>budget_-bytes_)return {};
        auto data=std::make_shared<Bytes>(size_t(key.size));
        if(!read(data->data(),key.size))return {};
        auto [i,inserted]=entries_.emplace(key,std::move(data));
        if(inserted)bytes_+=key.size;
        return i->second;
    }
    // Called with the exclusive lifetime gate. A source already handed
    // to a consumer remains alive, and a queued list's epoch is pinned.
    template<class Pinned> void prune(Pinned pinned){
        std::lock_guard lock(mutex_);
        for(auto i=entries_.begin();i!=entries_.end();){
            if(!pinned(i->first.resource)&&i->second.use_count()==1){
                bytes_-=i->first.size;
                i=entries_.erase(i);
            }else ++i;
        }
    }
    uint64_t bytes() const{std::lock_guard lock(mutex_);return bytes_;}
};
} // namespace d1::ng
