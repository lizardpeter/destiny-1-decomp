#pragma once
#include "native_resource_identity.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace d1::ng {
// Immutable CPU source bytes under an exact resource epoch/address/size.
// The caller captures the bytes under the resource lifetime contract.
class GeometrySources {
public:
    using Bytes=std::vector<uint8_t>;
    using Source=std::shared_ptr<const Bytes>;
private:
    struct Entry {
        std::mutex mutex;
        std::condition_variable ready;
        Source data;
        bool done=false;
        Source wait(){
            std::unique_lock lock(mutex);
            ready.wait(lock,[&]{return done;});
            return data;
        }
    };
    mutable std::mutex mutex_;
    std::unordered_map<GeometryKey,std::shared_ptr<Entry>,GeometryKeyHash> entries_;
    uint64_t bytes_=0; // completed and in-progress reservations
    const uint64_t budget_;
public:
    explicit GeometrySources(uint64_t budget=256ull<<20):budget_(budget){}
    Source get(const GeometryKey& key) const{
        std::shared_ptr<Entry> e;
        {
            std::lock_guard lock(mutex_);
            const auto i=entries_.find(key);
            if(i==entries_.end())return {};
            e=i->second;
        }
        return e->wait();
    }
    // The first claimant reserves budget; unrelated resources are copied
    // concurrently outside the global table lock. Same-key consumers
    // share the result, and failed reads are never published.
    template<class Read> Source capture(const GeometryKey& key,Read read){
        if(!key.resource||!key.size||key.size>budget_)return {};
        std::shared_ptr<Entry> e;
        bool owner=false;
        {
            std::lock_guard lock(mutex_);
            if(auto i=entries_.find(key);i!=entries_.end())e=i->second;
            else{
                if(key.size>budget_-bytes_)return {};
                e=std::make_shared<Entry>();
                entries_.emplace(key,e);
                bytes_+=key.size;
                owner=true;
            }
        }
        if(!owner)return e->wait();
        Source result;
        try{
            auto data=std::make_shared<Bytes>(size_t(key.size));
            if(read(data->data(),key.size))result=std::move(data);
        }catch(...){
            // Allocation or a host read failure cannot strand waiters.
        }
        if(!result){
            std::lock_guard lock(mutex_);
            auto i=entries_.find(key);
            if(i!=entries_.end()&&i->second==e){
                bytes_-=key.size;
                entries_.erase(i);
            }
        }
        {
            std::lock_guard lock(e->mutex);
            e->data=result;e->done=true;
        }
        e->ready.notify_all();
        return result;
    }
    // Called with the exclusive lifetime gate. Pending copies, externally
    // held sources and epochs owned by queued lists cannot be evicted.
    template<class Pinned> void prune(Pinned pinned){
        std::lock_guard lock(mutex_);
        for(auto i=entries_.begin();i!=entries_.end();){
            if(pinned(i->first.resource)||i->second.use_count()!=1){++i;continue;}
            auto& e=*i->second;
            bool reclaim=false;
            {
                std::lock_guard entry_lock(e.mutex);
                reclaim=e.done&&e.data.use_count()==1;
            }
            if(reclaim){
                bytes_-=i->first.size;
                i=entries_.erase(i);
            }else ++i;
        }
    }
    uint64_t bytes() const{std::lock_guard lock(mutex_);return bytes_;}
};
} // namespace d1::ng
