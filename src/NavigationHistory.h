#pragma once
#include <vector>
#include <cstddef>
#include <utility>

namespace AlterEgo::Navigation {
template<class State> class History {
    std::vector<State> entries_;
    std::size_t cursor_=0;
    static constexpr std::size_t Limit=64;
public:
    bool Empty() const {return entries_.empty();}
    const State* Current() const {return Empty()?nullptr:&entries_[cursor_];}
    bool CanMove(int direction) const {
        return !Empty()&&(direction<0?cursor_>0:direction>0&&cursor_+1<entries_.size());
    }
    void Observe(State state,bool deliberate) {
        if(Empty()){entries_.push_back(std::move(state));return;}
        if(!deliberate||entries_[cursor_].SameDestination(state)) {
            entries_[cursor_]=std::move(state);return;
        }
        entries_.erase(entries_.begin()+cursor_+1,entries_.end());
        entries_.push_back(std::move(state));
        if(entries_.size()>Limit)entries_.erase(entries_.begin());
        cursor_=entries_.size()-1;
    }
    const State* Move(int direction) {
        if(!CanMove(direction))return nullptr;
        if(direction<0)--cursor_;else ++cursor_;
        return Current();
    }
    void Clear(){entries_.clear();cursor_=0;}
};
}
