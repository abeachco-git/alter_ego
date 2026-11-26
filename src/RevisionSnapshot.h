#pragma once
#include <cstdint>
#include <memory>
#include <utility>

namespace AlterEgo {
// Caller serializes Acquire and the source revision/contents together.
// Readers own a const snapshot and need no source lock. Never rebuild on a
// stable revision, and do not advance the revision if construction throws.
template<class T>
class RevisionSnapshot {
    uint64_t revision_=0;
    std::shared_ptr<const T> value_;
public:
    template<class Build>
    std::shared_ptr<const T> Acquire(uint64_t revision,Build&& build) {
        if(!value_||revision_!=revision) {
            auto next=std::make_shared<const T>(std::forward<Build>(build)());
            value_=std::move(next);
            revision_=revision;
        }
        return value_;
    }
};
}
