#pragma once
#include "NativeProgressCore.h"
#include "NativeAccounts.h"
#include "BackgroundWorkers.h"
#include <chrono>
#include <ctime>
#include <memory>
#include <thread>
namespace AlterEgo::NativeProgress {
struct AchievementResult {
    int status=0;std::string error;
    std::shared_ptr<const Cache::AchievementSnapshot> snapshot;
};
inline AchievementResult FetchAchievements(const std::string& account,bool force=false){
    using Clock=std::chrono::steady_clock;
    struct Entry {uint64_t epoch=0;Clock::time_point finished{};std::shared_ptr<const Cache::AchievementSnapshot> snapshot;};
    static std::mutex mutex;
    static std::map<std::string,Entry> entries;
    const auto started=Clock::now();
    std::unique_lock<std::mutex> lock(mutex,std::defer_lock);
    while(!lock.try_lock()){
        if(Background::Workers().Stopping())return {0,"Cancelled",{}};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    NativeAccounts::LinkedAccount a;
    for(const auto& linked:NativeAccounts::Linked())if(linked.id==account){a=linked;break;}
    if(!NativeAccounts::Current(a))return {401,"No linked account",{}};
    auto& e=entries[account];
    if(e.snapshot&&e.epoch==a.generation&&(e.finished>=started||(!force&&Clock::now()-e.finished<std::chrono::seconds(15))))
        return {200,{},e.snapshot};
    auto r=NativeAccounts::Request(a,"/v2/account/achievements",{"progression"});
    if(!r)return {r.status,r.error,{}};
    try{
        auto snapshot=std::make_shared<const Cache::AchievementSnapshot>(ParseAchievements(r.data,std::time(nullptr)));
        if(!NativeAccounts::Current(a))return {0,"Account changed",{}};
        e={a.generation,Clock::now(),snapshot};return {200,{},snapshot};
    }catch(...){return {0,"Invalid account achievement response",{}};}
}
}
