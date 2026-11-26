#pragma once
#include <mutex>

namespace AlterEgo::AccountCommit {
// The same lock guards credential changes and the final identity check + commit.
// Never run HTTP, UI or host callbacks in this critical section.
template<class Current,class Commit>
bool Run(std::mutex& mutex,Current current,Commit commit) {
    std::lock_guard<std::mutex> lock(mutex);
    if(!current())return false;
    return commit();
}
}
