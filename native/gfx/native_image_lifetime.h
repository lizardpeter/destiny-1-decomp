#pragma once
#include <memory>
#include <atomic>
namespace d1::ng {
// A cache can retain a token, never ownership of the native allocation.
// The renderer serializes access; invalidate at retirement, not destruction.
// A reused allocation address cannot resurrect an old token.
template<class T> class ImageLifetime {
    struct State {std::atomic<T*> owner;explicit State(T* p):owner(p){}};
    std::shared_ptr<State> state_;
public:
    using Token=std::shared_ptr<State>;
    ImageLifetime()=default;
    ImageLifetime(const ImageLifetime&)=delete;
    ImageLifetime& operator=(const ImageLifetime&)=delete;
    ~ImageLifetime(){retire();}
    Token token(T* owner){
        if(!state_)state_=std::make_shared<State>(owner);
        return state_;
    }
    void retire(){if(state_){state_->owner.store(nullptr,std::memory_order_release);state_.reset();}}
    static T* resolve(const Token& token){return token?token->owner.load(std::memory_order_acquire):nullptr;}
};
} // namespace d1::ng
