#pragma once
#include <memory>
namespace d1::ng {
// A cache can retain a token, never ownership of the native allocation.
// The renderer serializes access; invalidate at retirement, not destruction.
// A reused allocation address cannot resurrect an old token.
template<class T> class ImageLifetime {
    struct State {T* owner=nullptr;};
    std::shared_ptr<State> state_;
public:
    using Token=std::shared_ptr<State>;
    ImageLifetime()=default;
    ImageLifetime(const ImageLifetime&)=delete;
    ImageLifetime& operator=(const ImageLifetime&)=delete;
    ~ImageLifetime(){retire();}
    Token token(T* owner){
        if(!state_)state_=std::make_shared<State>(State{owner});
        return state_;
    }
    void retire(){if(state_){state_->owner=nullptr;state_.reset();}}
    static T* resolve(const Token& token){return token?token->owner:nullptr;}
};
} // namespace d1::ng
