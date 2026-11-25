#pragma once
#include <cstddef>
#include <string>

namespace AlterEgo::RefreshProgress {
// A processed request may fail. Never count an old cached success as a new request.
struct Counts {
    std::size_t total=0,ready=0,failed=0,missing=0,working=0,stale=0;
    void Add(const std::string& status,bool hasSuccess) {
        ++total;
        if(status=="Refreshing") ++working;
        else if(status=="Ready"&&hasSuccess) ++ready;
        else if(status=="Not retrieved") ++missing;
        else if(status=="Stale") ++stale;
        else ++failed;
    }
    std::string Summary() const {
        auto result=std::to_string(ready)+"/"+std::to_string(total)+" ready";
        if(working)result+=", "+std::to_string(working)+" refreshing";
        if(failed)result+=", "+std::to_string(failed)+" failed";
        if(stale)result+=", "+std::to_string(stale)+" stale or awaiting synchronization";
        if(missing)result+=", "+std::to_string(missing)+" not retrieved";
        return result;
    }
};
inline std::string Character(std::size_t ordinal,std::size_t total,const std::string& name,const std::string& stage) {
    return std::to_string(ordinal)+"/"+std::to_string(total)+" - "+name+": "+stage;
}
}
