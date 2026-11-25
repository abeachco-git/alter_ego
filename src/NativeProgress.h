#pragma once
#include "NativeProgressCore.h"
#include "NativeAccounts.h"
#include "BackgroundWorkers.h"
#include "RefreshEngine.h"
#include <chrono>
#include <ctime>
#include <memory>
#include <thread>
namespace AlterEgo::NativeProgress {
struct AchievementResult {
    int status=0;std::string error;
    std::shared_ptr<const Cache::AchievementSnapshot> snapshot;
};
struct AchievementEntry {
    uint64_t epoch=0;
    std::chrono::steady_clock::time_point finished{};
    int64_t failedAt=0;
    int errorStatus=0;
    std::string error;
    std::shared_ptr<const Cache::AchievementSnapshot> snapshot;
};
inline std::mutex& AchievementMutex(){static std::mutex m;return m;}
inline std::map<std::string,AchievementEntry>& AchievementEntries(){static std::map<std::string,AchievementEntry> e;return e;}
inline bool AchievementDue(const std::string& account,int64_t localSuccess,int64_t interval,int64_t minimumEpoch=0){
    std::unique_lock<std::mutex> lock(AchievementMutex(),std::try_to_lock);
    if(!lock.owns_lock())return false;
    auto it=AchievementEntries().find(account);
    const auto now=static_cast<int64_t>(std::time(nullptr));
    if(it!=AchievementEntries().end()){
        bool current=false;
        for(const auto& a:NativeAccounts::Linked())if(a.id==account&&a.generation==it->second.epoch){current=true;break;}
        if(!current)return true;
        // Adopt a sufficiently current shared snapshot; a failed refresh must
        // not cause repeated workers trying to adopt an expired one.
        if(it->second.snapshot&&it->second.snapshot->fetchedAt>localSuccess&&
           !RefreshEngine::Due(it->second.snapshot->fetchedAt,0,now,interval,false,minimumEpoch,
               RefreshEngine::Offset(account+"/achievements")))return true;
        return RefreshEngine::Due(localSuccess,it->second.failedAt,now,interval,false,minimumEpoch,RefreshEngine::Offset(account+"/achievements"));
    }
    return RefreshEngine::Due(localSuccess,0,now,interval,false,minimumEpoch,RefreshEngine::Offset(account+"/achievements"));
}
inline AchievementResult FetchAchievements(const std::string& account,bool force=false,
        int64_t maxAge=RefreshEngine::BrowseSeconds,int64_t minimumEpoch=0){
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    std::unique_lock<std::mutex> lock(AchievementMutex(),std::defer_lock);
    while(!lock.try_lock()){
        if(Background::Workers().Stopping())return {0,"Cancelled",{}};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    NativeAccounts::LinkedAccount a;
    for(const auto& linked:NativeAccounts::Linked())if(linked.id==account){a=linked;break;}
    if(!NativeAccounts::Current(a))return {401,"No linked account",{}};
    auto& e=AchievementEntries()[account];
    if(e.epoch!=a.generation){e={};e.epoch=a.generation;}
    const auto now=static_cast<int64_t>(std::time(nullptr));
    if(e.snapshot&&e.snapshot->fetchedAt>=minimumEpoch&&
       (e.finished>=started||(!force&&!RefreshEngine::Due(e.snapshot->fetchedAt,0,now,maxAge,false,minimumEpoch,RefreshEngine::Offset(account+"/achievements")))))
        return {200,{},e.snapshot};
    if(!force&&e.failedAt>0&&e.failedAt<=now&&now-e.failedAt<RefreshEngine::FailureSeconds)
        return {e.errorStatus,e.error.empty()?"Achievement retry cooldown":e.error,{}};
    auto fail=[&](int status,std::string error){
        e.failedAt=static_cast<int64_t>(std::time(nullptr));e.errorStatus=status;e.error=error;
        return AchievementResult{status,std::move(error),{}};
    };
    auto r=NativeAccounts::Request(a,"/v2/account/achievements",{"progression"});
    if(!r)return fail(r.status,r.error);
    try{
        auto snapshot=std::make_shared<const Cache::AchievementSnapshot>(ParseAchievements(r.data,std::time(nullptr)));
        if(!NativeAccounts::Current(a))return {0,"Account changed",{}};
        e.snapshot=snapshot;e.finished=Clock::now();e.failedAt=0;e.error.clear();e.errorStatus=0;
        return {200,{},snapshot};
    }catch(...){return fail(0,"Invalid account achievement response");}
}
}
