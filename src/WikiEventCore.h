#pragma once
#include "UntrustedInput.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace AlterEgo::WikiEvents {
using Json=nlohmann::json;
using Timers=std::map<std::string,std::vector<int>>;
struct Phase {const char* group;const char* name;const char* label;};
#include "WikiEventMetadata.inc"
struct Snapshot {
    Timers timers;
    int64_t verified=0,attempt=0;
    bool embedded=true;
};
inline void Validate(const Timers& timers) {
    if(timers.size()!=sizeof(Phases)/sizeof(Phases[0]))throw std::runtime_error("Incomplete event timer coverage");
    for(const auto& phase:Phases){
        const auto& starts=timers.at(phase.label);
        if(starts.empty()||starts.front()<0||starts.back()>=1440||
           !std::is_sorted(starts.begin(),starts.end())||
           std::adjacent_find(starts.begin(),starts.end())!=starts.end())
            throw std::runtime_error("Invalid event start times");
    }
}
inline int BoundedInt(const Json& n,int low,int high) {
    if(!n.is_number_integer())throw std::runtime_error("Noninteger event step");
    const auto value=n.get<int64_t>();
    if(value<low||value>high)throw std::runtime_error("Out of range event step");
    return static_cast<int>(value);
}
inline Timers Parse(const Json& raw) {
    const auto& events=raw.at("events");
    if(!events.is_object()||events.size()<40)throw std::runtime_error("Incomplete event timer");
    Timers result;
    for(const auto& phase:Phases){
        const auto& event=events.at(phase.group);
        const auto& segments=event.at("segments");
        if(!segments.is_object()||segments.empty())throw std::runtime_error("Missing event segments");
        std::string target;
        for(auto it=segments.begin();it!=segments.end();++it){
            if(it.value().value("name",std::string{})==phase.name){
                if(!target.empty())throw std::runtime_error("Ambiguous event phase");
                target=it.key();
            }
        }
        if(target.empty())throw std::runtime_error("Missing event phase");
        const auto& seq=event.at("sequences");
        const auto& partial=seq.at("partial");
        const auto& pattern=seq.at("pattern");
        auto duration=[&](const Json& rows){
            if(!rows.is_array()||rows.size()>1440)throw std::runtime_error("Invalid event sequence");
            int total=0;
            for(const auto& step:rows){
                const auto key=std::to_string(BoundedInt(step.at("r"),0,1000000));
                if(!segments.contains(key))throw std::runtime_error("Unknown event segment");
                total+=BoundedInt(step.at("d"),1,1440);
                if(total>2880)throw std::runtime_error("Oversized event sequence");
            }
            return total;
        };
        const int prefix=duration(partial),cycle=duration(pattern);
        std::vector<int> starts;
        // Match the build parser's midnight phase alignment.
        int t=cycle?prefix:0;
        if(cycle){
            if(cycle>1440||1440%cycle)throw std::runtime_error("Non-daily event cycle");
            for(const auto& step:pattern){
                if(std::to_string(BoundedInt(step.at("r"),0,1000000))==target)
                    for(int start=t%cycle;start<1440;start+=cycle)starts.push_back(start);
                t+=BoundedInt(step.at("d"),1,1440);
            }
        }else{
            if(prefix<1440)throw std::runtime_error("Incomplete daily event sequence");
            for(const auto& step:partial){
                if(std::to_string(BoundedInt(step.at("r"),0,1000000))==target&&t<1440)starts.push_back(t);
                t+=BoundedInt(step.at("d"),1,1440);
            }
        }
        std::sort(starts.begin(),starts.end());
        starts.erase(std::unique(starts.begin(),starts.end()),starts.end());
        result.emplace(phase.label,std::move(starts));
    }
    Validate(result);
    return result;
}
inline Timers ParseResponse(const std::string& body,bool wrapped) {
    if(body.empty()||body.size()>4*1024*1024)throw std::runtime_error("Invalid event payload size");
    auto data=Untrusted::Parse(body,Untrusted::Wiki);
    if(wrapped){
        if(data.contains("error"))throw std::runtime_error("Wiki API error");
        const auto& parse=data.at("parse");
        if(parse.at("title").get<std::string>()!="Widget:Event timer/data.json")
            throw std::runtime_error("Unexpected Wiki page");
        data=Untrusted::Parse(parse.at("wikitext").at("*").get<std::string>(),Untrusted::Wiki);
    }
    return Parse(data);
}
inline const Snapshot& Embedded() {
    static const Snapshot value=[]{
        Snapshot s;
        s.timers={
#include "WikiEventTimers.inc"
        };
        s.verified=EmbeddedVerifiedAt;
        Validate(s.timers);
        return s;
    }();
    return value;
}
inline Snapshot Select(const Snapshot& baseline,const Snapshot& cached) {
    auto chosen=(cached.verified>0&&cached.verified>=baseline.verified)?cached:baseline;
    chosen.attempt=std::max(baseline.attempt,cached.attempt);
    return chosen;
}
inline std::mutex& StateMutex(){static std::mutex m;return m;}
inline std::shared_ptr<const Snapshot>& State(){
    static auto value=std::make_shared<const Snapshot>(Embedded());
    return value;
}
inline std::shared_ptr<const Snapshot> Get(){
    std::lock_guard<std::mutex> lock(StateMutex());return State();
}
inline void Publish(Snapshot value){
    Validate(value.timers);
    auto next=std::make_shared<const Snapshot>(std::move(value));
    std::lock_guard<std::mutex> lock(StateMutex());State()=std::move(next);
}
} // namespace AlterEgo::WikiEvents
